#include "ui.h"
#include "commands.h"
#include "config.h"
#include "session_log.h"
#include "presets.h"
#include <M5Cardputer.h>
#include <ctime>
#include <sys/time.h>
#include <algorithm>
#include <vector>

namespace Ui {
namespace {

// ----- Colours (RGB565) -----------------------------------------------------
constexpr uint16_t COL_BG   = 0x0000;
constexpr uint16_t COL_FG   = 0xFFFF;
constexpr uint16_t COL_HL   = 0x051F; // selection blue
constexpr uint16_t COL_DIM  = 0x8410; // grey
constexpr uint16_t COL_OK   = 0x07E0; // green
constexpr uint16_t COL_ERR  = 0xF800; // red
constexpr uint16_t COL_BAR  = 0x18E3; // dark header/footer

// ----- Layout ---------------------------------------------------------------
// Primary text is drawn at font size 2 (12x16 px) so it's readable on the small
// 240x135 panel. TXT_* are the cell sizes for that font.
constexpr int W = 240, H = 135;
constexpr int TXT_W = 12, TXT_H = 16;     // size-2 glyph cell
constexpr int kHeaderH = 22;
constexpr int kFooterH = 14;
constexpr int kRowH = 20;
constexpr int kBodyTop = kHeaderH + 2;            // 24
constexpr int kBodyBottom = H - kFooterH;          // 121
constexpr int kVisibleRows = (kBodyBottom - kBodyTop) / kRowH; // 4
constexpr int kMaxChars = W / TXT_W;               // 20 chars per line at size 2

// ----- Screens --------------------------------------------------------------
enum class Screen : uint8_t {
    BleScan,
    Home,
    CommandList,
    Channel,
    ParamEnum,
    Confirm,
    Login,
    LogView,
    Settings,
    TextInput,
    PresetList,
    PresetRun,
};

// ----- State ----------------------------------------------------------------
Screen   g_screen = Screen::BleScan;
int      g_cursor = 0;       // selection index on the current list
int      g_scroll = 0;       // first visible row
bool     g_dirty  = true;

int      g_catIdx = 0;       // chosen category
const CommandDef* g_cmd = nullptr;
String   g_channel;
String   g_param;

bool     g_loggedIn = false;
String   g_lastEncoderLine;        // most recent text from the encoder
String   g_notice;
uint32_t g_noticeUntil = 0;
bool     g_confirmArmed = false;   // destructive commands need a second ENTER
bool     g_autoLoginSuppressed = false; // set after an explicit Logout command
bool     g_awaitConfirm = false;   // a destructive cmd was sent; answer its Y/N
uint32_t g_awaitConfirmMs = 0;
uint32_t g_portalRefresh = 0;      // periodic redraw while the Wi-Fi portal is up

// Preset runner state.
int      g_presetIdx = 0;          // which preset is running
int      g_presetStep = 0;         // step currently being run
bool     g_presetRunning = false;
bool     g_presetWaiting = false;  // a step was sent; waiting for the encoder OK
uint32_t g_presetStepMs = 0;
volatile bool g_presetGotOk = false;
volatile bool g_presetGotYN = false;
String   g_presetLast;             // last status line shown on the run screen

// Password retype: the encoder asks "Retype password" after a password change.
String   g_retypePw;               // value to retype when prompted
String   g_pwCandidate;            // new password; saved only once confirmed
String   g_loginSavePending;       // login password to save once it succeeds
String   g_commitPw;               // password to write to NVS from the main loop
volatile bool g_retypePending = false;
uint32_t g_retypeMs = 0;
Screen   g_clockReturn = Screen::BleScan;  // where to go after Set date & time

// Text-entry context: where the confirmed text should go.
enum class TextTarget : uint8_t { Param, NewPassword, DeviceName, PresetLocation, SetClock };
TextTarget g_textTarget = TextTarget::Param;
String     g_textBuf;
String     g_textTitle;
String     g_textHint;

// ----- Forward declarations (definitions appear in dependency order below) --
void drawNoticeOverlay();
void beginParamStep();
void disconnectToScan();
void presetRunStep();
void presetAbort();
bool applyClock(const String& s);

// ----- Small helpers --------------------------------------------------------
// Explicit return type (the ESP32 Arduino core builds with C++11, which has no
// auto return-type deduction). decltype keeps us independent of the exact type.
decltype(M5Cardputer.Display)& D() { return M5Cardputer.Display; }

void setDirty() { g_dirty = true; }

bool clockIsSet() { return time(nullptr) > 1700000000; }  // RTC has been set

void notifyImpl(const String& msg) {
    g_notice = msg;
    g_noticeUntil = millis() + 2500;
    setDirty();
}

void clampCursor(int count) {
    if (count <= 0) { g_cursor = 0; g_scroll = 0; return; }
    if (g_cursor < 0) g_cursor = count - 1;
    if (g_cursor >= count) g_cursor = 0;
    if (g_cursor < g_scroll) g_scroll = g_cursor;
    if (g_cursor >= g_scroll + kVisibleRows) g_scroll = g_cursor - kVisibleRows + 1;
}

void gotoScreen(Screen s) {
    g_screen = s;
    g_cursor = 0;
    g_scroll = 0;
    if (s == Screen::Confirm) g_confirmArmed = false; // re-arm fresh each time
    setDirty();
}

// ----- Building list contents per screen ------------------------------------

// Home items (shown once connected): command categories, then tools.
std::vector<String> homeItems() {
    std::vector<String> v;
    for (auto& c : Commands::categories()) v.push_back(c.label);
    v.push_back("Presets");
    v.push_back("Session Log");
    v.push_back("Settings");
    v.push_back("Disconnect");
    return v;
}

std::vector<String> presetItems() {
    std::vector<String> v;
    for (auto& p : Presets::all()) v.push_back(p.name);
    return v;
}

// Maps the device rows shown on the scan screen to BleUart device indices
// (the displayed list is sorted/filtered, so positions differ).
std::vector<int> g_scanMap;
bool g_scanNamesOnly = false;

// Discovered Bluetooth devices (sorted), plus trailing actions.
std::vector<String> bleScanItems() {
    g_scanMap.clear();
    int n = BleUart::deviceCount();

    std::vector<int> idx;
    for (int i = 0; i < n; ++i) {
        BleUart::DeviceInfo d = BleUart::deviceAt(i);
        if (g_scanNamesOnly && !d.named) continue;
        idx.push_back(i);
    }
    // Likely encoder (UART service) first, then named devices, then strongest.
    std::sort(idx.begin(), idx.end(), [](int a, int b) {
        BleUart::DeviceInfo da = BleUart::deviceAt(a);
        BleUart::DeviceInfo db = BleUart::deviceAt(b);
        if (da.hasNus != db.hasNus) return da.hasNus;
        if (da.named  != db.named)  return da.named;
        return da.rssi > db.rssi;
    });

    std::vector<String> v;
    for (int i : idx) {
        BleUart::DeviceInfo d = BleUart::deviceAt(i);
        // "*" marks a device advertising a UART service (likely the encoder);
        // trailing number is signal strength (closer to 0 = nearer).
        String s = (d.hasNus ? "*" : " ") + d.label;
        if ((int)s.length() > 15) s = s.substring(0, 15);
        s += " " + String(d.rssi);
        g_scanMap.push_back(i);
        v.push_back(s);
    }

    v.push_back(v.size() ? "Rescan" : "Searching...");
    v.push_back(g_scanNamesOnly ? "Show all devices" : "Hide unnamed");
    v.push_back("Skip to menu");
    return v;
}

std::vector<String> commandItems() {
    std::vector<String> v;
    const auto& cat = Commands::categories()[g_catIdx];
    for (uint8_t i = 0; i < cat.count; ++i) v.push_back(cat.commands[i].label);
    return v;
}

std::vector<String> loginItems() {
    std::vector<String> v;
    for (auto& p : Config::get().passwords) v.push_back(p);
    v.push_back("[ Type new password ]");
    return v;
}

std::vector<String> settingsItems() {
    auto& cfg = Config::get();
    std::vector<String> v;
    String eol = cfg.lineEnding == "\r\n" ? "CRLF"
               : cfg.lineEnding == "\n"   ? "LF"
               : cfg.lineEnding == "\r"   ? "CR" : "?";
    v.push_back(String("Auto-login: ") + (cfg.autoLogin ? "ON" : "OFF"));
    v.push_back(String("Write w/ response: ") +
                (cfg.writeWithResponse ? "ON" : "OFF"));
    v.push_back(String("Line ending: ") + eol);
    v.push_back(String("Device filter: ") +
                (cfg.deviceName.length() ? cfg.deviceName : "(any NUS)"));
    v.push_back("Rescan / Reconnect");
    v.push_back("Set date & time");
    v.push_back("Reset to defaults");
    return v;
}

// ----- Rendering ------------------------------------------------------------
void drawHeader(const String& title) {
    D().fillRect(0, 0, W, kHeaderH, COL_BAR);
    D().setTextColor(COL_FG, COL_BAR);
    D().setTextSize(2);
    D().setCursor(4, 3);
    D().print(title.substring(0, kMaxChars));
}

void drawFooter() {
    D().fillRect(0, H - kFooterH, W, kFooterH, COL_BAR);
    D().setTextSize(1);   // status line stays compact

    bool conn = BleUart::connected();
    D().setTextColor(conn ? COL_OK : COL_DIM, COL_BAR);
    D().setCursor(4, H - kFooterH + 3);
    D().print(BleUart::statusText().substring(0, 30));

    D().setTextColor(g_loggedIn ? COL_OK : COL_ERR, COL_BAR);
    D().setCursor(W - 28, H - kFooterH + 3);
    D().print(g_loggedIn ? "AUTH" : "----");
}

void drawList(const String& title, const std::vector<String>& items,
              const String& emptyMsg = "") {
    D().fillRect(0, kBodyTop - 2, W, kBodyBottom - kBodyTop + 4, COL_BG);
    drawHeader(title);

    D().setTextSize(2);
    if (items.empty()) {
        D().setTextColor(COL_DIM, COL_BG);
        D().setCursor(8, kBodyTop + 6);
        D().print(emptyMsg.length() ? emptyMsg : "(empty)");
    } else {
        for (int row = 0; row < kVisibleRows; ++row) {
            int idx = g_scroll + row;
            if (idx >= (int)items.size()) break;
            int y = kBodyTop + row * kRowH;
            bool sel = (idx == g_cursor);
            if (sel) D().fillRect(0, y, W, kRowH, COL_HL);
            D().setTextColor(sel ? COL_FG : COL_DIM, sel ? COL_HL : COL_BG);
            D().setCursor(6, y + 2);
            D().print(items[idx].substring(0, kMaxChars - 1));
        }
        // position indicator (small)
        if ((int)items.size() > kVisibleRows) {
            D().setTextSize(1);
            D().setTextColor(COL_DIM, COL_BG);
            D().setCursor(W - 34, kBodyTop);
            D().printf("%d/%d", g_cursor + 1, (int)items.size());
        }
    }
    drawFooter();
    drawNoticeOverlay();
}

void drawNoticeOverlay() {
    if (!g_notice.length() || millis() > g_noticeUntil) {
        g_notice = "";
        return;
    }
    int boxH = 24;
    int y = (H - boxH) / 2;
    D().fillRect(6, y, W - 12, boxH, COL_DIM);
    D().drawRect(6, y, W - 12, boxH, COL_FG);
    D().setTextColor(COL_FG, COL_DIM);
    D().setTextSize(2);
    D().setCursor(12, y + 4);
    D().print(g_notice.substring(0, kMaxChars - 2));
}

void drawConfirm() {
    D().fillScreen(COL_BG);
    drawHeader("Send command");
    String line = Commands::build(*g_cmd, g_channel, g_param);

    D().setTextSize(2);
    D().setTextColor(COL_OK, COL_BG);
    int y = kBodyTop + 4;
    for (int pos = 0; pos < (int)line.length() && y < kBodyBottom - 36;
         pos += kMaxChars, y += TXT_H) {
        D().setCursor(6, y);
        D().print(line.substring(pos, pos + kMaxChars));
    }

    if (g_cmd->destructive) {
        D().setTextColor(COL_ERR, COL_BG);
        D().setCursor(6, kBodyBottom - 34);
        D().print(g_confirmArmed ? "ARMED" : "DESTRUCTIVE");
    }
    D().setTextSize(1);
    D().setTextColor(COL_DIM, COL_BG);
    D().setCursor(6, kBodyBottom - 10);
    if (g_cmd->destructive)
        D().print(g_confirmArmed ? "ENTER=SEND NOW   `=back"
                                 : "ENTER=arm, then ENTER   `=back");
    else
        D().print("ENTER=send   `=back");
    drawFooter();
    drawNoticeOverlay();
}


void drawTextInput() {
    D().fillScreen(COL_BG);
    drawHeader(g_textTitle);

    int boxY = kBodyTop + 16;
    if (g_textHint.length()) {
        // Short hints (e.g. the date format) are shown large so they're legible.
        bool big = g_textHint.length() <= 19;
        D().setTextSize(big ? 2 : 1);
        D().setTextColor(COL_DIM, COL_BG);
        D().setCursor(6, kBodyTop + 2);
        D().print(g_textHint.substring(0, big ? 19 : 38));
        boxY = kBodyTop + 2 + (big ? TXT_H : 8) + 8;
    }

    // input box
    D().drawRect(4, boxY, W - 8, TXT_H + 8, COL_DIM);
    D().setTextSize(2);
    D().setTextColor(COL_FG, COL_BG);
    D().setCursor(8, boxY + 4);
    String shown = g_textBuf;
    int maxShown = kMaxChars - 2;
    if ((int)shown.length() > maxShown)
        shown = shown.substring(shown.length() - maxShown);
    D().print(shown + "_");

    D().setTextSize(1);
    D().setTextColor(COL_DIM, COL_BG);
    D().setCursor(6, kBodyBottom - 10);
    D().print("ENTER=ok  DEL=del  `=cancel");
    drawFooter();
    drawNoticeOverlay();
}

void drawLogView() {
    D().fillScreen(COL_BG);
    drawHeader("Session Log");
    const auto& entries = SessionLog::entries();

    // Log is dense, so it stays at the small font to show more lines.
    D().setTextSize(1);
    const int helpY = kBodyBottom - 9;       // reserve a line for the hint
    int rows = (helpY - kBodyTop) / 10;
    int total = (int)entries.size();
    int start = total - rows;
    if (start < 0) start = 0;

    int y = kBodyTop;
    for (int i = start; i < total; ++i) {
        const auto& e = entries[i];
        uint16_t col = e.dir == SessionLog::Dir::Tx   ? COL_OK
                       : e.dir == SessionLog::Dir::Rx ? COL_FG
                                                      : COL_DIM;
        const char* tag = e.dir == SessionLog::Dir::Tx   ? ">"
                          : e.dir == SessionLog::Dir::Rx ? "<"
                                                         : "-";
        D().setTextColor(col, COL_BG);
        D().setCursor(4, y);
        D().print(String(tag) + " " + e.text.substring(0, 37));
        y += 10;
    }
    if (total == 0) {
        D().setTextColor(COL_DIM, COL_BG);
        D().setCursor(8, kBodyTop + 8);
        D().print("(no traffic yet)");
    }

    D().setTextColor(COL_OK, COL_BG);
    D().setCursor(4, helpY);
    String sf = SessionLog::sessionFile();
    if (sf.length())                  D().print("auto-saving " + sf + "  `=back");
    else if (SessionLog::sdAvailable()) D().print("auto-save pending  `=back");
    else                              D().print("NO SD CARD - not saving  `=back");
    drawFooter();
    drawNoticeOverlay();
}

void drawPresetRun() {
    D().fillScreen(COL_BG);
    const Preset& p = Presets::all()[g_presetIdx];
    drawHeader(p.name);
    D().setTextSize(2);
    D().setTextColor(g_presetRunning ? COL_FG : COL_OK, COL_BG);
    D().setCursor(4, kBodyTop + 6);
    if (g_presetRunning) D().printf("Step %d / %d", g_presetStep + 1, p.count);
    else                 D().print("Complete");
    D().setTextSize(1);
    D().setTextColor(COL_DIM, COL_BG);
    D().setCursor(4, kBodyTop + 6 + TXT_H + 6);
    D().print(g_presetLast.substring(0, 38));
    D().setCursor(4, kBodyBottom + 1);
    D().print(g_presetRunning ? "running...  `=stop" : "`=back");
    drawFooter();
    drawNoticeOverlay();
}

void render() {
    switch (g_screen) {
        case Screen::BleScan:
            drawList("Select encoder", bleScanItems());
            break;
        case Screen::PresetList:
            drawList("Presets", presetItems());
            break;
        case Screen::PresetRun:
            drawPresetRun();
            break;
        case Screen::Home:
            drawList("Encoder menu", homeItems());
            break;
        case Screen::CommandList:
            drawList(Commands::categories()[g_catIdx].label, commandItems());
            break;
        case Screen::Channel: {
            std::vector<String> v;
            for (auto& c : Commands::channels())
                v.push_back(c == "a" ? "a  (all gases)" : ("Gas " + c));
            drawList(String("Channel for ") + g_cmd->label, v);
            break;
        }
        case Screen::ParamEnum: {
            std::vector<String> v;
            for (uint8_t i = 0; i < g_cmd->enumCount; ++i)
                v.push_back(g_cmd->enumOpts[i].label);
            drawList(String(g_cmd->label) + " value", v);
            break;
        }
        case Screen::Confirm:  drawConfirm();   break;
        case Screen::Login:
            // Title shows the encoder's prompt (e.g. "Password:") when present.
            drawList(g_lastEncoderLine.length() ? g_lastEncoderLine
                                                : String("Login / Password"),
                     loginItems());
            break;
        case Screen::LogView:  drawLogView();   break;
        case Screen::Settings: drawList("Settings", settingsItems()); break;
        case Screen::TextInput:drawTextInput(); break;
    }
    g_dirty = false;
}

// ----- Actions --------------------------------------------------------------

void sendCurrentCommand() {
    String line = Commands::build(*g_cmd, g_channel, g_param);
    bool isLogout = String(g_cmd->id) == "gen_logout";
    bool isPassword = String(g_cmd->id) == "gen_password";
    // Changing the password: remember it for the encoder's "Retype password"
    // prompt. We only save it as our login once the encoder confirms success.
    if (isPassword && g_param.length()) {
        g_retypePw = g_param;
        g_pwCandidate = g_param;
    }
    // Password entry is paced (sendSlow) so the encoder doesn't drop characters.
    if (isPassword ? BleUart::sendSlow(line) : BleUart::send(line)) {
        SessionLog::tx(line);
        notifyImpl("Sent");
        // Destructive encoder commands (logclear/factory/reboot/...) reply with
        // a "Y or N" prompt; arm auto-confirm so main answers Y when it arrives.
        if (g_cmd->destructive) {
            g_awaitConfirm = true;
            g_awaitConfirmMs = millis();
        }
    } else {
        SessionLog::info("send failed (not connected): " + line);
        notifyImpl("Not connected");
    }
    g_param = "";
    if (isLogout) {
        // Logout ends the session: drop the link and return to the scan list.
        delay(150);             // let the command flush first
        disconnectToScan();
    } else {
        gotoScreen(Screen::CommandList);
    }
}

void sendPassword(const String& pw) {
    // Manual login: stop auto-login from racing this with a different password.
    g_autoLoginSuppressed = true;
    g_loginSavePending = pw;          // saved to the list only if it works
    if (BleUart::send(pw)) {
        SessionLog::info("login sent: " + pw);   // shown so it can be verified
        notifyImpl("Sent: " + pw);
    } else {
        notifyImpl("Not connected");
    }
    // Stay on the login screen; the success banner advances to the menu.
    gotoScreen(Screen::Login);
}

// After a command is chosen, walk to the next required step.
void enterCommand(const CommandDef* cmd) {
    g_cmd = cmd;
    g_param = "";
    if (cmd->needsChannel) {
        gotoScreen(Screen::Channel);
    } else {
        beginParamStep();
    }
}

void beginParamStep() {
    switch (g_cmd->param) {
        case ParamType::None:
            gotoScreen(Screen::Confirm);
            break;
        case ParamType::Enum:
            gotoScreen(Screen::ParamEnum);
            break;
        case ParamType::Numeric:
        case ParamType::Text:
            g_textTarget = TextTarget::Param;
            g_textBuf = "";
            g_textTitle = g_cmd->label;
            g_textHint = g_cmd->paramHint ? g_cmd->paramHint : "";
            gotoScreen(Screen::TextInput);
            break;
    }
}

void confirmTextInput() {
    g_textBuf.trim();
    switch (g_textTarget) {
        case TextTarget::Param:
            g_param = g_textBuf;
            gotoScreen(Screen::Confirm);
            break;
        case TextTarget::NewPassword:
            if (g_textBuf.length()) sendPassword(g_textBuf);
            else gotoScreen(Screen::Login);
            break;
        case TextTarget::DeviceName:
            Config::get().deviceName = g_textBuf;
            Config::save();
            notifyImpl("Saved. Rescanning...");
            BleUart::startScan();
            gotoScreen(Screen::Settings);
            break;
        case TextTarget::PresetLocation: {
            // Spaces -> \s so the encoder keeps the whole location as one token.
            String loc = g_textBuf;
            loc.replace(" ", "\\s");
            String line = "location " + loc;
            BleUart::send(line);
            SessionLog::tx(line);
            g_presetLast = line;
            g_presetWaiting = true;
            g_presetStepMs = millis();
            gotoScreen(Screen::PresetRun);
            break;
        }
        case TextTarget::SetClock:
            notifyImpl(applyClock(g_textBuf) ? "Clock set" : "Bad format");
            gotoScreen(g_clockReturn);
            break;
    }
}

// Drop the encoder link and return to the scan list (BLE keeps scanning).
void disconnectToScan() {
    g_loggedIn = false;
    g_autoLoginSuppressed = false;
    BleUart::disconnect();
    gotoScreen(Screen::BleScan);
}

// ----- Presets (chained commands) -------------------------------------------

void presetRunStep() {
    const Preset& p = Presets::all()[g_presetIdx];
    if (g_presetStep >= p.count) {        // finished
        g_presetRunning = false;
        g_presetLast = "Complete";
        notifyImpl("Preset complete");
        setDirty();
        return;
    }
    const PresetStep& st = p.steps[g_presetStep];
    if (st.kind == StepKind::PromptLocation) {
        g_textTarget = TextTarget::PresetLocation;
        g_textBuf = "";
        g_textTitle = "Enter location";
        g_textHint = "spaces ok";
        gotoScreen(Screen::TextInput);    // resumes in confirmTextInput()
        return;
    }
    String line = st.line;
    // If the preset changes the password, keep our saved login in sync so
    // auto-login still works on the next connection.
    bool isPw = line.startsWith("password ");
    if (isPw) {
        String pw = line.substring(9);
        pw.trim();
        if (pw.length()) {
            g_retypePw = pw;       // for the "Retype password" prompt
            g_pwCandidate = pw;    // saved as our login only once confirmed
        }
    }
    // Pace password entry (sendSlow) so the encoder doesn't drop characters.
    if (isPw ? BleUart::sendSlow(line) : BleUart::send(line)) {
        SessionLog::tx(line);
        g_presetLast = line;
    } else {
        SessionLog::info("preset: send failed: " + line);
        g_presetLast = "send failed";
    }
    g_presetWaiting = true;
    g_presetStepMs = millis();
    setDirty();
}

void presetStart(int idx) {
    g_presetIdx = idx;
    g_presetStep = 0;
    g_presetRunning = true;
    g_presetWaiting = false;
    g_presetGotOk = g_presetGotYN = false;
    g_presetLast = "starting...";
    gotoScreen(Screen::PresetRun);
    presetRunStep();
}

void presetAbort() {
    g_presetRunning = false;
    g_presetWaiting = false;
    notifyImpl("Preset stopped");
    gotoScreen(Screen::Home);
}

// Advance the running preset: answer Y/N, move on after OK, or time out.
void servicePreset() {
    if (!g_presetRunning || !g_presetWaiting) return;
    if (g_presetGotYN) {
        g_presetGotYN = false;
        BleUart::send("Y");
        SessionLog::info("preset: sent Y");
        g_presetStepMs = millis();
        return;
    }
    if (g_presetGotOk) {
        g_presetGotOk = false;
        g_presetWaiting = false;
        g_presetStep++;
        presetRunStep();
        return;
    }
    if (millis() - g_presetStepMs > 6000) {
        SessionLog::info("preset: step timed out, continuing");
        g_presetWaiting = false;
        g_presetStep++;
        presetRunStep();
    }
}

// Called from loop(): answer the encoder's "Retype password" prompt.
void serviceRetype() {
    if (!g_retypePending) return;
    if (millis() - g_retypeMs < 250) return;
    g_retypePending = false;
    if (!BleUart::connected() || !g_retypePw.length()) return;
    BleUart::sendSlow(g_retypePw);   // paced, like typing, so no dropped chars
    SessionLog::info("retype sent: " + g_retypePw);   // shown to verify
    g_retypePw = "";
    // Don't let a running preset time out while we confirm the password.
    if (g_presetRunning && g_presetWaiting) g_presetStepMs = millis();
}

// Parse 12 digits "DDMMYYYYHHMM" (separators ignored) and set the RTC.
bool applyClock(const String& s) {
    String d;
    for (size_t i = 0; i < s.length(); ++i)
        if (s[i] >= '0' && s[i] <= '9') d += s[i];
    if (d.length() != 12) return false;
    int D  = d.substring(0, 2).toInt();
    int Mo = d.substring(2, 4).toInt();
    int Y  = d.substring(4, 8).toInt();
    int H  = d.substring(8, 10).toInt();
    int Mi = d.substring(10, 12).toInt();
    if (Y < 2023 || Mo < 1 || Mo > 12 || D < 1 || D > 31 || H > 23 || Mi > 59)
        return false;
    struct tm tmv = {};
    tmv.tm_year = Y - 1900;
    tmv.tm_mon  = Mo - 1;
    tmv.tm_mday = D;
    tmv.tm_hour = H;
    tmv.tm_min  = Mi;
    time_t t = mktime(&tmv);
    if (t <= 0) return false;
    struct timeval tv = {t, 0};
    settimeofday(&tv, nullptr);
    return true;
}

void activateHome() {
    int nCats = (int)Commands::categories().size();
    if (g_cursor < nCats) {
        g_catIdx = g_cursor;
        gotoScreen(Screen::CommandList);
    } else if (g_cursor == nCats) {
        gotoScreen(Screen::PresetList);
    } else if (g_cursor == nCats + 1) {
        gotoScreen(Screen::LogView);
    } else if (g_cursor == nCats + 2) {
        gotoScreen(Screen::Settings);
    } else {
        disconnectToScan();         // Disconnect
    }
}

void activateSettings() {
    auto& cfg = Config::get();
    switch (g_cursor) {
        case 0: cfg.autoLogin = !cfg.autoLogin; Config::save(); setDirty(); break;
        case 1: cfg.writeWithResponse = !cfg.writeWithResponse; Config::save();
                setDirty(); break;
        case 2:   // cycle line ending CRLF -> LF -> CR
            cfg.lineEnding = cfg.lineEnding == "\r\n" ? "\n"
                           : cfg.lineEnding == "\n"   ? "\r" : "\r\n";
            Config::save();
            setDirty();
            break;
        case 3:
            g_textTarget = TextTarget::DeviceName;
            g_textBuf = cfg.deviceName;
            g_textTitle = "Device name filter";
            g_textHint = "blank = match any NUS device";
            gotoScreen(Screen::TextInput);
            break;
        case 4:
            BleUart::startScan();
            notifyImpl("Rescanning...");
            break;
        case 5:
            g_clockReturn = Screen::Settings;
            g_textTarget = TextTarget::SetClock;
            g_textBuf = "";
            g_textTitle = "Set date & time";
            g_textHint = "DDMMYYYYHHMM digits";
            gotoScreen(Screen::TextInput);
            break;
        case 6:
            Config::resetDefaults();
            notifyImpl("Defaults restored");
            setDirty();
            break;
    }
}

// ----- Input model ----------------------------------------------------------
enum class Key : uint8_t { None, Up, Down, Left, Right, Select, Back };

Key navFromChar(char c) {
    switch (c) {
        case ';': return Key::Up;
        case '.': return Key::Down;
        case ',': return Key::Left;
        case '/': return Key::Right;
        case '`': return Key::Back;
        default:  return Key::None;
    }
}

void handleListNav(Key k, int count) {
    if (k == Key::Up)   { g_cursor--; clampCursor(count); setDirty(); }
    if (k == Key::Down) { g_cursor++; clampCursor(count); setDirty(); }
}

void handleKeys(const Keyboard_Class::KeysState& st) {
    // ---- Text entry screen: take raw characters ----
    if (g_screen == Screen::TextInput) {
        bool changed = false;
        for (char c : st.word) {
            if (c == '`') {            // cancel text entry
                switch (g_textTarget) {
                    case TextTarget::NewPassword:    gotoScreen(Screen::Login); break;
                    case TextTarget::DeviceName:     gotoScreen(Screen::Settings); break;
                    case TextTarget::SetClock:       gotoScreen(g_clockReturn); break;
                    case TextTarget::PresetLocation: presetAbort(); break;
                    default:                         gotoScreen(Screen::CommandList); break;
                }
                return;
            }
            g_textBuf += c;
            changed = true;
        }
        if (st.space) { g_textBuf += ' '; changed = true; }
        if (st.del && g_textBuf.length()) {
            g_textBuf.remove(g_textBuf.length() - 1);
            changed = true;
        }
        if (st.enter) { confirmTextInput(); return; }
        if (changed) setDirty();
        return;
    }

    // ---- All other screens: navigation ----
    Key k = Key::None;
    for (char c : st.word) {
        Key kc = navFromChar(c);
        if (kc != Key::None) { k = kc; break; }
    }
    if (st.enter) k = Key::Select;

    if (k == Key::None) {
        // screen-specific extra keys (single chars)
        if (g_screen == Screen::LogView) {
            for (char c : st.word) {
                if (c == 'c') { SessionLog::clear(); setDirty(); }
            }
        }
        return;
    }

    switch (g_screen) {
        case Screen::BleScan: {
            auto items = bleScanItems();      // also rebuilds g_scanMap
            int shown = (int)g_scanMap.size(); // device rows; then 3 actions
            if (k == Key::Back) {
                // BleScan is the root screen; back does nothing.
            } else if (k == Key::Select) {
                if (g_cursor < shown) {
                    BleUart::connectIndex(g_scanMap[g_cursor]);
                    notifyImpl("Connecting...");
                    // Stay here; onBleState() moves to the login screen.
                } else if (g_cursor == shown) {          // Rescan
                    BleUart::startScan();
                    notifyImpl("Rescanning...");
                } else if (g_cursor == shown + 1) {      // toggle filter
                    g_scanNamesOnly = !g_scanNamesOnly;
                    g_cursor = 0;
                    setDirty();
                } else {                                  // Skip to menu
                    gotoScreen(Screen::Home);
                }
            } else handleListNav(k, items.size());
            break;
        }
        case Screen::Home: {
            auto items = homeItems();
            if (k == Key::Back) disconnectToScan();
            else if (k == Key::Select) activateHome();
            else handleListNav(k, items.size());
            break;
        }
        case Screen::CommandList: {
            const auto& cat = Commands::categories()[g_catIdx];
            if (k == Key::Back) { gotoScreen(Screen::Home); }
            else if (k == Key::Select) enterCommand(&cat.commands[g_cursor]);
            else handleListNav(k, cat.count);
            break;
        }
        case Screen::Channel: {
            const auto& ch = Commands::channels();
            if (k == Key::Back) { gotoScreen(Screen::CommandList); }
            else if (k == Key::Select) { g_channel = ch[g_cursor]; beginParamStep(); }
            else handleListNav(k, ch.size());
            break;
        }
        case Screen::ParamEnum: {
            if (k == Key::Back) {
                gotoScreen(g_cmd->needsChannel ? Screen::Channel
                                               : Screen::CommandList);
            } else if (k == Key::Select) {
                g_param = g_cmd->enumOpts[g_cursor].value;
                gotoScreen(Screen::Confirm);
            } else handleListNav(k, g_cmd->enumCount);
            break;
        }
        case Screen::Confirm:
            if (k == Key::Back) {
                gotoScreen(Screen::CommandList);
            } else if (k == Key::Select) {
                if (g_cmd->destructive && !g_confirmArmed) {
                    g_confirmArmed = true;   // first ENTER arms, second sends
                    setDirty();
                } else {
                    sendCurrentCommand();
                }
            }
            break;
        case Screen::Login: {
            auto items = loginItems();
            if (k == Key::Back) gotoScreen(Screen::Home);
            else if (k == Key::Select) {
                auto& pws = Config::get().passwords;
                if (g_cursor < (int)pws.size()) {
                    sendPassword(pws[g_cursor]);
                } else {
                    g_textTarget = TextTarget::NewPassword;
                    g_textBuf = "";
                    g_textTitle = "New password";
                    g_textHint = "saved for next time";
                    gotoScreen(Screen::TextInput);
                }
            } else handleListNav(k, items.size());
            break;
        }
        case Screen::Settings: {
            auto items = settingsItems();
            if (k == Key::Back) gotoScreen(Screen::Home);
            else if (k == Key::Select) activateSettings();
            else handleListNav(k, items.size());
            break;
        }
        case Screen::LogView:
            // Sessions auto-save to SD live; nothing to do but view/scroll.
            if (k == Key::Back) gotoScreen(Screen::Home);
            break;
        case Screen::PresetList: {
            auto items = presetItems();
            if (k == Key::Back) gotoScreen(Screen::Home);
            else if (k == Key::Select && g_cursor < (int)items.size())
                presetStart(g_cursor);
            else handleListNav(k, items.size());
            break;
        }
        case Screen::PresetRun:
            if (k == Key::Back) {
                if (g_presetRunning) presetAbort();
                else gotoScreen(Screen::Home);
            }
            break;
        default: break;
    }
}

} // namespace

// ----- Public API -----------------------------------------------------------

void begin() {
    D().setRotation(1);
    D().fillScreen(COL_BG);
    D().setTextSize(1);
    // Prompt for the clock on startup if it isn't set (cancellable with `).
    if (!clockIsSet()) {
        g_clockReturn = Screen::BleScan;
        g_textTarget = TextTarget::SetClock;
        g_textBuf = "";
        g_textTitle = "Set date & time";
        g_textHint = "DDMMYYYYHHMM digits";   // `=skip shown in the footer
        g_screen = Screen::TextInput;
    }
    setDirty();
}

void loop() {
    if (M5Cardputer.Keyboard.isChange() && M5Cardputer.Keyboard.isPressed()) {
        handleKeys(M5Cardputer.Keyboard.keysState());
    }
    // Commit a confirmed password to NVS here (main loop), never from the BLE
    // callback - flash writes there can intermittently stall the device.
    if (g_commitPw.length()) {
        Config::get().lastPassword = g_commitPw;
        Config::addPassword(g_commitPw);   // persists to NVS
        g_commitPw = "";
    }
    serviceRetype();    // answer a "Retype password" prompt
    servicePreset();    // advance a running preset (sends happen here, not in RX)
    // notice expiry forces a redraw
    if (g_notice.length() && millis() > g_noticeUntil) setDirty();
    // refresh the scan screen so the live device list updates
    if (g_screen == Screen::BleScan && millis() > g_portalRefresh) {
        g_portalRefresh = millis() + 1000;
        setDirty();
    }
    if (g_dirty) render();
}

void onRxLine(const String& line) {
    SessionLog::rx(line);
    g_lastEncoderLine = line;

    String lower = line;
    lower.toLowerCase();

    // Encoder asks to confirm a password change: re-send it (deferred to loop).
    if (lower.indexOf("retype") >= 0 && g_retypePw.length()) {
        g_retypePending = true;
        g_retypeMs = millis();
    }
    // Password change confirmed: adopt it as our login. The actual NVS write is
    // deferred to loop() - writing flash from this BLE callback can stall.
    if (lower.indexOf("updated successfully") >= 0 && g_pwCandidate.length()) {
        g_commitPw = g_pwCandidate;
        g_pwCandidate = "";
    } else if (lower.indexOf("not updated") >= 0 ||
               lower.indexOf("do not match") >= 0) {
        g_pwCandidate = "";       // change failed; keep the old saved login
        g_retypePw = "";
    }

    // Feed the preset runner: flag the encoder's OK / Y-N (or a password
    // success/failure reply) so servicePreset() (main loop) can advance.
    if (g_presetRunning && g_presetWaiting) {
        if (lower.indexOf("y or n") >= 0 || lower.indexOf("y/n") >= 0)
            g_presetGotYN = true;
        else if (line.endsWith("OK") ||
                 lower.indexOf("updated successfully") >= 0 ||
                 lower.indexOf("not updated") >= 0)
            g_presetGotOk = true;
    }

    if (line.indexOf(Config::get().loginSuccessMarker) >= 0) {
        setLoggedIn(true);
        notifyImpl("Logged in");
        // The password that just worked: adopt it (NVS write deferred to loop()).
        if (g_loginSavePending.length()) {
            g_commitPw = g_loginSavePending;
            g_loginSavePending = "";
        }
        if (g_screen == Screen::Login) gotoScreen(Screen::Home);
    }
    setDirty();
}

// True for screens that only make sense while in encoder mode and connected.
bool isLiveEncoderScreen(Screen s) {
    return s == Screen::Login || s == Screen::Home ||
           s == Screen::CommandList || s == Screen::Channel ||
           s == Screen::ParamEnum || s == Screen::Confirm ||
           s == Screen::Settings || s == Screen::LogView ||
           s == Screen::TextInput;
}

void onBleState(BleUart::State s) {
    if (s == BleUart::State::Connected) {
        // Connected: show the login screen so the step is visible. Auto-login
        // (handled in main) runs in the background; on the success banner we
        // jump to the menu automatically.
        g_lastEncoderLine = "";
        g_autoLoginSuppressed = false;   // fresh connection, allow auto-login
        gotoScreen(Screen::Login);
    } else {
        setLoggedIn(false);
        if (s == BleUart::State::Disconnected) {
            g_presetRunning = false;   // a dropped link aborts any running preset
            // Dropped link while using the encoder: go back to the scan list.
            if (isLiveEncoderScreen(g_screen) || g_screen == Screen::PresetRun ||
                g_screen == Screen::PresetList)
                gotoScreen(Screen::BleScan);
        }
    }
    SessionLog::info(String("BLE: ") + BleUart::statusText());
    setDirty();
}

void promptLogin() {
    if (g_screen != Screen::Login) gotoScreen(Screen::Login);
    notifyImpl("Enter password");
}

void onDevicesChanged() { setDirty(); }

void setLoggedIn(bool in) { g_loggedIn = in; setDirty(); }
bool loggedIn()           { return g_loggedIn; }
bool autoLoginSuppressed() { return g_autoLoginSuppressed; }
void rememberLogin(const String& pw) { g_loginSavePending = pw; }

// A destructive command was sent within the last few seconds and is awaiting a
// Y/N confirmation prompt from the encoder.
bool awaitEncoderConfirm() {
    return g_awaitConfirm && (millis() - g_awaitConfirmMs < 5000);
}
void clearEncoderConfirm() { g_awaitConfirm = false; }
void notify(const String& msg) { notifyImpl(msg); }

} // namespace Ui
