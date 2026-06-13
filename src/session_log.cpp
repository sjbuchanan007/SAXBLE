#include "session_log.h"
#include <SPI.h>
#include <SD.h>

namespace SessionLog {
namespace {

std::deque<Entry> g_entries;
constexpr size_t  kMaxEntries = 400;   // on-device scrollback cap
bool              g_sdOk = false;

// Connected device (set via setDevice); session logs go into g_devFolder.
String g_devName;
String g_devAddr;
String g_devFolder;

// microSD pins for the Cardputer / Stamp S3.
// NOTE: verify against the Cardputer ADV schematic — if export fails, these are
// the first thing to check. They are isolated here so there's one place to fix.
constexpr int kSdSck  = 40;
constexpr int kSdMiso = 39;
constexpr int kSdMosi = 14;
constexpr int kSdCs   = 12;

// Keep only filesystem-friendly characters; cap the length.
String sanitize(const String& s) {
    String o;
    for (size_t i = 0; i < s.length() && o.length() < 16; ++i) {
        char c = s[i];
        bool ok = (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') ||
                  (c >= 'a' && c <= 'z');
        o += ok ? c : '_';
    }
    return o.length() ? o : String("dev");
}

// Address hex with the colons removed.
String hexOnly(const String& addr) {
    String h;
    for (size_t i = 0; i < addr.length(); ++i)
        if (addr[i] != ':') h += addr[i];
    return h;
}

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

// Add this device to /saxble/devices.txt the first time we see its address.
void updateRegistry() {
    if (!g_sdOk) return;
    String path = String(logDir()) + "/devices.txt";
    File rf = SD.open(path, FILE_READ);
    if (rf) {
        String all = rf.readString();
        rf.close();
        if (g_devAddr.length() && all.indexOf(g_devAddr) >= 0) return; // known
    }
    File wf = SD.open(path, FILE_APPEND);
    if (wf) {
        wf.println(g_devAddr + "\t" +
                   (g_devName.length() ? g_devName : String("(no name)")));
        wf.close();
    }
}

void setDevice(const String& name, const String& address) {
    g_devName = name;
    g_devAddr = address;
    if (name.length())
        g_devFolder = String(logDir()) + "/" + sanitize(name) + "_" +
                      hexOnly(address).substring(
                          hexOnly(address).length() >= 4
                              ? hexOnly(address).length() - 4 : 0);
    else
        g_devFolder = String(logDir()) + "/dev_" + hexOnly(address);

    if (mountSd()) {
        if (!SD.exists(logDir())) SD.mkdir(logDir());
        if (!SD.exists(g_devFolder)) SD.mkdir(g_devFolder);
        updateRegistry();
    }
}

String exportToSd(String* outError) {
    if (!g_sdOk) {
        // Try once more — a card may have been inserted after boot.
        g_sdOk = SD.begin(kSdCs, SPI);
        if (!g_sdOk) {
            if (outError) *outError = "No SD card";
            return "";
        }
    }

    if (!SD.exists(logDir())) SD.mkdir(logDir());
    String folder = g_devFolder.length() ? g_devFolder : String(logDir());
    if (!SD.exists(folder)) SD.mkdir(folder);

    // Sessions are numbered; uptime alone isn't a wall clock, so we just need a
    // unique, sortable filename per export.
    String path;
    for (int i = 1; i < 10000; ++i) {
        char n[24];
        snprintf(n, sizeof(n), "/session_%04d.txt", i);
        path = folder + n;
        if (!SD.exists(path)) break;
    }

    File f = SD.open(path.c_str(), FILE_WRITE);
    if (!f) {
        if (outError) *outError = "Open failed";
        return "";
    }

    f.println("SAX-D commissioning session log");
    f.println("Device:  " + (g_devName.length() ? g_devName : String("(unnamed)")));
    f.println("Address: " + (g_devAddr.length() ? g_devAddr : String("(unknown)")));
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
