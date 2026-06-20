#include "session_log.h"
#include <SPI.h>
#include <SD.h>
#include <ctime>

// time(nullptr) returns a value past this only once the clock has been set.
static constexpr time_t kClockSetThreshold = 1700000000;  // ~2023-11
static bool clockIsSet() { return time(nullptr) > kClockSetThreshold; }

namespace SessionLog {
namespace {

std::deque<Entry> g_entries;
constexpr size_t  kMaxEntries = 400;   // on-device scrollback cap
bool              g_sdOk = false;

// Connected device (set via setDevice); session logs go into g_devFolder.
String g_devName;
String g_devAddr;
String g_devFolder;

// Auto-save: a session file opened on connect, appended to live.
File   g_file;
bool   g_fileOpen = false;
String g_sessionPath;

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

// Timestamp for an entry: real date/time if the clock was set when it was
// logged, otherwise H:MM:SS.mmm relative to power-on.
String stamp(const Entry& e) {
    if (e.wall > kClockSetThreshold) {
        struct tm tmv;
        localtime_r(&e.wall, &tmv);
        char buf[24];
        snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d",
                 tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday,
                 tmv.tm_hour, tmv.tm_min, tmv.tm_sec);
        return String(buf);
    }
    uint32_t s = e.ms / 1000;
    char buf[16];
    snprintf(buf, sizeof(buf), "%lu:%02lu:%02lu.%03lu",
             (unsigned long)(s / 3600),
             (unsigned long)((s / 60) % 60),
             (unsigned long)(s % 60),
             (unsigned long)(e.ms % 1000));
    return String(buf);
}

void writeHeader(File& f) {
    f.println("SAX-D commissioning session log");
    f.println("Device:  " + (g_devName.length() ? g_devName : String("(unnamed)")));
    f.println("Address: " + (g_devAddr.length() ? g_devAddr : String("(unknown)")));
    if (clockIsSet()) {
        Entry now{millis(), time(nullptr), Dir::Info, ""};
        f.println("Started: " + stamp(now));
    } else {
        f.println("Timestamps are relative to device power-on (H:MM:SS.mmm).");
        f.println("(Set the clock in Settings for real date/time.)");
    }
    f.println("Legend: >> sent   << received   -- note");
    f.println("----------------------------------------");
}

void writeEntryLine(File& f, const Entry& e) {
    f.printf("%-20s %s %s\n", stamp(e).c_str(), dirTag(e.dir), e.text.c_str());
}

// Open a fresh, numbered session file in the current device folder and write
// the header. Called on each connect (setDevice).
void startSessionFile() {
    if (g_fileOpen) { g_file.flush(); g_file.close(); g_fileOpen = false; }
    g_sessionPath = "";
    if (!mountSd()) return;

    if (!SD.exists(logDir())) SD.mkdir(logDir());
    String folder = g_devFolder.length() ? g_devFolder : String(logDir());
    if (!SD.exists(folder)) SD.mkdir(folder);

    String path;
    for (int i = 1; i < 10000; ++i) {
        char n[24];
        snprintf(n, sizeof(n), "/session_%04d.txt", i);
        path = folder + n;
        if (!SD.exists(path)) break;
    }
    g_file = SD.open(path.c_str(), FILE_WRITE);
    if (!g_file) return;
    g_fileOpen = true;
    g_sessionPath = path;
    writeHeader(g_file);
    g_file.flush();
}

} // namespace

void begin() {
    SPI.begin(kSdSck, kSdMiso, kSdMosi, kSdCs);
    // max_files=10 so listing folders + opening a file for download doesn't
    // exhaust the FATFS file handles.
    g_sdOk = SD.begin(kSdCs, SPI, 4000000, "/sd", 10);
}

bool mountSd() {
    if (g_sdOk) return true;
    g_sdOk = SD.begin(kSdCs, SPI, 4000000, "/sd", 10);
    return g_sdOk;
}

const char* logDir() { return "/saxble"; }

void add(Dir dir, const String& text) {
    time_t wall = clockIsSet() ? time(nullptr) : 0;
    g_entries.push_back({millis(), wall, dir, text});
    while (g_entries.size() > kMaxEntries) g_entries.pop_front();
    // Append to the auto-saved session file and flush so it's crash-safe.
    if (g_fileOpen) {
        writeEntryLine(g_file, g_entries.back());
        g_file.flush();
    }
}

const std::deque<Entry>& entries() { return g_entries; }
size_t size()                      { return g_entries.size(); }
void clear()                       { g_entries.clear(); }
bool sdAvailable()                 { return g_sdOk; }

String sessionFile() {
    if (!g_fileOpen || !g_sessionPath.length()) return "";
    int slash = g_sessionPath.lastIndexOf('/');
    return slash >= 0 ? g_sessionPath.substring(slash + 1) : g_sessionPath;
}

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
    startSessionFile();   // begin auto-saving this connection's session
}

String exportToSd(String* outError) {
    if (!g_sdOk) {
        // Try once more — a card may have been inserted after boot.
        g_sdOk = SD.begin(kSdCs, SPI, 4000000, "/sd", 10);
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
    if (clockIsSet()) {
        Entry now{millis(), time(nullptr), Dir::Info, ""};
        f.println("Exported: " + stamp(now));
    } else {
        f.println("Timestamps are relative to device power-on (H:MM:SS.mmm).");
        f.println("(Set the clock in Settings for real date/time.)");
    }
    f.println("Legend: >> sent   << received   -- note");
    f.println("----------------------------------------");
    for (const auto& e : g_entries) {
        f.printf("%-20s %s %s\n", stamp(e).c_str(), dirTag(e.dir),
                 e.text.c_str());
    }
    f.flush();
    f.close();
    return String(path);
}

} // namespace SessionLog
