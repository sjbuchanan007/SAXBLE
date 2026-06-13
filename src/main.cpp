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
    if (s == BleUart::State::Connected) {
        g_loginAttempts = 0;
        // Group this session's logs per device and record it in the registry.
        SessionLog::setDevice(BleUart::peerName(), BleUart::peerAddress());
    }
    Ui::onBleState(s);
}

// Drive login off the encoder's actual prompt. The encoder sends "Password:"
// (no newline) after connecting; when we see it we send the saved password.
// On the success banner we mark ourselves logged in (handled in Ui::onRxLine).
void handleEncoderLine(const String& line) {
    String lower = line;
    lower.toLowerCase();
    // A login prompt looks like "Password:" — require both to avoid matching
    // command echoes (e.g. changing the password) that mention the word.
    if (lower.indexOf("password") < 0 || !line.endsWith(":")) return;

    // Seeing the password prompt means we are NOT logged in.
    Ui::setLoggedIn(false);

    auto& cfg = Config::get();
    if (cfg.autoLogin && !Ui::autoLoginSuppressed() && cfg.lastPassword.length() &&
        g_loginAttempts < kMaxLoginAttempts) {
        g_loginAttempts++;
        BleUart::send(cfg.lastPassword);
        SessionLog::info("auto-login sent (password hidden)");
    } else {
        // Auto-login off, suppressed (explicit logout), or exhausted — let the
        // user pick a password.
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

    // Register callbacks now; BLE itself is only started when the user picks
    // "Connect to Encoder" (and fully shut down again on disconnect), so BLE
    // and Wi-Fi are never powered at the same time.
    BleUart::onStateChange(handleBleState);
    BleUart::onLine([](const String& line) {
        Ui::onRxLine(line);
        handleEncoderLine(line);
    });
    BleUart::onDevicesChanged([]() { Ui::onDevicesChanged(); });
}

void loop() {
    M5Cardputer.update();
    // Handle the keyboard first so the UI (e.g. exiting the Wi-Fi screen) stays
    // responsive even when the web server is busy serving a client.
    Ui::loop();
    WifiPortal::loop();     // no-op unless the portal is active
    BleUart::loop();        // no-op while BLE is paused for Wi-Fi
    delay(5);
}
