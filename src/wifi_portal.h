#pragma once
#include <Arduino.h>

// Wi-Fi "log export" hotspot.
//
// The Cardputer brings up its own Wi-Fi access point and a tiny web server that
// lists the commissioning logs on the microSD card and lets a phone/laptop
// download them in a browser — no network or cable required.
//
// Wi-Fi and BLE share the single radio, so the caller should pause BLE
// (BleUart::pause()) before starting the portal and resume it after stopping.

namespace WifiPortal {

bool start();        // bring up the SoftAP + web server; false on failure
void stop();         // tear it all down
bool active();
void loop();         // service the web server (call only while active())

String ssid();
String password();
String ip();         // AP IP address as text (e.g. "192.168.4.1")
int    clientCount(); // number of connected stations

} // namespace WifiPortal
