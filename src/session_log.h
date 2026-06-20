#pragma once
#include <Arduino.h>
#include <ctime>
#include <deque>

// Captures the terminal session (commands we send + replies we receive) so it
// can be reviewed on-screen and exported to microSD for a commissioning report.
//
// The Cardputer ADV has a microSD slot; export writes a timestamped text file
// you can pull off the card and drop into the report.

namespace SessionLog {

enum class Dir : uint8_t { Tx, Rx, Info };

struct Entry {
    uint32_t ms;    // millis() when logged
    time_t   wall;  // wall-clock time when logged (0 if the clock isn't set)
    Dir      dir;
    String   text;
};

void begin();

// Record the connected device. Session logs are then grouped into a per-device
// folder on the SD card, and the device is added to the on-card registry
// (/saxble/devices.txt) the first time it's seen.
void setDevice(const String& name, const String& address);

void add(Dir dir, const String& text);
inline void tx(const String& s)   { add(Dir::Tx, s); }
inline void rx(const String& s)   { add(Dir::Rx, s); }
inline void info(const String& s) { add(Dir::Info, s); }

const std::deque<Entry>& entries();
size_t size();
void clear();

// True if a microSD card was detected at begin().
bool sdAvailable();

// (Re)mount the microSD card, e.g. after inserting one post-boot. Returns true
// if the card is now mounted.
bool mountSd();

// Folder where logs are written (on the SD card).
const char* logDir();

// Write the whole session to microSD. Returns the path written, or "" on
// failure. `outError` (optional) receives a human-readable reason on failure.
String exportToSd(String* outError = nullptr);

} // namespace SessionLog
