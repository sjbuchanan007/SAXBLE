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

// On connect, optionally send the saved password so the CLI logs us straight in.
void handleBleState(BleUart::State s) {
    Ui::onBleState(s);
    if (s == BleUart::State::Connected) {
        auto& cfg = Config::get();
        if (cfg.autoLogin && cfg.lastPassword.length()) {
            // Small settle delay so the encoder has printed its login prompt.
            delay(300);
            if (BleUart::send(cfg.lastPassword)) {
                SessionLog::info("auto-login sent (password hidden)");
            }
        }
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
    BleUart::onLine([](const String& line) { Ui::onRxLine(line); });
    BleUart::begin();
}

void loop() {
    M5Cardputer.update();
    BleUart::loop();        // no-op while BLE is paused for Wi-Fi
    WifiPortal::loop();     // no-op unless the portal is active
    Ui::loop();
    delay(5);
}
