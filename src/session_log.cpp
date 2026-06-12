#include "session_log.h"
#include <SPI.h>
#include <SD.h>

namespace SessionLog {
namespace {

std::deque<Entry> g_entries;
constexpr size_t  kMaxEntries = 400;   // on-device scrollback cap
bool              g_sdOk = false;

// microSD pins for the Cardputer / Stamp S3.
// NOTE: verify against the Cardputer ADV schematic — if export fails, these are
// the first thing to check. They are isolated here so there's one place to fix.
constexpr int kSdSck  = 40;
constexpr int kSdMiso = 39;
constexpr int kSdMosi = 14;
constexpr int kSdCs   = 12;

const char* dirTag(Dir d) {
    switch (d) {
        case Dir::Tx:   return ">>";  // sent to encoder
        case Dir::Rx:   return "<<";  // received from encoder
        case Dir::Info: return "--";  // local note
    }
    return "??";
}

// Format a millis() value as H:MM:SS.mmm relative to power-on.
String stamp(uint32_t ms) {
    uint32_t s = ms / 1000;
    char buf[16];
    snprintf(buf, sizeof(buf), "%lu:%02lu:%02lu.%03lu",
             (unsigned long)(s / 3600),
             (unsigned long)((s / 60) % 60),
             (unsigned long)(s % 60),
             (unsigned long)(ms % 1000));
    return String(buf);
}

} // namespace

void begin() {
    SPI.begin(kSdSck, kSdMiso, kSdMosi, kSdCs);
    g_sdOk = SD.begin(kSdCs, SPI);
}

bool mountSd() {
    if (g_sdOk) return true;
    g_sdOk = SD.begin(kSdCs, SPI);
    return g_sdOk;
}

const char* logDir() { return "/saxble"; }

void add(Dir dir, const String& text) {
    g_entries.push_back({millis(), dir, text});
    while (g_entries.size() > kMaxEntries) g_entries.pop_front();
}

const std::deque<Entry>& entries() { return g_entries; }
size_t size()                      { return g_entries.size(); }
void clear()                       { g_entries.clear(); }
bool sdAvailable()                 { return g_sdOk; }

String exportToSd(String* outError) {
    if (!g_sdOk) {
        // Try once more — a card may have been inserted after boot.
        g_sdOk = SD.begin(kSdCs, SPI);
        if (!g_sdOk) {
            if (outError) *outError = "No SD card";
            return "";
        }
    }

    if (!SD.exists("/saxble")) SD.mkdir("/saxble");

    // Sessions are numbered; uptime alone isn't a wall clock, so we just need a
    // unique, sortable filename per export.
    char path[40];
    for (int i = 1; i < 10000; ++i) {
        snprintf(path, sizeof(path), "/saxble/session_%04d.txt", i);
        if (!SD.exists(path)) break;
    }

    File f = SD.open(path, FILE_WRITE);
    if (!f) {
        if (outError) *outError = "Open failed";
        return "";
    }

    f.println("SAX-D commissioning session log");
    f.println("Timestamps are relative to device power-on (H:MM:SS.mmm).");
    f.println("Legend: >> sent   << received   -- note");
    f.println("----------------------------------------");
    for (const auto& e : g_entries) {
        f.printf("%-12s %s %s\n", stamp(e.ms).c_str(), dirTag(e.dir),
                 e.text.c_str());
    }
    f.flush();
    f.close();
    return String(path);
}

} // namespace SessionLog
