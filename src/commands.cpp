#include "commands.h"

// ---------------------------------------------------------------------------
// SCAFFOLD COVERAGE
//
// This table wires up a representative slice of the SAX-D command set so every
// input shape is exercised end-to-end: no-param, numeric, text and enum
// parameters, with and without a gas channel. The remaining commands from the
// manual are documented in docs/COMMANDS.md and drop straight into these tables
// with no other code changes.
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

// Pressure display modes (Press_on / Press_off / Press_al / Press_eng).
// Modelled as distinct commands below rather than an enum, matching the manual.

// Tone selection.
const EnumOption kTones[] = {
    {"0", "Tone 0"}, {"1", "Tone 1"}, {"2", "Tone 2"},
};

// ----- Gas settings ---------------------------------------------------------

const CommandDef kGasCommands[] = {
    {"gas_name",   "Name",            "Name",   true, ParamType::Text,
     "e.g. Oxygen", nullptr, 0, "Set the gas name text string"},
    {"gas_hi",     "Hi pressure set", "Hi_set", true, ParamType::Numeric,
     "bar e.g. 8.4", nullptr, 0, "Alarm above this pressure"},
    {"gas_pd",     "Pressure drop set","Pd_set",true, ParamType::Numeric,
     "bar e.g. 7.8", nullptr, 0, "Pressure-drop alarm threshold"},
    {"gas_lo",     "Lo pressure set", "Lo_set", true, ParamType::Numeric,
     "bar e.g. 7.6", nullptr, 0, "Alarm below this pressure"},
    {"gas_type",   "Type (defaults)", "type",   true, ParamType::Enum,
     nullptr, kGasTypes, sizeof(kGasTypes) / sizeof(kGasTypes[0]),
     "Load default settings for a gas type"},
    {"gas_on",     "Enable",          "on",     true, ParamType::None,
     nullptr, nullptr, 0, "Enable this gas channel"},
    {"gas_off",    "Disable",         "off",    true, ParamType::None,
     nullptr, nullptr, 0, "Disable this gas channel"},
    {"gas_list",   "List settings",   "list",   true, ParamType::None,
     nullptr, nullptr, 0, "Display settings for this gas"},
};

// ----- General settings -----------------------------------------------------

const CommandDef kGeneralCommands[] = {
    {"gen_tone",     "Tone",        "Tone",     false, ParamType::Enum,
     nullptr, kTones, sizeof(kTones) / sizeof(kTones[0]), "Change alarm tone"},
    {"gen_mute",     "Mute timer",  "Mute",     false, ParamType::Numeric,
     "min (0=off)", nullptr, 0, "Set mute timeout in minutes"},
    {"gen_location", "Location",    "location", false, ParamType::Text,
     "use \\s for space", nullptr, 0, "Set the system location"},
    {"gen_settings", "Settings",    "settings", false, ParamType::None,
     nullptr, nullptr, 0, "List system settings"},
    {"gen_reboot",   "Reboot",      "reboot",   false, ParamType::None,
     nullptr, nullptr, 0, "Reboot the encoder"},
};

const CommandCategory kCategories[] = {
    {"gas",     "Gas Settings",     kGasCommands,
     sizeof(kGasCommands) / sizeof(kGasCommands[0])},
    {"general", "General Settings", kGeneralCommands,
     sizeof(kGeneralCommands) / sizeof(kGeneralCommands[0])},
};

std::vector<CommandCategory> g_categories(
    kCategories, kCategories + sizeof(kCategories) / sizeof(kCategories[0]));

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
