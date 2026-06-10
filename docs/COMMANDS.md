# SAX-D Command Reference

This is the full command set from the SAX-D manual, expressed the way the
firmware models it. Each row maps to a `CommandDef` entry in
`src/commands.cpp`. The scaffold wires up a representative subset (marked
**[wired]**); the rest drop in by adding table entries — no other code changes.

The firmware assembles the line as:

- **Gas commands:** `Gas <channel> <token> [param]` where `<channel>` is `1`–`6`
  or `a` (all).
- **General commands:** `<token> [param]`.

Text escapes used by the encoder: `\n` = newline inside a string, `\s` = a
space (used by `location`).

---

## Gas settings (`gas` category, `needsChannel = true`)

| Token | Label | Param | Notes | Status |
|-------|-------|-------|-------|--------|
| `Name`     | Name              | Text    | gas name string, e.g. `Oxygen`        | **[wired]** |
| `normal`   | Normal text       | Text    | normal-state string, e.g. `normal`    | todo |
| `High`     | High text         | Text    | high-pressure string, e.g. `high\npressure` | todo |
| `drops`    | Drop text         | Text    | pressure-drop string, e.g. `pressure\ndrop` | todo |
| `Low`      | Low text          | Text    | low-pressure string, e.g. `low\npressure`   | todo |
| `Fault`    | Fault text        | Text    | fault string, e.g. `fault`            | todo |
| `Hi_set`   | Hi pressure set   | Numeric | hi alarm above value (bar), e.g. `8.4`| **[wired]** |
| `Pd_set`   | Pressure drop set | Numeric | pressure-drop alarm (bar), e.g. `7.8` | **[wired]** |
| `Lo_set`   | Lo pressure set   | Numeric | lo alarm below value (bar), e.g. `7.6`| **[wired]** |
| `list`     | List settings     | None    | display settings for this gas         | **[wired]** |
| `on`       | Enable            | None    | enable this gas                       | **[wired]** |
| `off`      | Disable           | None    | disable this gas                      | **[wired]** |
| `Press_on` | Pressure on       | None    | display pressure                      | todo |
| `Press_off`| Pressure off      | None    | do not display pressure               | todo |
| `Press_al` | Pressure in alarm | None    | show pressure only in alarm/warning   | todo |
| `Press_eng`| Pressure on test  | None    | show pressure only when test pressed  | todo |
| `type`     | Type (defaults)   | Enum    | load gas-type defaults (see below)    | **[wired]** |
| `units`    | Units             | Enum    | `Bar`, `mmHg`, `PSI`, `other`         | todo |
| `cal`      | Calibrate point   | Numeric | update reading calibration point      | todo |
| `atm`      | Atmosphere zero   | Numeric | update zero (atmosphere) calibration  | todo |

**Gas `type` enum values:** `USER`, `O2`, `N2O`, `ENT`, `MA_4`, `MA_7`,
`SA_7`, `VAC`, `N2`, `CO2`, `BOT_230`, `BOT_250`.

---

## General settings (`general` category, `needsChannel = false`)

| Token | Label | Param | Notes | Status |
|-------|-------|-------|-------|--------|
| `Tone`       | Tone           | Enum    | `0`, `1`, `2`                          | **[wired]** |
| `Mute`       | Mute timer     | Numeric | minutes; `0` = no timeout              | **[wired]** |
| `Logout`     | Logout         | None    | log out of the console                 | todo |
| `Logouttime` | Auto-logout    | Numeric | minutes                                | todo |
| `Password`   | Change password| Text    | sets a new login password              | todo |
| `Modbus`     | Modbus         | Text    | `<address> <baud>` (addr 1–127; baud 0=9600,1=19200,2=38400,3=115200) | todo |
| `location`   | Location       | Text    | use `\s` for spaces, e.g. `Ward\s10`   | **[wired]** |
| `settings`   | Settings       | None    | list system settings                   | **[wired]** |
| `Logtime`    | Log interval   | Numeric | minutes between log entries            | todo |
| `Logdump`    | Log dump       | None    | list the log data                      | todo |
| `Logclear`   | Log clear      | None    | clears the log data                    | todo |
| `Factory`    | Factory reset  | None    | reset to factory defaults              | todo |
| `Reboot`     | Reboot         | None    | reboot the encoder                     | **[wired]** |
| `help`       | Help           | None    | list commands                          | todo |
| `gashelp`    | Gas help       | None    | list gas commands                      | todo |

> **Note on destructive commands.** `Factory`, `Logclear`, `Password` and
> friends are flagged with `*` in the manual (they require confirmation). When
> wiring these up, consider an extra on-device confirmation step before sending.

---

## How to add a command

In `src/commands.cpp`, add a `CommandDef` to `kGasCommands` or
`kGeneralCommands`:

```cpp
{"gas_normal", "Normal text", "normal", /*needsChannel=*/true,
 ParamType::Text, "e.g. normal", nullptr, 0, "Normal-state text string"},
```

For an enum parameter, define an `EnumOption[]` and point `enumOpts`/`enumCount`
at it (see `kGasTypes`). That's all — the menus, channel prompt and parameter
entry are generated from the table.
