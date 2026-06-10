#pragma once
#include <Arduino.h>
#include <vector>

// Data model for the SAX-D command set.
//
// A command is defined declaratively (token, parameter type, hints) so the full
// command list can be filled in by adding table entries — no UI changes needed.
// The UI walks these tables to build the menus and parameter prompts.

enum class ParamType : uint8_t {
    None,     // command takes no parameter (e.g. "on", "list", "reboot")
    Numeric,  // a number — setpoints (bar), timers (min), addresses, etc.
    Text,     // free text string (names, location). "\n"/"\s" escapes allowed.
    Enum,     // pick one of a fixed set of tokens (gas type, tone, baud, ...)
};

struct EnumOption {
    const char* value;  // token actually sent
    const char* label;  // shown in the menu (may equal value)
};

struct CommandDef {
    const char* id;            // stable identifier
    const char* label;         // menu display name
    const char* token;         // command keyword sent to the encoder
    bool        needsChannel;  // gas commands: prompt channel (1-6 / a) first
    ParamType   param;
    const char* paramHint;     // units / example shown during entry
    const EnumOption* enumOpts; // for ParamType::Enum
    uint8_t     enumCount;
    const char* help;          // one-line description
};

struct CommandCategory {
    const char* id;
    const char* label;
    const CommandDef* commands;
    uint8_t     count;
};

namespace Commands {
    const std::vector<CommandCategory>& categories();

    // Valid gas channels: "1".."6" and "a" (all).
    const std::vector<String>& channels();

    // Assemble the final command line for the encoder.
    //   gas command:     "Gas <channel> <token> [param]"
    //   general command: "<token> [param]"
    String build(const CommandDef& cmd, const String& channel, const String& param);
}
