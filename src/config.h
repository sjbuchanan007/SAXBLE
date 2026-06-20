#pragma once
#include <Arduino.h>
#include <vector>

// Persistent application configuration.
//
// Defaults target the Nordic UART Service (NUS) that "BLE Terminal"-style apps
// use, because the SAX-D encoder is reached the same way. Everything here is
// overridable on-device (Settings screen) and stored in NVS so it survives a
// reboot.

struct AppConfig {
    // --- BLE target / transport ---------------------------------------------
    String deviceName;        // advertised-name filter; "" = match by service UUID
    String serviceUuid;       // NUS service
    String rxCharUuid;        // characteristic we WRITE commands to (peer's RX)
    String txCharUuid;        // characteristic we get NOTIFY responses on (peer's TX)
    String lineEnding;        // appended to every command/line sent (e.g. "\r\n")
    bool   writeWithResponse; // false => Write Without Response (matches the app notes)

    // --- Login --------------------------------------------------------------
    bool                 autoLogin;     // send a password automatically on connect
    String               lastPassword;  // password used last (pre-selected next time)
    std::vector<String>  passwords;     // saved "popular passwords" to choose from
    String               loginSuccessMarker; // text in a reply that means we're in

    // --- Wi-Fi log export hotspot -------------------------------------------
    String wifiSsid;        // SoftAP name the Cardputer broadcasts
    String wifiPassword;    // SoftAP password (>= 8 chars for WPA2)
};

namespace Config {
    AppConfig& get();

    void load();            // populate from NVS, falling back to defaults
    void save();            // persist current values to NVS
    void resetDefaults();   // restore compiled-in defaults (and persist)

    // Password helpers (kept de-duplicated, order preserved).
    void addPassword(const String& pw);
    void removePassword(size_t index);
}
