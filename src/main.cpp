// SAXBLE — SAX-D local-alarm setup tool for the M5Stack Cardputer ADV.
//
// Acts as a BLE central toward the SAX-D encoder (same role as the "BLE
// Terminal" phone app): connect, log in, then drive the encoder's command-line
// interface from on-device menus. The session is captured to microSD (one
// folder per device); pull the card to read the logs for a commissioning report.

#include <M5Cardputer.h>
#include <vector>
#include "config.h"
#include "ble_uart.h"
#include "session_log.h"
#include "ui.h"

namespace {

// Auto-login tries each saved password in turn (lastPassword first) until one
// works, so a unit on a different password still logs in.
std::vector<String> g_loginCandidates;
int                 g_loginIdx = 0;
String              g_pendingLoginPw;

// Auto-login is deferred out of the BLE notify callback into the main loop:
// sending the password from inside the callback (1ms after the prompt) didn't
// reliably go out, and the encoder also needs a moment after printing
// "Password:" before it accepts input.
volatile bool g_loginPending = false;
uint32_t      g_loginPromptMs = 0;
constexpr uint32_t kLoginSettleMs = 500;

void buildLoginCandidates() {
    g_loginCandidates.clear();
    g_loginIdx = 0;
    auto& cfg = Config::get();
    auto addUnique = [&](const String& p) {
        if (!p.length()) return;
        for (auto& e : g_loginCandidates) if (e == p) return;
        g_loginCandidates.push_back(p);
    };
    addUnique(cfg.lastPassword);               // last one used first
    for (auto& p : cfg.passwords) addUnique(p); // then the saved list
    // Always also try the well-known commissioning passwords (correctly cased,
    // so a unit on either one logs in without any typing).
    addUnique("studio3");
    addUnique("MMSmms659");
}

// Deferred "Y" answer to a destructive command's "Y or N" prompt.
volatile bool g_confirmPending = false;
uint32_t      g_confirmPromptMs = 0;
constexpr uint32_t kConfirmSettleMs = 300;

void handleBleState(BleUart::State s) {
    if (s == BleUart::State::Connected) {
        buildLoginCandidates();
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

    // Destructive commands (logclear/factory/reboot/...) prompt "Y or N". If we
    // just sent one (user confirmed on-device), answer Y (deferred to loop()).
    if ((lower.indexOf("y or n") >= 0 || lower.indexOf("y/n") >= 0) &&
        Ui::awaitEncoderConfirm()) {
        Ui::clearEncoderConfirm();
        g_confirmPending = true;
        g_confirmPromptMs = millis();
        return;
    }

    // Trigger the next auto-login attempt on the initial "Password:" prompt or
    // after an "Invalid Password" reply (the encoder re-asks each time).
    bool prompt  = (lower.indexOf("password") >= 0 && line.endsWith(":"));
    bool invalid = (lower.indexOf("invalid password") >= 0);
    if (!prompt && !invalid) return;

    // Seeing the prompt means we are NOT logged in.
    Ui::setLoggedIn(false);
    if (g_loginPending) return;   // a send is already queued; don't skip ahead

    auto& cfg = Config::get();
    if (cfg.autoLogin && !Ui::autoLoginSuppressed() &&
        g_loginIdx < (int)g_loginCandidates.size()) {
        // Defer the actual send to loop() (out of this BLE-callback context).
        g_pendingLoginPw = g_loginCandidates[g_loginIdx++];
        g_loginPending = true;
        g_loginPromptMs = millis();
    } else {
        // Auto-login off, suppressed, or all saved passwords tried — let the
        // user pick/type a password.
        Ui::promptLogin();
    }
}

// Called from loop(): send the deferred auto-login password once the encoder
// has had a moment to be ready for input.
void serviceAutoLogin() {
    if (!g_loginPending) return;
    if (millis() - g_loginPromptMs < kLoginSettleMs) return;
    g_loginPending = false;
    if (Ui::loggedIn() || !BleUart::connected() || !g_pendingLoginPw.length()) return;
    BleUart::send(g_pendingLoginPw);
    SessionLog::info("auto-login sent: " + g_pendingLoginPw);  // shown to verify
}

// Called from loop(): answer a destructive command's "Y or N" prompt with Y.
void serviceConfirm() {
    if (!g_confirmPending) return;
    if (millis() - g_confirmPromptMs < kConfirmSettleMs) return;
    g_confirmPending = false;
    if (!BleUart::connected()) return;
    BleUart::send("Y");
    SessionLog::info("confirm: sent Y");
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
    serviceConfirm();       // answers a destructive command's Y/N prompt
    delay(5);
}
