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

| Token | Label | Param | Notes |
|-------|-------|-------|-------|
| `Name`      | Name              | Text    | gas name string, e.g. `Oxygen` |
| `normal`    | Normal text       | Text    | normal-state string, e.g. `normal` |
| `High`      | High text         | Text    | high-pressure string, e.g. `high\npressure` |
| `drops`     | Drop text         | Text    | pressure-drop string, e.g. `pressure\ndrop` |
| `Low`       | Low text          | Text    | low-pressure string, e.g. `low\npressure` |
| `Fault`     | Fault text        | Text    | fault string, e.g. `fault` |
| `Hi_set`    | Hi pressure set   | Numeric | hi alarm above value (bar), e.g. `8.4` |
| `Pd_set`    | Pressure drop set | Numeric | pressure-drop alarm (bar), e.g. `7.8` |
| `Lo_set`    | Lo pressure set   | Numeric | lo alarm below value (bar), e.g. `7.6` |
| `list`      | List settings     | None    | display settings for this gas |
| `on`        | Enable            | None    | enable this gas |
| `off`       | Disable           | None    | disable this gas |
| `Press_on`  | Pressure: show    | None    | display pressure |
| `Press_off` | Pressure: hide    | None    | do not display pressure |
| `Press_al`  | Pressure: in alarm| None    | show pressure only in alarm/warning |
| `Press_eng` | Pressure: on test | None    | show pressure only when test pressed |
| `type`      | Type (defaults)   | Enum    | load gas-type defaults (see below) |
| `units`     | Units             | Enum    | `Bar`, `mmHg`, `PSI` |
| `cal` ⚠     | Calibrate point   | Numeric | update reading calibration point |
| `atm` ⚠     | Atmosphere zero   | Numeric | update zero (atmosphere) calibration |

**Gas `type` enum values:** `USER`, `O2`, `N2O`, `ENT`, `MA_4`, `MA_7`,
`SA_7`, `VAC`, `N2`, `CO2`, `BOT_230`, `BOT_250`.

---

## General settings (`general` category, `needsChannel = false`)

| Token | Label | Param | Notes |
|-------|-------|-------|-------|
| `Tone`       | Tone            | Enum    | `0`, `1`, `2` |
| `Mute`       | Mute timer      | Numeric | minutes; `0` = no timeout |
| `Logout`     | Logout          | None    | log out of the console |
| `Logouttime` | Auto-logout     | Numeric | minutes |
| `Password` ⚠ | Change password | Text    | sets a new login password |
| `Modbus`     | Modbus          | Text    | `<address> <baud>` (addr 1–127; baud 0=9600, 1=19200, 2=38400, 3=115200) |
| `location`   | Location        | Text    | use `\s` for spaces, e.g. `Ward\s10` |
| `settings`   | Settings        | None    | list system settings |
| `Logtime`    | Log interval    | Numeric | minutes between log entries |
| `Logdump`    | Log dump        | None    | list the log data |
| `Logclear` ⚠ | Log clear       | None    | clears the log data |
| `Factory` ⚠  | Factory reset   | None    | reset to factory defaults |
| `reboot` ⚠   | Reboot          | None    | reboot the encoder |
| `help`       | Help (list)     | None    | list general commands |
| `gashelp`    | Gas help (list) | None    | list gas commands |

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
