# SAX-D Command Reference

The full command set from the SAX-D manual, as modelled by the firmware. Each
row is a `CommandDef` entry in `src/commands.cpp`; the UI generates its menus,
the gas-channel prompt and the parameter entry from these tables.

The firmware assembles each line as:

- **Gas commands:** `Gas <channel> <token> [param]`, `<channel>` = `1`–`6` or
  `a` (all).
- **General commands:** `<token> [param]`.

Text escapes used by the encoder: `\n` = newline inside a string, `\s` = a
space (used by `location`).

Commands marked **⚠** are flagged *destructive* in the firmware and require a
second confirmation (ENTER to arm, ENTER again to send) before they go out.

---

## Gas settings (`gas` category, `needsChannel = true`)

Tokens below match the encoder's own `help gas` output (lowercase). Commands
are sent with a lowercase `gas` prefix, e.g. `gas 1 hi_set 5.6`.

| Token | Label | Param | Notes |
|-------|-------|-------|-------|
| `name`      | Name              | Text    | gas name string, e.g. `Oxygen` |
| `normal`    | Normal text       | Text    | normal-state string, e.g. `Normal` |
| `high`      | High text         | Text    | high-pressure string, e.g. `High\nPressure` |
| `drop`      | Drop text         | Text    | pressure-drop string, e.g. `Pressure\nDrop` |
| `low`       | Low text          | Text    | low-pressure string, e.g. `Low\nPressure` |
| `fault`     | Fault text        | Text    | fault string, e.g. `Signal\nFault` |
| `hi_set`    | Hi pressure set   | Numeric | hi alarm above value (bar) |
| `pd_set`    | Pressure drop set | Numeric | pressure-drop alarm (bar) |
| `lo_set`    | Lo pressure set   | Numeric | lo alarm below value (bar) |
| `hi_diff`   | Hi differential   | Numeric | hi-alarm hysteresis (bar) |
| `pd_diff`   | Drop differential | Numeric | drop-alarm hysteresis (bar) |
| `lo_diff`   | Lo differential   | Numeric | lo-alarm hysteresis (bar) |
| `list`      | List settings     | None    | display settings for this gas |
| `on`        | Enable            | None    | enable this gas |
| `off`       | Disable           | None    | disable this gas |
| `press_on`  | Pressure: show    | None    | display pressure |
| `press_off` | Pressure: hide    | None    | do not display pressure |
| `press_al`  | Pressure: in alarm| None    | show pressure only in alarm/warning |
| `press_eng` | Pressure: on test | None    | show pressure only when test pressed |
| `type`      | Type (defaults)   | Enum    | load gas-type defaults (see below) |
| `units`     | Units             | Enum    | `Bar`, `mmHg`, `PSI` |
| `cal` ⚠     | Calibrate point   | Numeric | update reading calibration point |
| `atm` ⚠     | Atmosphere zero   | Numeric | update zero (atmosphere) calibration |

**Gas `type` enum values** (from the encoder's `help gas`): `O2`, `N2O`, `ENT`,
`MA_4`, `MA_7`, `SA_7`, `VAC`, `N2`, `CO2`, `USER`. (The manual's `BOT_230` /
`BOT_250` are not supported on this unit.)

---

## General settings (`general` category, `needsChannel = false`)

Tokens match the encoder's own `help` output (lowercase).

| Token | Label | Param | Notes |
|-------|-------|-------|-------|
| `tone`       | Tone            | Enum    | `0`, `1`, `2` |
| `mute`       | Mute timer      | Numeric | minutes; `0` = no timeout |
| `logout`     | Logout          | None    | log out of the console |
| `logouttime` | Auto-logout     | Numeric | minutes |
| `password` ⚠ | Change password | Text    | sets a new login password |
| `modbus`     | Modbus          | Text    | `<address> <baud>` (addr 1–127; baud 0=9600, 1=19200, 2=38400, 3=115200) |
| `location`   | Location        | Text    | use `\s` for spaces, e.g. `Ward\s10` |
| `settings`   | Settings        | None    | list system settings |
| `logtime`    | Log interval    | Numeric | minutes between log entries |
| `logdump`    | Log dump        | None    | list the log data |
| `logclear` ⚠ | Log clear       | None    | clears the log data |
| `factory` ⚠  | Factory reset   | None    | reset to factory defaults |
| `reboot` ⚠   | Reboot          | None    | reboot the encoder |
| `help`       | Help (list)     | None    | list general commands |
| `help gas`   | Gas help (list) | None    | list gas commands |

**Not yet wired** (present in the encoder's `help` but syntax unknown):
`engineer`, `screensave`.

---

## Notes & assumptions to confirm on hardware

These were inferred from the manual; verify the exact spelling/casing against a
live encoder and adjust the `token` strings in `src/commands.cpp` if needed:

- Token **case** is taken from the manual's examples (e.g. `Hi_set`, `Name`,
  lowercase `on`/`off`/`list`/`reboot`). If the CLI is case-insensitive this
  doesn't matter; if not, these are the first thing to check.
- `Modbus` takes two values; it's entered as one text field
  (`"<addr> <baud>"`).
- `units` offers `Bar`/`mmHg`/`PSI`; the manual hinted at a possible "other" —
  add an enum option if the encoder supports it.
- `reboot` is lowercase per the manual's example table.

## How to add or change a command

Add a `CommandDef` to `kGasCommands` or `kGeneralCommands` in
`src/commands.cpp`:

```cpp
//  id           label          token   needsChannel  param            hint
{"gas_normal", "Normal text", "normal", true, ParamType::Text, "e.g. normal",
//  enumOpts  enumCount  help                    destructive
    nullptr,  0,        "Normal-state text string", false},
```

For an enum parameter, define an `EnumOption[]` and point `enumOpts`/`enumCount`
at it (see `kGasTypes`). Set the final `destructive` flag to `true` to require
the two-step confirmation. No other code changes are needed.
