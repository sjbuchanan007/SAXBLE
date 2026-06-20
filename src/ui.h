#pragma once
#include <Arduino.h>
#include "ble_uart.h"

// On-device UI: menu navigation + parameter entry on the Cardputer ADV.
//
// Navigation keys (Cardputer convention):
//   ;  up      .  down      ,  left      /  right
//   ENTER select        `  back / cancel
// In text entry, type normally; ENTER confirms, ` cancels, DEL backspaces.

namespace Ui {

void begin();
void loop();   // call every main-loop iteration (after M5Cardputer.update())

// Hooks fed by main from the BLE layer.
void onRxLine(const String& line);
void onBleState(BleUart::State s);
void onDevicesChanged();   // the discovered-device list changed

void setLoggedIn(bool in);
bool loggedIn();
bool autoLoginSuppressed();   // true after an explicit Logout, until reconnect/login
void rememberLogin(const String& pw);  // password to save to the list if login succeeds

// A destructive command was just sent and is awaiting the encoder's Y/N prompt.
bool awaitEncoderConfirm();
void clearEncoderConfirm();

// Show the password screen (e.g. when the encoder prompts and auto-login is off
// or has failed).
void promptLogin();

// Transient on-screen message (auto-clears).
void notify(const String& msg);

} // namespace Ui
