#pragma once
#include <Arduino.h>

// USB Mass Storage export: presents the microSD card as a normal USB drive when
// the Cardputer is plugged into a computer. No radio, no web server - the host
// OS handles the FAT filesystem, so it's far more robust than the Wi-Fi route.
//
// Requires the native USB-OTG stack (ARDUINO_USB_MODE=0); see platformio.ini.

namespace UsbMsc {

// Initialise MSC over the SD card and start USB. Call once at startup, after
// the SD card has been mounted. Returns false if there's no usable card.
bool begin();

// Attach (true) or detach (false) the drive to the host. We keep it detached
// until the user opens the USB export screen, so the device "owns" the card the
// rest of the time.
void setActive(bool on);

bool active();      // drive currently presented to the host
bool available();   // MSC initialised (card was present at begin)

} // namespace UsbMsc
