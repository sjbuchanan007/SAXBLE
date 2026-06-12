#include "ble_uart.h"
#include "config.h"
#include "session_log.h"
#include <NimBLEDevice.h>
#include <utility>
#include <vector>

namespace BleUart {
namespace {

State g_state = State::Idle;
NimBLEClient*               g_client    = nullptr;
NimBLERemoteCharacteristic* g_rxChar    = nullptr; // we write here
NimBLERemoteCharacteristic* g_txChar    = nullptr; // we get notifications here
NimBLEAdvertisedDevice*     g_target    = nullptr; // found, awaiting connect
volatile bool               g_doConnect = false;
bool                        g_paused    = false;
String                      g_peerName;
String                      g_rxBuffer;            // accumulates partial lines

std::function<void(const String&)> g_lineCb;
std::function<void(State)>          g_stateCb;
std::function<void()>               g_devicesCb;

// Devices seen during the current scan. The NimBLEAdvertisedDevice pointers stay
// valid until the scan results are cleared (only done in startScan()).
struct FoundDev {
    NimBLEAdvertisedDevice* dev;
    String addr;
    String label;
    int    rssi;
    bool   hasNus;
    bool   named;
};
std::vector<FoundDev> g_found;

void setState(State s) {
    if (s == g_state) return;
    g_state = s;
    if (g_stateCb) g_stateCb(s);
}


// --- Notifications: split the stream into text lines ------------------------
// The encoder ends replies with newlines, but prompts (e.g. "Password:") have
// no trailing newline, so we also surface the buffer when it ends in a prompt
// character. Otherwise a prompt would sit unseen in the buffer forever.
void emitBuffer() {
    g_rxBuffer.trim();
    if (g_rxBuffer.length() && g_lineCb) g_lineCb(g_rxBuffer);
    g_rxBuffer = "";
}

bool looksLikePrompt(const String& s) {
    if (!s.length()) return false;
    char last = s[s.length() - 1];
    return last == ':' || last == '>' || last == '?' || last == '#';
}

void onNotify(NimBLERemoteCharacteristic* /*chr*/, uint8_t* data, size_t len,
              bool /*isNotify*/) {
    for (size_t i = 0; i < len; ++i) {
        char c = static_cast<char>(data[i]);
        if (c == '\n') {
            emitBuffer();
        } else if (c != '\r') {
            g_rxBuffer += c;
            if (g_rxBuffer.length() > 512) emitBuffer(); // runaway guard
        }
    }
    // A chunk that ends on a prompt (no newline) is surfaced immediately.
    if (looksLikePrompt(g_rxBuffer)) emitBuffer();
}

// --- Scan results: collect every device so the user can pick the encoder ----
class ScanCallbacks : public NimBLEAdvertisedDeviceCallbacks {
    void onResult(NimBLEAdvertisedDevice* dev) override {
        String addr = dev->getAddress().toString().c_str();
        for (auto& f : g_found) {           // de-dupe by address, refresh rssi
            if (f.addr == addr) { f.rssi = dev->getRSSI(); f.dev = dev; return; }
        }
        auto& cfg = Config::get();
        String name = dev->haveName() ? String(dev->getName().c_str()) : String();
        // Flag devices advertising a known serial service (Nordic UART or the
        // Microchip/ISSC transparent UART) as a likely encoder.
        bool hasNus =
            dev->isAdvertisingService(NimBLEUUID(cfg.serviceUuid.c_str())) ||
            dev->isAdvertisingService(
                NimBLEUUID("49535343-fe7d-4ae5-8fa9-9fafd205e455"));
        g_found.push_back({dev, addr, name.length() ? name : addr,
                           dev->getRSSI(), hasNus, name.length() > 0});
        if (g_devicesCb) g_devicesCb();

        // If a device-name filter is configured and matches, auto-connect.
        if (cfg.deviceName.length() && name == cfg.deviceName) {
            NimBLEDevice::getScan()->stop();
            g_target = dev;
            g_doConnect = true;
        }
    }
};

// --- Client connection lifecycle -------------------------------------------
class ClientCallbacks : public NimBLEClientCallbacks {
    void onConnect(NimBLEClient* /*c*/) override {
        // Keep the peripheral's preferred connection parameters; renegotiating
        // here has caused some devices to drop the link immediately.
    }
    void onDisconnect(NimBLEClient* c) override {
        g_rxChar = nullptr;
        g_txChar = nullptr;
        g_peerName = "";
        setState(State::Disconnected);
    }
};

ScanCallbacks  g_scanCb;
ClientCallbacks g_clientCb;

bool connectToTarget() {
    auto& cfg = Config::get();
    setState(State::Connecting);

    if (!g_client) {
        g_client = NimBLEDevice::createClient();
        g_client->setClientCallbacks(&g_clientCb, /*deleteCB=*/false);
        g_client->setConnectTimeout(10);
    }

    if (!g_client->connect(g_target)) {
        SessionLog::info("connect failed");
        setState(State::Disconnected);
        return false;
    }

    g_rxChar = nullptr;
    g_txChar = nullptr;

    // Prefer the configured write characteristic if it exists.
    NimBLERemoteService* svc =
        g_client->getService(NimBLEUUID(cfg.serviceUuid.c_str()));
    if (svc) {
        NimBLERemoteCharacteristic* rx =
            svc->getCharacteristic(NimBLEUUID(cfg.rxCharUuid.c_str()));
        if (rx && (rx->canWrite() || rx->canWriteNoResponse())) g_rxChar = rx;
    }

    // Enumerate everything: pick a writable characteristic for sending, and
    // subscribe to EVERY notify/indicate characteristic so we capture the
    // encoder's replies regardless of which one it uses for output.
    int subscribed = 0;
    std::vector<NimBLERemoteService*>* services = g_client->getServices(true);
    if (services) {
        for (auto s : *services) {
            std::vector<NimBLERemoteCharacteristic*>* chars =
                s->getCharacteristics(true);
            if (!chars) continue;
            for (auto ch : *chars) {
                bool w = ch->canWrite() || ch->canWriteNoResponse();
                bool n = ch->canNotify();
                bool ind = ch->canIndicate();
                SessionLog::info(String("chr ") + (w ? "W" : "-") +
                                 (n ? "N" : (ind ? "I" : "-")) + " " +
                                 ch->getUUID().toString().c_str());
                if (!g_rxChar && w) g_rxChar = ch;
                if (n || ind) {
                    // subscribe(true)=notify, subscribe(false)=indicate.
                    if (ch->subscribe(n, onNotify)) {
                        if (!g_txChar) g_txChar = ch;
                        subscribed++;
                    }
                }
            }
        }
    }

    if (!g_rxChar) {
        SessionLog::info("no writable characteristic; disconnecting");
        g_client->disconnect();
        return false;
    }

    SessionLog::info(String("rx=") + g_rxChar->getUUID().toString().c_str());
    SessionLog::info(String("subscribed ") + subscribed + " notify char(s)");

    g_peerName = g_target->haveName()
                     ? String(g_target->getName().c_str())
                     : String(g_target->getAddress().toString().c_str());
    setState(State::Connected);
    return true;
}

} // namespace

void initRadio() {
    NimBLEDevice::init("SAXBLE");
    NimBLEDevice::setPower(ESP_PWR_LVL_P9);
    // Support "just works" pairing in case the encoder requires an encrypted
    // link before it exposes its service (harmless if it doesn't).
    NimBLEDevice::setSecurityAuth(/*bonding=*/true, /*mitm=*/false, /*sc=*/true);
    NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);
}

void begin() {
    initRadio();
    startScan();
}

void startScan() {
    g_doConnect = false;
    g_target = nullptr;
    g_found.clear();
    if (g_devicesCb) g_devicesCb();
    NimBLEScan* scan = NimBLEDevice::getScan();
    scan->setAdvertisedDeviceCallbacks(&g_scanCb, /*wantDuplicates=*/false);
    scan->setActiveScan(true);   // request scan responses (carry the name)
    scan->setInterval(60);        // near-continuous scanning so the list fills
    scan->setWindow(60);          // fast (window == interval = 100% duty)
    scan->clearResults();
    scan->start(0, nullptr, false); // 0 = scan until we stop it
    setState(State::Scanning);
}

void pause() {
    if (g_paused) return;
    g_paused = true;
    g_doConnect = false;
    NimBLEScan* scan = NimBLEDevice::getScan();
    if (scan && scan->isScanning()) scan->stop();
    if (g_client && g_client->isConnected()) g_client->disconnect();
    g_rxChar = nullptr;
    g_txChar = nullptr;
    // NOTE: we deliberately do NOT NimBLEDevice::deinit() here - doing so while
    // a connection is tearing down can deadlock the BLE host task and hang the
    // device. Stopping scanning and dropping the link is enough to keep Wi-Fi
    // responsive.
    setState(State::Idle);
}

void resume() {
    if (!g_paused) return;
    g_paused = false;
    startScan();
}

bool paused() { return g_paused; }

void loop() {
    if (g_paused) return;

    if (g_doConnect) {
        g_doConnect = false;
        if (!connectToTarget()) {
            // Back off briefly, then resume scanning.
            delay(200);
            startScan();
        }
    }

    // If we dropped the link, automatically look for the encoder again.
    if (g_state == State::Disconnected) {
        startScan();
    }
}

State  state()     { return g_state; }
bool   connected() { return g_state == State::Connected; }
String peerName()  { return g_peerName; }

String statusText() {
    switch (g_state) {
        case State::Idle:         return "Idle";
        case State::Scanning:     return "Scanning...";
        case State::Connecting:   return "Connecting...";
        case State::Connected:    return "Linked: " + g_peerName;
        case State::Disconnected: return "Disconnected";
    }
    return "?";
}

void disconnect() {
    if (g_client && g_client->isConnected()) g_client->disconnect();
}

int deviceCount() { return (int)g_found.size(); }

DeviceInfo deviceAt(int index) {
    if (index < 0 || index >= (int)g_found.size())
        return {String("?"), 0, false, false};
    const FoundDev& f = g_found[index];
    return {f.label, f.rssi, f.hasNus, f.named};
}

void connectIndex(int index) {
    if (index < 0 || index >= (int)g_found.size()) return;
    NimBLEDevice::getScan()->stop();
    g_target = g_found[index].dev;
    g_doConnect = true;
}

bool send(const String& line) {
    if (g_state != State::Connected || !g_rxChar) return false;
    String payload = line + Config::get().lineEnding;
    return g_rxChar->writeValue(reinterpret_cast<const uint8_t*>(payload.c_str()),
                                payload.length(),
                                Config::get().writeWithResponse);
}

void onLine(std::function<void(const String&)> cb)   { g_lineCb  = std::move(cb); }
void onStateChange(std::function<void(State)> cb)     { g_stateCb = std::move(cb); }
void onDevicesChanged(std::function<void()> cb)       { g_devicesCb = std::move(cb); }

} // namespace BleUart
