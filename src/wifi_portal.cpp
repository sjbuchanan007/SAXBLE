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

void handleIndex() {
    String page = htmlHeader("Commissioning logs");
    page += F("<a class=btn href='/export'>Save current session to SD</a>");

    if (!SD.exists(kLogDir)) SD.mkdir(kLogDir);
    File dir = SD.open(kLogDir);
    if (!dir || !dir.isDirectory()) {
        page += F("<p class=muted>No log folder on the SD card yet.</p>");
    } else {
        page += F("<table><tr><th>File</th><th>Size</th><th></th></tr>");
        int count = 0;
        for (File f = dir.openNextFile(); f; f = dir.openNextFile()) {
            if (f.isDirectory()) continue;
            String name = baseName(String(f.name()));
            page += "<tr><td>" + name + "</td><td>" + humanSize(f.size()) +
                    "</td><td><a href='/dl?f=" + name + "'>download</a> &middot; "
                    "<a href='/view?f=" + name + "'>view</a></td></tr>";
            count++;
        }
        page += F("</table>");
        if (count == 0)
            page += F("<p class=muted>No logs yet. Run a session, then refresh.</p>");
    }
    page += htmlFooter();
    g_server.send(200, "text/html", page);
}

// Resolve and open a requested file safely (no path traversal).
File openRequested(const String& fname) {
    String name = baseName(fname);          // strip any path the client sent
    if (name.length() == 0) return File();
    String path = String(kLogDir) + "/" + name;
    if (!SD.exists(path)) return File();
    return SD.open(path, FILE_READ);
}

void handleDownload() {
    File f = openRequested(g_server.arg("f"));
    if (!f) { g_server.send(404, "text/plain", "not found"); return; }
    g_server.sendHeader("Content-Disposition",
                        "attachment; filename=" + baseName(String(f.name())));
    g_server.streamFile(f, "application/octet-stream");
    f.close();
}

void handleView() {
    File f = openRequested(g_server.arg("f"));
    if (!f) { g_server.send(404, "text/plain", "not found"); return; }
    g_server.streamFile(f, "text/plain");
    f.close();
}

void handleExport() {
    String err;
    String path = SessionLog::exportToSd(&err);
    if (path.length()) {
        g_server.sendHeader("Location", "/");
        g_server.send(303, "text/plain", "");
    } else {
        g_server.send(500, "text/plain", "Export failed: " + err);
    }
}

} // namespace

bool start() {
    if (g_active) return true;
    auto& cfg = Config::get();

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
        g_server.sendHeader("Location", "/");
        g_server.send(303, "text/plain", "");
    });
    g_server.begin();

    g_active = true;
    return true;
}

void stop() {
    if (!g_active) return;
    g_server.stop();
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_OFF);
    g_active = false;
}

bool active() { return g_active; }

void loop() {
    if (g_active) g_server.handleClient();
}

String ssid()     { return Config::get().wifiSsid; }
String password() { return Config::get().wifiPassword; }
String ip()       { return g_active ? WiFi.softAPIP().toString() : String("-"); }
int clientCount() { return g_active ? WiFi.softAPgetStationNum() : 0; }

} // namespace WifiPortal
