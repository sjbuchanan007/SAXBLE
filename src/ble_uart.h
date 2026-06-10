#pragma once
#include <Arduino.h>
#include <functional>

// BLE "central" wrapper around the encoder's Nordic UART Service.
//
// Responsibilities:
//   * scan for and connect to the encoder (by name filter or by NUS service),
//   * subscribe to notifications (encoder -> us),
//   * write command lines (us -> encoder),
//   * surface received text line-by-line and report connection state.
//
// This is intentionally transport-only: it knows nothing about the SAX-D
// command set or the login flow. main.cpp wires those on top.

namespace BleUart {

enum class State : uint8_t {
    Idle,
    Scanning,
    Connecting,
    Connected,      // link up + NUS characteristics found + subscribed
    Disconnected,
};

void begin();
void loop();                  // call every iteration of the main loop

State  state();
bool   connected();
String statusText();          // short human-readable status for the UI
String peerName();            // name/address of the connected peer ("" if none)

void startScan();             // (re)start scanning for the encoder
void disconnect();

// Pause all BLE radio activity (disconnect + stop scanning) so Wi-Fi can use
// the radio, then resume afterwards. While paused, loop() does nothing.
void pause();
void resume();
bool paused();

// Send one line. The configured line ending is appended automatically.
// Returns false if not currently connected.
bool send(const String& line);

// Callbacks (set once during setup).
void onLine(std::function<void(const String&)> cb);        // a received text line
void onStateChange(std::function<void(State)> cb);

} // namespace BleUart
