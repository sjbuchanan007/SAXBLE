#include "commands.h"

// ---------------------------------------------------------------------------
// Full SAX-D command set, modelled declaratively. The UI generates its menus,
// channel prompt and parameter entry from these tables, so adding/adjusting a
// command means editing only this file. See docs/COMMANDS.md for the reference.
//
// CommandDef fields:
//   id, label, token, needsChannel, param, paramHint,
//   enumOpts, enumCount, help, destructive
// ---------------------------------------------------------------------------

namespace {

// ----- Enum option sets -----------------------------------------------------

// Gas "type" — load default settings for a gas type.
const EnumOption kGasTypes[] = {
    {"USER", "USER"},   {"O2", "O2"},     {"N2O", "N2O"},
    {"ENT", "ENT"},     {"MA_4", "MA_4"}, {"MA_7", "MA_7"},
    {"SA_7", "SA_7"},   {"VAC", "VAC"},   {"N2", "N2"},
    {"CO2", "CO2"},     {"BOT_230", "BOT_230"}, {"BOT_250", "BOT_250"},
};

// Pressure display units.
const EnumOption kUnits[] = {
    {"Bar", "Bar"}, {"mmHg", "mmHg"}, {"PSI", "PSI"},
};

// Alarm tone selection.
const EnumOption kTones[] = {
    {"0", "Tone 0"}, {"1", "Tone 1"}, {"2", "Tone 2"},
};

#define ARRAYLEN(a) (uint8_t)(sizeof(a) / sizeof((a)[0]))

// ----- Gas settings (needsChannel = true) -----------------------------------

const CommandDef kGasCommands[] = {
    // Text strings
    {"gas_name",   "Name",            "Name",   true, ParamType::Text,
     "e.g. Oxygen", nullptr, 0, "Set the gas name text string", false},
    {"gas_normal", "Normal text",     "normal", true, ParamType::Text,
     "e.g. normal", nullptr, 0, "Normal-state text string", false},
    {"gas_high",   "High text",       "High",   true, ParamType::Text,
     "use \\n for newline", nullptr, 0, "High-pressure text string", false},
    {"gas_drops",  "Drop text",       "drops",  true, ParamType::Text,
     "use \\n for newline", nullptr, 0, "Pressure-drop text string", false},
    {"gas_low",    "Low text",        "Low",    true, ParamType::Text,
     "use \\n for newline", nullptr, 0, "Low-pressure text string", false},
    {"gas_fault",  "Fault text",      "Fault",  true, ParamType::Text,
     "e.g. fault", nullptr, 0, "Fault text string", false},
    // Alarm pressures
    {"gas_hi",     "Hi pressure set", "Hi_set", true, ParamType::Numeric,
     "bar e.g. 8.4", nullptr, 0, "Alarm above this pressure", false},
    {"gas_pd",     "Pressure drop set","Pd_set",true, ParamType::Numeric,
     "bar e.g. 7.8", nullptr, 0, "Pressure-drop alarm threshold", false},
    {"gas_lo",     "Lo pressure set", "Lo_set", true, ParamType::Numeric,
     "bar e.g. 7.6", nullptr, 0, "Alarm below this pressure", false},
    // Other settings
    {"gas_list",   "List settings",   "list",   true, ParamType::None,
     nullptr, nullptr, 0, "Display settings for this gas", false},
    {"gas_on",     "Enable",          "on",     true, ParamType::None,
     nullptr, nullptr, 0, "Enable this gas channel", false},
    {"gas_off",    "Disable",         "off",    true, ParamType::None,
     nullptr, nullptr, 0, "Disable this gas channel", false},
    {"gas_pron",   "Pressure: show",  "Press_on", true, ParamType::None,
     nullptr, nullptr, 0, "Display pressure", false},
    {"gas_proff",  "Pressure: hide",  "Press_off",true, ParamType::None,
     nullptr, nullptr, 0, "Do not display pressure", false},
    {"gas_pral",   "Pressure: in alarm","Press_al",true, ParamType::None,
     nullptr, nullptr, 0, "Show pressure only in alarm/warning", false},
    {"gas_preng",  "Pressure: on test","Press_eng",true, ParamType::None,
     nullptr, nullptr, 0, "Show pressure only when test pressed", false},
    {"gas_type",   "Type (defaults)", "type",   true, ParamType::Enum,
     nullptr, kGasTypes, ARRAYLEN(kGasTypes),
     "Load default settings for a gas type", false},
    {"gas_units",  "Units",           "units",  true, ParamType::Enum,
     nullptr, kUnits, ARRAYLEN(kUnits), "Set the pressure units", false},
    {"gas_cal",    "Calibrate point", "cal",    true, ParamType::Numeric,
     "reading e.g. 4.15", nullptr, 0,
     "Update the reading calibration point", true},
    {"gas_atm",    "Atmosphere zero", "atm",    true, ParamType::Numeric,
     "e.g. 0", nullptr, 0, "Update the zero (atmosphere) calibration", true},
};

// ----- General settings (needsChannel = false) ------------------------------

const CommandDef kGeneralCommands[] = {
    {"gen_tone",     "Tone",          "Tone",       false, ParamType::Enum,
     nullptr, kTones, ARRAYLEN(kTones), "Change alarm tone", false},
    {"gen_mute",     "Mute timer",    "Mute",       false, ParamType::Numeric,
     "min (0=off)", nullptr, 0, "Set mute timeout in minutes", false},
    {"gen_logout",   "Logout",        "Logout",     false, ParamType::None,
     nullptr, nullptr, 0, "Log out of the console", false},
    {"gen_logouttime","Auto-logout",  "Logouttime", false, ParamType::Numeric,
     "minutes", nullptr, 0, "Set auto-logout time", false},
    {"gen_password", "Change password","Password",  false, ParamType::Text,
     "new password", nullptr, 0, "Change the login password", true},
    {"gen_modbus",   "Modbus",        "Modbus",     false, ParamType::Text,
     "addr(1-127) baud(0-3)", nullptr, 0,
     "Set Modbus address and baud rate", false},
    {"gen_location", "Location",      "location",   false, ParamType::Text,
     "use \\s for space", nullptr, 0, "Set the system location", false},
    {"gen_settings", "Settings",      "settings",   false, ParamType::None,
     nullptr, nullptr, 0, "List system settings", false},
    {"gen_logtime",  "Log interval",  "Logtime",    false, ParamType::Numeric,
     "minutes", nullptr, 0, "Set min/max log time interval", false},
    {"gen_logdump",  "Log dump",      "Logdump",    false, ParamType::None,
     nullptr, nullptr, 0, "List the log data", false},
    {"gen_logclear", "Log clear",     "Logclear",   false, ParamType::None,
     nullptr, nullptr, 0, "Clear the log data", true},
    {"gen_factory",  "Factory reset", "Factory",    false, ParamType::None,
     nullptr, nullptr, 0, "Reset to factory defaults", true},
    {"gen_reboot",   "Reboot",        "reboot",     false, ParamType::None,
     nullptr, nullptr, 0, "Reboot the encoder", true},
    {"gen_help",     "Help (list)",   "help",       false, ParamType::None,
     nullptr, nullptr, 0, "List general commands", false},
    {"gen_gashelp",  "Gas help (list)","help gas",   false, ParamType::None,
     nullptr, nullptr, 0, "List gas commands", false},
};

const CommandCategory kCategories[] = {
    {"gas",     "Gas Settings",     kGasCommands,     ARRAYLEN(kGasCommands)},
    {"general", "General Settings", kGeneralCommands, ARRAYLEN(kGeneralCommands)},
};

std::vector<CommandCategory> g_categories(
    kCategories, kCategories + ARRAYLEN(kCategories));

std::vector<String> g_channels = {"1", "2", "3", "4", "5", "6", "a"};

} // namespace

namespace Commands {

const std::vector<CommandCategory>& categories() { return g_categories; }

const std::vector<String>& channels() { return g_channels; }

String build(const CommandDef& cmd, const String& channel, const String& param) {
    String out;
    if (cmd.needsChannel) {
        out = "Gas " + channel + " " + cmd.token;
    } else {
        out = cmd.token;
    }
    if (cmd.param != ParamType::None && param.length()) {
        out += " " + param;
    }
    return out;
}

} // namespace Commands
