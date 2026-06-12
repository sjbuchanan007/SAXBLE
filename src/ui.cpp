#include "ui.h"
#include "commands.h"
#include "config.h"
#include "session_log.h"
#include "wifi_portal.h"
#include <M5Cardputer.h>
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
    WifiPortal,
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
uint32_t g_portalRefresh = 0;      // periodic redraw while the Wi-Fi portal is up

// Text-entry context: where the confirmed text should go.
enum class TextTarget : uint8_t { Param, NewPassword, DeviceName };
TextTarget g_textTarget = TextTarget::Param;
String     g_textBuf;
String     g_textTitle;
String     g_textHint;

// ----- Forward declarations (definitions appear in dependency order below) --
void drawNoticeOverlay();
void beginParamStep();
void startWifiPortal();
void stopWifiPortal();

// ----- Small helpers --------------------------------------------------------
// Explicit return type (the ESP32 Arduino core builds with C++11, which has no
// auto return-type deduction). decltype keeps us independent of the exact type.
decltype(M5Cardputer.Display)& D() { return M5Cardputer.Display; }

void setDirty() { g_dirty = true; }

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

// Home items: each command category, then the fixed tools.
std::vector<String> homeItems() {
    std::vector<String> v;
    for (auto& c : Commands::categories()) v.push_back(c.label);
    v.push_back("Bluetooth (connect)");
    v.push_back("Login / Password");
    v.push_back("Session Log");
    v.push_back("Wi-Fi Log Export");
    v.push_back("Settings");
    return v;
}

// Discovered Bluetooth devices, plus trailing actions.
std::vector<String> bleScanItems() {
    std::vector<String> v;
    int n = BleUart::deviceCount();
    for (int i = 0; i < n; ++i) {
        BleUart::DeviceInfo d = BleUart::deviceAt(i);
        // "*" prefix marks a device advertising the UART service (likely the
        // encoder); trailing number is signal strength (closer = nearer 0).
        String s = (d.hasNus ? "*" : " ") + d.label;
        if ((int)s.length() > 15) s = s.substring(0, 15);
        s += " " + String(d.rssi);
        v.push_back(s);
    }
    v.push_back(n ? "Rescan" : "Searching... (Rescan)");
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
    v.push_back(String("Auto-login: ") + (cfg.autoLogin ? "ON" : "OFF"));
    v.push_back(String("Write w/ response: ") +
                (cfg.writeWithResponse ? "ON" : "OFF"));
    v.push_back(String("Device filter: ") +
                (cfg.deviceName.length() ? cfg.deviceName : "(any NUS)"));
    v.push_back("Rescan / Reconnect");
    v.push_back("Export log to SD");
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

void drawWifiPortal() {
    D().fillScreen(COL_BG);
    drawHeader("Wi-Fi Export");

    D().setTextSize(2);
    int y = kBodyTop + 2;
    D().setTextColor(COL_FG, COL_BG);
    D().setCursor(4, y);            D().print("Join Wi-Fi:");
    D().setTextColor(COL_OK, COL_BG);
    D().setCursor(4, y + TXT_H);     D().print(WifiPortal::ssid());
    D().setCursor(4, y + TXT_H * 2);  D().print("pw " + WifiPortal::password());
    D().setTextColor(COL_FG, COL_BG);
    D().setCursor(4, y + TXT_H * 3 + 2); D().print("Browse to:");
    D().setTextColor(COL_OK, COL_BG);
    D().setCursor(4, y + TXT_H * 4 + 2); D().print(WifiPortal::ip());

    D().setTextSize(1);
    D().setTextColor(COL_DIM, COL_BG);
    D().setCursor(4, kBodyBottom + 1);
    D().printf("clients:%d   `=stop & back", WifiPortal::clientCount());
    // footer omitted: BLE is paused while the portal is up
    drawNoticeOverlay();
}

void drawTextInput() {
    D().fillScreen(COL_BG);
    drawHeader(g_textTitle);

    if (g_textHint.length()) {
        D().setTextSize(1);
        D().setTextColor(COL_DIM, COL_BG);
        D().setCursor(6, kBodyTop + 2);
        D().print(g_textHint.substring(0, 38));
    }

    // input box
    int boxY = kBodyTop + 16;
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

    D().setTextColor(COL_DIM, COL_BG);
    D().setCursor(4, helpY);
    D().print(SessionLog::sdAvailable() ? "ENTER=export SD  c=clear  `=back"
                                        : "no SD  c=clear  `=back");
    drawFooter();
    drawNoticeOverlay();
}

void render() {
    switch (g_screen) {
        case Screen::BleScan:
            drawList("Select encoder", bleScanItems());
            break;
        case Screen::Home:
            drawList("SAXBLE menu", homeItems());
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
        case Screen::WifiPortal:drawWifiPortal(); break;
    }
    g_dirty = false;
}

// ----- Actions --------------------------------------------------------------

void sendCurrentCommand() {
    String line = Commands::build(*g_cmd, g_channel, g_param);
    if (BleUart::send(line)) {
        SessionLog::tx(line);
        notifyImpl("Sent");
    } else {
        SessionLog::info("send failed (not connected): " + line);
        notifyImpl("Not connected");
    }
    g_param = "";
    gotoScreen(Screen::CommandList);
}

void sendPassword(const String& pw) {
    auto& cfg = Config::get();
    Config::addPassword(pw);
    cfg.lastPassword = pw;
    Config::save();
    if (BleUart::send(pw)) {
        SessionLog::info("login sent (password hidden)");
        notifyImpl("Password sent");
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
    }
}

void activateHome() {
    auto items = homeItems();
    int nCats = (int)Commands::categories().size();
    if (g_cursor < nCats) {
        g_catIdx = g_cursor;
        gotoScreen(Screen::CommandList);
    } else if (g_cursor == nCats) {
        BleUart::startScan();
        gotoScreen(Screen::BleScan);
    } else if (g_cursor == nCats + 1) {
        gotoScreen(Screen::Login);
    } else if (g_cursor == nCats + 2) {
        gotoScreen(Screen::LogView);
    } else if (g_cursor == nCats + 3) {
        startWifiPortal();
    } else {
        gotoScreen(Screen::Settings);
    }
}

// Wi-Fi shares the radio with BLE, so pause BLE before bringing up the AP.
void startWifiPortal() {
    BleUart::pause();
    if (WifiPortal::start()) {
        gotoScreen(Screen::WifiPortal);
    } else {
        BleUart::resume();
        notifyImpl("Wi-Fi failed to start");
    }
}

void stopWifiPortal() {
    WifiPortal::stop();
    BleUart::resume();
    gotoScreen(Screen::Home);
}

void activateSettings() {
    auto& cfg = Config::get();
    switch (g_cursor) {
        case 0: cfg.autoLogin = !cfg.autoLogin; Config::save(); setDirty(); break;
        case 1: cfg.writeWithResponse = !cfg.writeWithResponse; Config::save();
                setDirty(); break;
        case 2:
            g_textTarget = TextTarget::DeviceName;
            g_textBuf = cfg.deviceName;
            g_textTitle = "Device name filter";
            g_textHint = "blank = match any NUS device";
            gotoScreen(Screen::TextInput);
            break;
        case 3:
            BleUart::startScan();
            notifyImpl("Rescanning...");
            break;
        case 4: {
            String err;
            String path = SessionLog::exportToSd(&err);
            notifyImpl(path.length() ? ("Saved " + path) : ("Export: " + err));
            break;
        }
        case 5:
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
            if (c == '`') { gotoScreen(  // cancel
                    g_textTarget == TextTarget::NewPassword ? Screen::Login
                    : g_textTarget == TextTarget::DeviceName ? Screen::Settings
                                                             : Screen::CommandList);
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
            int n = BleUart::deviceCount();
            auto items = bleScanItems();      // n devices + Rescan + Skip
            if (k == Key::Back) {
                gotoScreen(Screen::Home);
            } else if (k == Key::Select) {
                if (g_cursor < n) {
                    BleUart::connectIndex(g_cursor);
                    notifyImpl("Connecting...");
                    // Stay here; onBleState() moves to the login screen on
                    // connect.
                } else if (g_cursor == n) {
                    BleUart::startScan();
                    notifyImpl("Rescanning...");
                } else {
                    gotoScreen(Screen::Home);
                }
            } else handleListNav(k, items.size());
            break;
        }
        case Screen::Home: {
            auto items = homeItems();
            if (k == Key::Select) activateHome();
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
            if (k == Key::Back) gotoScreen(Screen::Home);
            else if (k == Key::Select) {
                String err;
                String path = SessionLog::exportToSd(&err);
                notifyImpl(path.length() ? ("Saved " + path)
                                         : ("Export: " + err));
            }
            break;
        case Screen::WifiPortal:
            if (k == Key::Back) stopWifiPortal();
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
    setDirty();
}

void loop() {
    if (M5Cardputer.Keyboard.isChange() && M5Cardputer.Keyboard.isPressed()) {
        handleKeys(M5Cardputer.Keyboard.keysState());
    }
    // notice expiry forces a redraw
    if (g_notice.length() && millis() > g_noticeUntil) setDirty();
    // refresh the portal/scan screens so live info (clients, devices) updates
    if ((g_screen == Screen::WifiPortal || g_screen == Screen::BleScan) &&
        millis() > g_portalRefresh) {
        g_portalRefresh = millis() + 1000;
        setDirty();
    }
    if (g_dirty) render();
}

void onRxLine(const String& line) {
    SessionLog::rx(line);
    g_lastEncoderLine = line;
    if (line.indexOf(Config::get().loginSuccessMarker) >= 0) {
        setLoggedIn(true);
        notifyImpl("Logged in");
        if (g_screen == Screen::Login) gotoScreen(Screen::Home);
    }
    setDirty();
}

void onBleState(BleUart::State s) {
    if (s != BleUart::State::Connected) {
        setLoggedIn(false);
    } else {
        // Connected: show the login screen so the step is visible. Auto-login
        // (handled in main) runs in the background; on the success banner we
        // jump to the menu automatically.
        g_lastEncoderLine = "";
        gotoScreen(Screen::Login);
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
void notify(const String& msg) { notifyImpl(msg); }

} // namespace Ui
