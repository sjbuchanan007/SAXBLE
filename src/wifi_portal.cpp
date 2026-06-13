#include "wifi_portal.h"
#include "config.h"
#include "session_log.h"
#include <WiFi.h>
#include <WebServer.h>
#include <SD.h>

namespace WifiPortal {
namespace {

WebServer g_server(80);
bool      g_active = false;
TaskHandle_t g_task = nullptr;

// The web server runs on its own task so handleClient() never blocks the main
// loop (that previously froze the keyboard, making the screen impossible to
// exit, and throttled transfers).
void serverTask(void*) {
    while (g_active) {
        g_server.handleClient();
        vTaskDelay(2 / portTICK_PERIOD_MS);
    }
    g_task = nullptr;
    vTaskDelete(nullptr);
}

constexpr const char* kLogDir = "/saxble";

// Return just the file name (basename) regardless of how the core reports it.
String baseName(const String& path) {
    int slash = path.lastIndexOf('/');
    return slash >= 0 ? path.substring(slash + 1) : path;
}

String htmlHeader(const String& title) {
    String h = F("<!doctype html><html><head><meta charset=utf-8>"
                 "<meta name=viewport content='width=device-width,initial-scale=1'>"
                 "<title>");
    h += title;
    h += F("</title><style>"
           "body{font-family:system-ui,sans-serif;margin:0;background:#111;color:#eee}"
           "header{background:#051f8c;padding:14px 18px;font-size:1.2em}"
           "main{padding:16px 18px}"
           "a{color:#6cf;text-decoration:none}"
           "table{width:100%;border-collapse:collapse}"
           "td,th{padding:8px 6px;border-bottom:1px solid #333;text-align:left}"
           ".btn{display:inline-block;background:#051f8c;color:#fff;padding:8px 14px;"
           "border-radius:6px;margin-bottom:14px}"
           ".muted{color:#999}"
           "</style></head><body><header>SAXBLE &mdash; ");
    h += title;
    h += F("</header><main>");
    return h;
}

const char* htmlFooter() { return "</main></body></html>"; }

String humanSize(size_t bytes) {
    if (bytes < 1024) return String(bytes) + " B";
    if (bytes < 1024 * 1024) return String(bytes / 1024.0, 1) + " KB";
    return String(bytes / (1024.0 * 1024.0), 1) + " MB";
}

// One table row for a file at relative path `rel` (e.g. "Ward10_3c2a/session_0001.txt").
String fileRow(const String& rel, size_t size) {
    return "<tr><td>" + rel + "</td><td>" + humanSize(size) +
           "</td><td><a href='/dl?f=" + rel + "'>download</a> &middot; "
           "<a href='/view?f=" + rel + "'>view</a></td></tr>";
}

void handleIndex() {
    String page = htmlHeader("Commissioning logs");
    page += F("<a class=btn href='/export'>Save current session to SD</a>");

    if (!SessionLog::mountSd()) {
        page += F("<p class=muted>No SD card detected. Insert a FAT32 card and "
                  "tap reload.</p>");
        page += htmlFooter();
        g_server.send(200, "text/html", page);
        return;
    }

    if (!SD.exists(kLogDir)) SD.mkdir(kLogDir);
    File root = SD.open(kLogDir);
    int count = 0;
    if (root && root.isDirectory()) {
        page += F("<table><tr><th>File</th><th>Size</th><th></th></tr>");
        // Top-level files plus one level of per-device folders.
        for (File e = root.openNextFile(); e; e = root.openNextFile()) {
            String ename = baseName(String(e.name()));
            if (e.isDirectory()) {
                for (File f = e.openNextFile(); f; f = e.openNextFile()) {
                    if (f.isDirectory()) continue;
                    page += fileRow(ename + "/" + baseName(String(f.name())),
                                    f.size());
                    count++;
                }
            } else {
                page += fileRow(ename, e.size());
                count++;
            }
        }
        page += F("</table>");
    }
    if (count == 0)
        page += F("<p class=muted>No logs yet. Run a session, then refresh.</p>");
    page += htmlFooter();
    g_server.send(200, "text/html", page);
}

// Resolve and open a requested file safely. `rel` may include one subfolder
// (e.g. "Ward10_3c2a/session_0001.txt"); reject any parent-directory tricks.
File openRequested(const String& rel) {
    if (rel.length() == 0 || rel.indexOf("..") >= 0) return File();
    String path = String(kLogDir) + "/" + rel;
    if (!SD.exists(path)) return File();
    File f = SD.open(path, FILE_READ);
    if (f && f.isDirectory()) { f.close(); return File(); }
    return f;
}

// Log files are small, so read the whole thing into RAM and send it in one
// response. This is far more reliable over Wi-Fi than chunked streamFile().
String readWhole(File& f) {
    String body;
    body.reserve(f.size() + 1);
    while (f.available()) body += (char)f.read();
    return body;
}

void handleDownload() {
    File f = openRequested(g_server.arg("f"));
    if (!f) { g_server.send(404, "text/plain", "not found"); return; }
    String name = baseName(String(f.name()));
    String body = readWhole(f);
    f.close();
    g_server.sendHeader("Content-Disposition", "attachment; filename=" + name);
    g_server.send(200, "application/octet-stream", body);
}

void handleView() {
    File f = openRequested(g_server.arg("f"));
    if (!f) { g_server.send(404, "text/plain", "not found"); return; }
    String body = readWhole(f);
    f.close();
    g_server.send(200, "text/plain", body);
}

void handleExport() {
    String err;
    String path = SessionLog::exportToSd(&err);
    String page = htmlHeader("Export");
    if (path.length()) {
        page += "<p>Saved <b>" + baseName(path) + "</b>.</p>";
    } else {
        page += "<p class=muted>Export failed: " + err + "</p>";
    }
    page += F("<a class=btn href='/'>Back to logs</a>");
    page += htmlFooter();
    g_server.send(200, "text/html", page);
}

} // namespace

bool start() {
    if (g_active) return true;
    auto& cfg = Config::get();

    SessionLog::mountSd();   // pick up a card inserted after boot

    WiFi.persistent(false);
    WiFi.mode(WIFI_AP);
    bool ok = WiFi.softAP(cfg.wifiSsid.c_str(),
                          cfg.wifiPassword.length() >= 8
                              ? cfg.wifiPassword.c_str()
                              : nullptr); // open AP if password too short
    if (!ok) {
        WiFi.mode(WIFI_OFF);
        return false;
    }

    g_server.on("/", handleIndex);
    g_server.on("/dl", handleDownload);
    g_server.on("/view", handleView);
    g_server.on("/export", handleExport);
    g_server.onNotFound([]() {
        String page = htmlHeader("Not found");
        page += F("<a class=btn href='/'>Back to logs</a>");
        page += htmlFooter();
        g_server.send(404, "text/html", page);
    });
    g_server.begin();

    g_active = true;
    // 8 KB stack: enough for handleClient + buffering a log file into a String.
    xTaskCreatePinnedToCore(serverTask, "saxweb", 8192, nullptr, 1, &g_task, 0);
    return true;
}

void stop() {
    if (!g_active) return;
    g_active = false;                       // signal the task to finish
    for (int i = 0; i < 50 && g_task; ++i) delay(10);  // wait up to 500ms
    if (g_task) { vTaskDelete(g_task); g_task = nullptr; }
    g_server.stop();
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_OFF);
}

bool active() { return g_active; }

void loop() {
    // The web server runs on its own task (serverTask); nothing to do here.
}

String ssid()     { return Config::get().wifiSsid; }
String password() { return Config::get().wifiPassword; }
String ip()       { return g_active ? WiFi.softAPIP().toString() : String("-"); }
int clientCount() { return g_active ? WiFi.softAPgetStationNum() : 0; }

} // namespace WifiPortal
