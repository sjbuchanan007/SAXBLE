// SAXBLE — SAX-D local-alarm setup tool for the M5Stack Cardputer ADV.
//
// Acts as a BLE central toward the SAX-D encoder (same role as the "BLE
// Terminal" phone app): connect, log in, then drive the encoder's command-line
// interface from on-device menus. The session is captured for a commissioning
// report and can be exported to microSD.

#include <M5Cardputer.h>
#include "config.h"
#include "ble_uart.h"
#include "session_log.h"
#include "wifi_portal.h"
#include "ui.h"

namespace {

constexpr int kMaxLoginAttempts = 2;   // don't loop forever on a bad password
int g_loginAttempts = 0;

void handleBleState(BleUart::State s) {
    if (s == BleUart::State::Connected) g_loginAttempts = 0;
    Ui::onBleState(s);
}

// Drive login off the encoder's actual prompt. The encoder sends "Password:"
// (no newline) after connecting; when we see it we send the saved password.
// On the success banner we mark ourselves logged in (handled in Ui::onRxLine).
void handleEncoderLine(const String& line) {
    if (Ui::loggedIn()) return;

    String lower = line;
    lower.toLowerCase();
    if (lower.indexOf("password") < 0) return;   // not a login prompt

    auto& cfg = Config::get();
    if (cfg.autoLogin && cfg.lastPassword.length() &&
        g_loginAttempts < kMaxLoginAttempts) {
        g_loginAttempts++;
        BleUart::send(cfg.lastPassword);
        SessionLog::info("auto-login sent (password hidden)");
    } else {
        // Auto-login is off or exhausted — let the user pick a password.
        Ui::promptLogin();
    }
}

} // namespace

void setup() {
    auto cfg = M5.config();
    M5Cardputer.begin(cfg, /*enableKeyboard=*/true);
    M5Cardputer.Display.setRotation(1);

    Config::load();
    SessionLog::begin();
    SessionLog::info("SAXBLE started");

    Ui::begin();

    BleUart::onStateChange(handleBleState);
    BleUart::onLine([](const String& line) {
        Ui::onRxLine(line);
        handleEncoderLine(line);
    });
    BleUart::onDevicesChanged([]() { Ui::onDevicesChanged(); });
    BleUart::begin();
}

void loop() {
    M5Cardputer.update();
    BleUart::loop();        // no-op while BLE is paused for Wi-Fi
    WifiPortal::loop();     // no-op unless the portal is active
    Ui::loop();
    delay(5);
}
