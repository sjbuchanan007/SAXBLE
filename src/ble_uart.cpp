#include "ble_uart.h"
#include "config.h"
#include <NimBLEDevice.h>
#include <utility>

namespace BleUart {
namespace {

State g_state = State::Idle;
NimBLEClient*               g_client    = nullptr;
NimBLERemoteCharacteristic* g_rxChar    = nullptr; // we write here
NimBLERemoteCharacteristic* g_txChar    = nullptr; // we get notifications here
NimBLEAdvertisedDevice*     g_target    = nullptr; // found, awaiting connect
volatile bool               g_doConnect = false;
String                      g_peerName;
String                      g_rxBuffer;            // accumulates partial lines

std::function<void(const String&)> g_lineCb;
std::function<void(State)>          g_stateCb;

void setState(State s) {
    if (s == g_state) return;
    g_state = s;
    if (g_stateCb) g_stateCb(s);
}

// Does this advertisement look like our encoder?
bool matchesTarget(const NimBLEAdvertisedDevice* dev) {
    auto& cfg = Config::get();
    if (cfg.deviceName.length()) {
        return dev->haveName() &&
               String(dev->getName().c_str()) == cfg.deviceName;
    }
    // No name configured: match anything advertising the NUS service.
    return dev->isAdvertisingService(NimBLEUUID(cfg.serviceUuid.c_str()));
}

// --- Notifications: split the stream into text lines ------------------------
void onNotify(NimBLERemoteCharacteristic* /*chr*/, uint8_t* data, size_t len,
              bool /*isNotify*/) {
    for (size_t i = 0; i < len; ++i) {
        char c = static_cast<char>(data[i]);
        if (c == '\n') {
            g_rxBuffer.trim();
            if (g_rxBuffer.length() && g_lineCb) g_lineCb(g_rxBuffer);
            g_rxBuffer = "";
        } else if (c != '\r') {
            g_rxBuffer += c;
            // Guard against a peer that never sends newlines.
            if (g_rxBuffer.length() > 512) {
                if (g_lineCb) g_lineCb(g_rxBuffer);
                g_rxBuffer = "";
            }
        }
    }
}

// --- Scan results -----------------------------------------------------------
class ScanCallbacks : public NimBLEAdvertisedDeviceCallbacks {
    void onResult(NimBLEAdvertisedDevice* dev) override {
        if (!matchesTarget(dev)) return;
        NimBLEDevice::getScan()->stop();
        g_target = dev;          // owned by scan results until the scan is cleared
        g_doConnect = true;
    }
};

// --- Client connection lifecycle -------------------------------------------
class ClientCallbacks : public NimBLEClientCallbacks {
    void onConnect(NimBLEClient* c) override {
        c->updateConnParams(12, 12, 0, 200);
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
        setState(State::Disconnected);
        return false;
    }

    NimBLERemoteService* svc =
        g_client->getService(NimBLEUUID(cfg.serviceUuid.c_str()));
    if (!svc) {
        g_client->disconnect();
        return false;
    }

    g_rxChar = svc->getCharacteristic(NimBLEUUID(cfg.rxCharUuid.c_str()));
    g_txChar = svc->getCharacteristic(NimBLEUUID(cfg.txCharUuid.c_str()));
    if (!g_rxChar || !g_txChar) {
        g_client->disconnect();
        return false;
    }

    if (g_txChar->canNotify()) {
        g_txChar->subscribe(true, onNotify);
    }

    g_peerName = g_target->haveName()
                     ? String(g_target->getName().c_str())
                     : String(g_target->getAddress().toString().c_str());
    setState(State::Connected);
    return true;
}

} // namespace

void begin() {
    auto& cfg = Config::get();
    NimBLEDevice::init("SAXBLE");
    NimBLEDevice::setPower(ESP_PWR_LVL_P9);
    (void)cfg;
    startScan();
}

void startScan() {
    g_doConnect = false;
    g_target = nullptr;
    NimBLEScan* scan = NimBLEDevice::getScan();
    scan->setAdvertisedDeviceCallbacks(&g_scanCb, /*wantDuplicates=*/false);
    scan->setActiveScan(true);
    scan->setInterval(100);
    scan->setWindow(80);
    scan->clearResults();
    scan->start(0, nullptr, false); // 0 = scan until we stop it
    setState(State::Scanning);
}

void loop() {
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

bool send(const String& line) {
    if (g_state != State::Connected || !g_rxChar) return false;
    String payload = line + Config::get().lineEnding;
    return g_rxChar->writeValue(reinterpret_cast<const uint8_t*>(payload.c_str()),
                                payload.length(),
                                Config::get().writeWithResponse);
}

void onLine(std::function<void(const String&)> cb)   { g_lineCb  = std::move(cb); }
void onStateChange(std::function<void(State)> cb)     { g_stateCb = std::move(cb); }

} // namespace BleUart
