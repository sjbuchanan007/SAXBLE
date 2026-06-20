#include "presets.h"

namespace {

// 2-gas panel: O2 (1), Vacuum (3); gas 2 disabled.
const PresetStep k2Gas[] = {
    {StepKind::Cmd, "gas 1 type O2"},
    {StepKind::Cmd, "gas 2 off"},
    {StepKind::Cmd, "gas 3 type VAC"},
    {StepKind::Cmd, "gas a press_on"},
    {StepKind::Cmd, "password MMSmms659"},
    {StepKind::PromptLocation, nullptr},
    {StepKind::Cmd, "gas a list"},          // snapshot the result into the log
};

// 3-gas panel: O2 (1), Medical Air (2), Vacuum (3).
const PresetStep k3Gas[] = {
    {StepKind::Cmd, "gas 1 type O2"},
    {StepKind::Cmd, "gas 2 type MA_4"},
    {StepKind::Cmd, "gas 3 type VAC"},
    {StepKind::Cmd, "gas a press_on"},
    {StepKind::Cmd, "password MMSmms659"},
    {StepKind::PromptLocation, nullptr},
    {StepKind::Cmd, "gas a list"},
};

#define ARRAYLEN(a) (uint8_t)(sizeof(a) / sizeof((a)[0]))

const Preset kPresets[] = {
    {"2-Gas O2/Vac",      k2Gas, ARRAYLEN(k2Gas)},
    {"3-Gas O2/MA4/Vac",  k3Gas, ARRAYLEN(k3Gas)},
};

std::vector<Preset> g_presets(kPresets, kPresets + ARRAYLEN(kPresets));

} // namespace

namespace Presets {
const std::vector<Preset>& all() { return g_presets; }
}
