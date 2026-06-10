#include "config.h"
#include <Preferences.h>

namespace {
AppConfig g_cfg;
Preferences g_prefs;

constexpr const char* kNvsNamespace = "saxble";

// Nordic UART Service UUIDs — the de-facto standard for BLE serial bridges.
constexpr const char* kNusService = "6e400001-b5a3-f393-e0a9-e50e24dcca9e";
constexpr const char* kNusRx      = "6e400002-b5a3-f393-e0a9-e50e24dcca9e"; // write
constexpr const char* kNusTx      = "6e400003-b5a3-f393-e0a9-e50e24dcca9e"; // notify

// Passwords are stored as a single newline-separated blob in NVS.
String joinPasswords(const std::vector<String>& pws) {
    String out;
    for (size_t i = 0; i < pws.size(); ++i) {
        if (i) out += '\n';
        out += pws[i];
    }
    return out;
}

std::vector<String> splitPasswords(const String& blob) {
    std::vector<String> out;
    int start = 0;
    while (start <= blob.length()) {
        int nl = blob.indexOf('\n', start);
        if (nl < 0) nl = blob.length();
        String item = blob.substring(start, nl);
        item.trim();
        if (item.length()) out.push_back(item);
        start = nl + 1;
    }
    return out;
}

void applyDefaults() {
    g_cfg.deviceName        = "";            // match by service if name unknown
    g_cfg.serviceUuid       = kNusService;
    g_cfg.rxCharUuid        = kNusRx;
    g_cfg.txCharUuid        = kNusTx;
    g_cfg.lineEnding        = "\r\n";
    g_cfg.writeWithResponse = false;          // "Write Without Response"
    g_cfg.autoLogin         = true;
    g_cfg.lastPassword      = "studio3";
    // Popular/known passwords. studio3 is the documented default.
    g_cfg.passwords         = {"studio3"};
    g_cfg.loginSuccessMarker = "Welcome to Shire SAX Command Line Interface";
}
} // namespace

namespace Config {

AppConfig& get() { return g_cfg; }

void load() {
    applyDefaults();
    g_prefs.begin(kNvsNamespace, /*readOnly=*/true);

    g_cfg.deviceName  = g_prefs.getString("dev",  g_cfg.deviceName);
    g_cfg.serviceUuid = g_prefs.getString("svc",  g_cfg.serviceUuid);
    g_cfg.rxCharUuid  = g_prefs.getString("rx",   g_cfg.rxCharUuid);
    g_cfg.txCharUuid  = g_prefs.getString("tx",   g_cfg.txCharUuid);
    g_cfg.lineEnding  = g_prefs.getString("eol",  g_cfg.lineEnding);
    g_cfg.writeWithResponse = g_prefs.getBool("wwr", g_cfg.writeWithResponse);
    g_cfg.autoLogin   = g_prefs.getBool("auto",  g_cfg.autoLogin);
    g_cfg.lastPassword = g_prefs.getString("lpw", g_cfg.lastPassword);
    g_cfg.loginSuccessMarker =
        g_prefs.getString("mark", g_cfg.loginSuccessMarker);

    String pwBlob = g_prefs.getString("pws", "");
    if (pwBlob.length()) g_cfg.passwords = splitPasswords(pwBlob);

    g_prefs.end();
}

void save() {
    g_prefs.begin(kNvsNamespace, /*readOnly=*/false);
    g_prefs.putString("dev",  g_cfg.deviceName);
    g_prefs.putString("svc",  g_cfg.serviceUuid);
    g_prefs.putString("rx",   g_cfg.rxCharUuid);
    g_prefs.putString("tx",   g_cfg.txCharUuid);
    g_prefs.putString("eol",  g_cfg.lineEnding);
    g_prefs.putBool("wwr",    g_cfg.writeWithResponse);
    g_prefs.putBool("auto",   g_cfg.autoLogin);
    g_prefs.putString("lpw",  g_cfg.lastPassword);
    g_prefs.putString("mark", g_cfg.loginSuccessMarker);
    g_prefs.putString("pws",  joinPasswords(g_cfg.passwords));
    g_prefs.end();
}

void resetDefaults() {
    applyDefaults();
    save();
}

void addPassword(const String& pw) {
    String v = pw;
    v.trim();
    if (!v.length()) return;
    for (auto& existing : g_cfg.passwords) {
        if (existing == v) return; // de-dupe
    }
    g_cfg.passwords.push_back(v);
    save();
}

void removePassword(size_t index) {
    if (index >= g_cfg.passwords.size()) return;
    g_cfg.passwords.erase(g_cfg.passwords.begin() + index);
    save();
}

} // namespace Config
