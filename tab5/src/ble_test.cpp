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
//   1. Scan (ACTIVE) for a device advertising the encoder's ISSC "transparent
//      UART" service. Active scan is needed: the 128-bit service UUID and name
//      ride in the scan response, which a passive scan never requests.
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
//   * There is a known ESP-Hosted (P4+C6) bug where ACTIVE scanning stops
//     delivering advertising reports after ~60-90s. We use active scan anyway
//     because we connect within seconds of seeing the encoder, well inside that
//     window. If a unit isn't found, tap the screen to restart the scan.
//   * The P4 reaches the C6 over SDIO2 on Tab5-specific pins. The generic
//     esp32-p4-evboard build uses different defaults, so we MUST set them with
//     WiFi.setPins() before BLEDevice::init() (see kSdio* below). Symptom of a
//     missing/wrong pin map: repeating "sdmmc send_op_cond 0x107 / card init
//     failed" then an abort in ble_transport_ll_init.
//   * If, with correct pins, the C6 still reports "Slave firmware version:
//     0.0.0" / a version-mismatch, the C6 needs its ESP-Hosted firmware
//     refreshed (M5Burner / M5Stack's C6 firmware-restore guide). Pins first.

#include <M5Unified.h>
#include <WiFi.h>          // only for WiFi.setPins() — see kSdio* below
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

// The encoder advertises under its Location string and does NOT put the ISSC
// service UUID in its advertising packet, so we match by name as well. Set this
// to your encoder's advertised name (its Location). Leave "" to match by the
// service UUID only.
const char* kTargetName = "mmssjbt1";

// Tab5 SDIO2 pins to the ESP32-C6 radio. The generic esp32-p4-evboard build
// has DIFFERENT defaults, so without these the P4 knocks on the wrong pins and
// the link times out (sdmmc send_op_cond 0x107 / "card init failed"). We hand
// them to the shared ESP-Hosted transport via WiFi.setPins() before BLE init.
constexpr int kSdioClk = 12, kSdioCmd = 13, kSdioD0 = 11,
              kSdioD1  = 10, kSdioD2  = 9,  kSdioD3 = 8, kSdioRst = 15;

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

// All drawing happens on the main task. BLE callbacks (scan results, notifies)
// run on the BLE host task, so they hand text to the loop via this little
// mailbox instead of touching M5GFX directly (the DSI panel doesn't like being
// drawn from two tasks at once).
portMUX_TYPE        g_inboxMux = portMUX_INITIALIZER_UNLOCKED;
std::vector<String> g_inbox;
volatile int        g_seenCount = 0;   // distinct devices seen this scan
volatile bool       g_loginJustOk = false;
volatile bool       g_scanning  = false;

void inboxPush(const String& s) {
    portENTER_CRITICAL(&g_inboxMux);
    g_inbox.push_back(s);
    portEXIT_CRITICAL(&g_inboxMux);
}

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

// Runs on the BLE host task: assemble + classify a line, but DRAW nothing here
// (hand the text to the loop via inboxPush).
void flushLine() {
    g_rxLine.trim();
    if (!g_rxLine.length()) { g_rxLine = ""; return; }

    inboxPush(g_rxLine);

    String lower = g_rxLine;
    lower.toLowerCase();
    if (lower.indexOf("password") >= 0 && g_rxLine.endsWith(":") && !g_loggedIn) {
        g_sendPwd = true;                       // act in loop()
    } else if (lower.indexOf("invalid password") >= 0) {
        inboxPush("!! invalid password");
    } else if (g_rxLine.indexOf(kLoginOk) >= 0) {
        g_loggedIn = true;
        g_loginJustOk = true;                   // loop() updates the status line
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

// --- scan: pick the encoder by its service UUID -----------------------------
// NOTE: ACTIVE scan. The encoder's 128-bit service UUID (and its name) ride in
// the scan *response*, which a passive scan never requests — so passive scan
// would never match. The known P4+C6 "active scan stops after ~60-90s" bug
// doesn't bite us because we connect within a few seconds of seeing the unit.
std::vector<String> g_seenAddrs;   // BLE-task only: dedupe so each device logs once

// Strip non-printable bytes so junk in a device name can't corrupt the screen.
String clean(const String& s) {
    String out;
    for (size_t i = 0; i < s.length(); ++i) {
        char c = s[i];
        out += (c >= 0x20 && c < 0x7f) ? c : '.';
    }
    return out;
}

class ScanCb : public BLEAdvertisedDeviceCallbacks {
    void onResult(BLEAdvertisedDevice dev) override {
        if (g_doConnect || g_target) return;     // already locked onto one

        // Nearby devices re-advertise constantly; only act on the FIRST sighting
        // of each address, otherwise the log scrolls uncontrollably.
        String addr = dev.getAddress().toString().c_str();
        for (auto& a : g_seenAddrs) if (a == addr) return;
        g_seenAddrs.push_back(addr);
        g_seenCount = g_seenAddrs.size();

        String name = dev.haveName() ? clean(dev.getName().c_str()) : String("?");
        bool hit = dev.isAdvertisingService(BLEUUID(kSvcUuid)) ||
                   (strlen(kTargetName) && name == kTargetName);
        inboxPush(String("dev ") + addr + " " + dev.getRSSI() +
                  (hit ? " *SAX* " : " ") + name);

        if (hit) {
            g_scan->stop();
            g_target = new BLEAdvertisedDevice(dev);
            g_doConnect = true;
        }
    }
};
ScanCb g_scanCb;

void startScan() {
    g_doConnect = false;
    g_seenCount = 0;
    g_seenAddrs.clear();
    g_scanning = true;
    if (g_target) { delete g_target; g_target = nullptr; }
    setStatus("scanning");
    g_scan->setAdvertisedDeviceCallbacks(&g_scanCb, /*wantDuplicates=*/false);
    g_scan->setActiveScan(true);     // need scan responses (service UUID + name)
    g_scan->setInterval(160);
    g_scan->setWindow(160);
    g_scan->clearResults();
    g_scan->start(0, nullptr, false); // 0 = until we stop it
}

// --- connect + discover -----------------------------------------------------
void doConnect() {
    g_scanning = false;
    setStatus("connecting");

    // NimBLE won't connect while a scan is still active (returns status=2,
    // BLE_HS_EALREADY). Stopping inside the scan callback isn't enough - the GAP
    // scan takes a moment to actually wind down - so stop it here on the main
    // task and give it time before connecting.
    g_scan->stop();
    delay(200);

    if (!g_client) g_client = BLEDevice::createClient();
    g_client->setConnectTimeout(12);   // seconds

    // Retry a few times: the first connect right after a scan is often refused.
    bool ok = false;
    for (int attempt = 1; attempt <= 3 && !ok; ++attempt) {
        logLine(String("connect attempt ") + attempt);
        ok = g_client->connect(g_target);
        if (!ok) delay(400);
    }
    if (!ok) {
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
    // Dim the backlight: at full brightness the 5" panel + BLE TX spikes can sag
    // the rail enough to trip the brownout detector during a connect.
    M5.Display.setBrightness(80);
    M5.Display.fillScreen(TFT_BLACK);

    Serial.begin(115200);
    delay(200);

    setStatus("init BLE");
    logLine("If this line is the last you see, BLEDevice::init() hung -");
    logLine("that points at the C6 / ESP-Hosted link.");

    // CRITICAL for the Tab5: tell the ESP-Hosted transport which pins reach the
    // C6 before any BLE/Wi-Fi init. Both stacks share this one SDIO link, so
    // setting it here is enough for BLE. Without it: sdmmc 0x107 timeouts.
    WiFi.setPins(kSdioClk, kSdioCmd, kSdioD0, kSdioD1, kSdioD2, kSdioD3, kSdioRst);

    BLEDevice::init("SAXBLE-Tab5");
    // (No setPower() here: the TX-power enum isn't exposed the same way on the
    // P4/ESP-Hosted BLE build, and the default level is fine for a bench test.)
    g_scan = BLEDevice::getScan();

    startScan();
}

// Drain text queued by the BLE-task callbacks and draw it (main task only).
void drainInbox() {
    for (;;) {
        String s;
        portENTER_CRITICAL(&g_inboxMux);
        if (!g_inbox.empty()) { s = g_inbox.front(); g_inbox.erase(g_inbox.begin()); }
        portEXIT_CRITICAL(&g_inboxMux);
        if (!s.length()) break;
        logLine(s);
    }
}

void loop() {
    M5.update();

    drainInbox();

    // While scanning, keep the device count visible so it's obvious the radio
    // is working even before the encoder shows up.
    static int lastSeen = -1;
    if (g_scanning) {
        int seen = g_seenCount;
        if (seen != lastSeen) {
            lastSeen = seen;
            setStatus(String("scanning (") + seen + " seen)");
        }
    } else {
        lastSeen = -1;
    }

    if (g_loginJustOk) { g_loginJustOk = false; setStatus("logged in"); }

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
