// SAXBLE Tab5 bring-up test #2 — BLE central -> SAX-D encoder.
//
// This is the spike that answers the only real porting question for the Tab5:
// can the ESP32-P4 act as a BLE *central* (the role the phone "BLE Terminal"
// app plays) through its ESP32-C6 radio co-processor?  On the Cardputer the P4
// had its own radio; here BLE runs on the C6 and tunnels over ESP-Hosted, and
// the Arduino BLE-central path on that combo is new (and, as of 2026, a little
// fragile — see the notes below).
//
// What it does, fully automatically:
//   1. Scan (PASSIVE — see note) for a device advertising the encoder's ISSC
//      "transparent UART" service.
//   2. Connect, discover characteristics, subscribe to every notify char.
//   3. Log in: when the encoder prints "Password:", send the password one
//      character at a time, terminated with CR+LF.
//   4. On the "Welcome to Shire" banner, send a harmless read command
//      ("gas a list") and show the reply.
//
// Everything the encoder says is shown on the 5" screen and printed to USB
// serial (115200). Tap the screen to force a rescan.
//
// --- Important Tab5/P4 caveats (read if it misbehaves) ----------------------
//   * Uses the BLE library bundled with the Arduino core (BLEDevice.h), NOT
//     NimBLE-Arduino. NimBLE-Arduino has historically failed to compile on the
//     P4; the core's bundled stack is the one that got the P4/ESP-Hosted fixes.
//   * PASSIVE scan on purpose: there is a known ESP-Hosted (P4+C6) bug where
//     ACTIVE scanning stops delivering advertising reports after ~60-90s.
//     Passive scan is steadier and still sees the service UUID in the advert.
//     A side effect is we may not get the device *name* (that rides in the scan
//     response), so we match the encoder by its service UUID instead.
//   * If BLEDevice::init() hangs or scanning never finds anything, the C6
//     firmware / ESP-Hosted link is the suspect — that is exactly the unknown
//     this test exists to surface. Bump the pioarduino platform tag and retry.

#include <M5Unified.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>
#include <BLEClient.h>
#include <vector>

namespace {

// Encoder's Microchip/ISSC "transparent UART" service + the Write+Notify
// characteristic, captured from the live unit on the Cardputer build.
const char* kSvcUuid     = "49535343-fe7d-4ae5-8fa9-9fafd205e455";
const char* kRxNotifyChr = "49535343-1e4d-4bd9-ba61-23c647249616"; // Write+Notify

// Login facts learned on the Cardputer: CR+LF line ending is REQUIRED (LF-only
// is rejected), and the password must be sent character-by-character or the
// last char is dropped.
const char* kPassword   = "MMSmms659";
const char* kLineEnd    = "\r\n";
const char* kLoginOk    = "Welcome to Shire";
const char* kTestCmd    = "gas a list";   // harmless read-back once logged in

// --- shared state between BLE callbacks (BLE task) and loop() (main task) ----
volatile bool g_doConnect   = false;   // scan found the encoder; connect in loop
volatile bool g_doRescan    = false;   // start a fresh scan in loop
volatile bool g_sendPwd     = false;   // saw "Password:" -> send pwd in loop
volatile bool g_loggedIn    = false;
volatile bool g_testSent    = false;

BLEAdvertisedDevice*        g_target = nullptr;
BLEClient*                  g_client = nullptr;
BLERemoteCharacteristic*    g_writeChr = nullptr;
BLEScan*                    g_scan   = nullptr;

String g_status = "boot";
String g_rxLine;                       // partial line being assembled
std::vector<String> g_log;             // last N lines, for the screen
constexpr size_t kLogMax = 14;

// --- screen ----------------------------------------------------------------
void drawStatus() {
    auto& d = M5.Display;
    d.fillRect(0, 0, d.width(), 56, TFT_NAVY);
    d.setTextColor(TFT_WHITE, TFT_NAVY);
    d.setTextSize(3);
    d.setCursor(16, 14);
    d.print("Tab5 BLE test: ");
    d.print(g_status);
}

void redrawLog() {
    auto& d = M5.Display;
    d.fillRect(0, 60, d.width(), d.height() - 60, TFT_BLACK);
    d.setTextSize(2);
    int y = 68;
    for (auto& l : g_log) {
        d.setTextColor(l.startsWith(">>") ? TFT_GREENYELLOW : TFT_LIGHTGREY,
                       TFT_BLACK);
        d.setCursor(12, y);
        d.print(l);
        y += 26;
    }
}

void logLine(const String& s) {
    Serial.println(s);
    g_log.push_back(s);
    if (g_log.size() > kLogMax) g_log.erase(g_log.begin());
    redrawLog();
}

void setStatus(const String& s) {
    g_status = s;
    drawStatus();
    Serial.println("[status] " + s);
}

// --- notifications: assemble the encoder's reply into lines -----------------
bool looksLikePrompt(const String& s) {
    if (!s.length()) return false;
    char c = s[s.length() - 1];
    return c == ':' || c == '>' || c == '?' || c == '#';
}

void flushLine() {
    g_rxLine.trim();
    if (!g_rxLine.length()) { g_rxLine = ""; return; }

    logLine(g_rxLine);

    String lower = g_rxLine;
    lower.toLowerCase();
    if (lower.indexOf("password") >= 0 && g_rxLine.endsWith(":") && !g_loggedIn) {
        g_sendPwd = true;                       // act in loop()
    } else if (lower.indexOf("invalid password") >= 0) {
        logLine("!! invalid password");
    } else if (g_rxLine.indexOf(kLoginOk) >= 0) {
        g_loggedIn = true;
        setStatus("logged in");
    }
    g_rxLine = "";
}

void notifyCallback(BLERemoteCharacteristic*, uint8_t* data, size_t len, bool) {
    for (size_t i = 0; i < len; ++i) {
        char c = (char)data[i];
        if (c == '\n')      flushLine();
        else if (c != '\r') g_rxLine += c;
    }
    if (looksLikePrompt(g_rxLine)) flushLine();   // prompts have no newline
}

// --- scan: pick the encoder by its service UUID (works with passive scan) ---
class ScanCb : public BLEAdvertisedDeviceCallbacks {
    void onResult(BLEAdvertisedDevice dev) override {
        if (g_doConnect || g_target) return;     // already have one
        if (dev.isAdvertisingService(BLEUUID(kSvcUuid))) {
            g_scan->stop();
            g_target = new BLEAdvertisedDevice(dev);
            g_doConnect = true;
        }
    }
};
ScanCb g_scanCb;

void startScan() {
    g_doConnect = false;
    if (g_target) { delete g_target; g_target = nullptr; }
    setStatus("scanning");
    g_scan->setAdvertisedDeviceCallbacks(&g_scanCb, /*wantDuplicates=*/false);
    g_scan->setActiveScan(false);    // PASSIVE — dodge the P4+C6 active-scan bug
    g_scan->setInterval(160);
    g_scan->setWindow(160);
    g_scan->clearResults();
    g_scan->start(0, nullptr, false); // 0 = until we stop it
}

// --- connect + discover -----------------------------------------------------
void doConnect() {
    setStatus("connecting");
    if (!g_client) g_client = BLEDevice::createClient();

    if (!g_client->connect(g_target)) {
        logLine("!! connect failed");
        g_doRescan = true;
        return;
    }

    g_writeChr = nullptr;
    int subscribed = 0;

    // Prefer the known service; if it's missing, walk everything.
    std::vector<BLERemoteService*> svcs;
    if (auto* s = g_client->getService(BLEUUID(kSvcUuid))) svcs.push_back(s);
    if (svcs.empty()) {
        std::map<std::string, BLERemoteService*>* all = g_client->getServices();
        if (all) for (auto& kv : *all) svcs.push_back(kv.second);
    }

    for (auto* s : svcs) {
        std::map<std::string, BLERemoteCharacteristic*>* chars =
            s->getCharacteristics();
        if (!chars) continue;
        for (auto& kv : *chars) {
            BLERemoteCharacteristic* ch = kv.second;
            bool w = ch->canWrite() || ch->canWriteNoResponse();
            bool n = ch->canNotify() || ch->canIndicate();
            logLine(String("chr ") + (w ? "W" : "-") + (n ? "N" : "-") + " " +
                    ch->getUUID().toString().c_str());
            if (!g_writeChr && w) g_writeChr = ch;
            if (n) { ch->registerForNotify(notifyCallback); subscribed++; }
        }
    }

    if (!g_writeChr) {
        logLine("!! no writable characteristic");
        g_client->disconnect();
        g_doRescan = true;
        return;
    }
    logLine(String("subscribed ") + subscribed + " notify char(s)");
    setStatus("linked (waiting for prompt)");
}

// --- write helpers ----------------------------------------------------------
// Char-by-char with a small gap: the encoder's password input drops characters
// from a single bulk BLE write.
void sendSlow(const String& line) {
    if (!g_writeChr) return;
    String payload = line + kLineEnd;
    logLine(String(">> ") + line);
    for (size_t i = 0; i < payload.length(); ++i) {
        uint8_t c = (uint8_t)payload[i];
        g_writeChr->writeValue(&c, 1, /*response=*/false);
        delay(35);
    }
}

void sendLine(const String& line) {
    if (!g_writeChr) return;
    String payload = line + kLineEnd;
    logLine(String(">> ") + line);
    g_writeChr->writeValue((uint8_t*)payload.c_str(), payload.length(), false);
}

} // namespace

void setup() {
    auto cfg = M5.config();
    M5.begin(cfg);
    M5.Display.setRotation(1);
    M5.Display.fillScreen(TFT_BLACK);

    Serial.begin(115200);
    delay(200);

    setStatus("init BLE");
    logLine("If this line is the last you see, BLEDevice::init() hung -");
    logLine("that points at the C6 / ESP-Hosted link.");

    BLEDevice::init("SAXBLE-Tab5");
    BLEDevice::setPower(ESP_PWR_LVL_P9);
    g_scan = BLEDevice::getScan();

    startScan();
}

void loop() {
    M5.update();

    // Tap anywhere to force a rescan (e.g. if you power-cycled the encoder).
    if (M5.Touch.getCount() > 0 && M5.Touch.getDetail(0).wasPressed()) {
        if (g_client && g_client->isConnected()) g_client->disconnect();
        g_doRescan = true;
    }

    if (g_doRescan) {
        g_doRescan = false;
        g_loggedIn = false;
        g_testSent = false;
        delay(200);
        startScan();
    }

    if (g_doConnect) {
        g_doConnect = false;
        doConnect();
    }

    if (g_sendPwd) {
        g_sendPwd = false;
        setStatus("sending password");
        sendSlow(kPassword);
    }

    // Once logged in, fire the read-back command exactly once.
    if (g_loggedIn && !g_testSent) {
        g_testSent = true;
        delay(300);
        sendLine(kTestCmd);
    }

    delay(10);
}
