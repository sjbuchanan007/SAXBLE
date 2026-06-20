#pragma once
#include <Arduino.h>
#include <vector>

// Presets: named sequences of commands run one after another (waiting for each
// "OK", auto-answering any "Y or N" prompt). A step is either a fixed command
// line or a prompt that asks the operator for input mid-sequence.

enum class StepKind : uint8_t {
    Cmd,             // send `line` verbatim
    PromptLocation,  // ask the operator for a location, then send `location <it>`
};

struct PresetStep {
    StepKind    kind;
    const char* line;   // used for Cmd
};

struct Preset {
    const char*       name;
    const PresetStep* steps;
    uint8_t           count;
};

namespace Presets {
    const std::vector<Preset>& all();
}
