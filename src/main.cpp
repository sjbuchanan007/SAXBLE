// SAXBLE — SAX-D local-alarm setup tool for the M5Stack Cardputer ADV.
//
// Acts as a BLE central toward the SAX-D encoder (same role as the "BLE
// Terminal" phone app): connect, log in, then drive the encoder's command-line
// interface from on-device menus. The session is captured to microSD (one
// folder per device); pull the card to read the logs for a commissioning report.

#include <M5Cardputer.h>
#include "config.h"
#include "ble_uart.h"
#include "session_log.h"
#include "ui.h"

namespace {

constexpr int kMaxLoginAttempts = 2;   // don't loop forever on a bad password
int g_loginAttempts = 0;

// Auto-login is deferred out of the BLE notify callback into the main loop:
// sending the password from inside the callback (1ms after the prompt) didn't
// reliably go out, and the encoder also needs a moment after printing
// "Password:" before it accepts input.
volatile bool g_loginPending = false;
uint32_t      g_loginPromptMs = 0;
constexpr uint32_t kLoginSettleMs = 500;

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
        // Defer the actual send to loop() (out of this BLE-callback context).
        g_loginPending = true;
        g_loginPromptMs = millis();
    } else {
        // Auto-login off, suppressed (explicit logout), or exhausted — let the
        // user pick a password.
        Ui::promptLogin();
    }
}

// Called from loop(): send the deferred auto-login password once the encoder
// has had a moment to be ready for input.
void serviceAutoLogin() {
    if (!g_loginPending) return;
    if (millis() - g_loginPromptMs < kLoginSettleMs) return;
    g_loginPending = false;
    if (Ui::loggedIn() || !BleUart::connected()) return;
    g_loginAttempts++;
    BleUart::send(Config::get().lastPassword);
    SessionLog::info("auto-login sent (password hidden)");
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
    BleUart::begin();   // start scanning for the encoder right away
}

void loop() {
    M5Cardputer.update();
    Ui::loop();
    BleUart::loop();
    serviceAutoLogin();     // sends the deferred password after the settle delay
    delay(5);
}
