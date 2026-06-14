# SAXBLE — Command Test Plan

Work through this **top to bottom**: safe/read-only commands first, disruptive
and destructive ones last. Every command and the encoder's reply is captured in
the **Session Log** (and written to the SD card per device), so the simplest way
to record results is:

1. Connect + log in.
2. Run the commands in order.
3. Pull the SD card (or read the on-device Session Log) and send the log back —
   the `<<` replies show what passed.

You can also tick the boxes here as you go.

## Before you start

- **Record a baseline** so you can restore anything you change:
  - `Gas Settings → List settings → a` (dumps all gases) — note it / keep the log.
  - `General Settings → Settings` — **look for a firmware/version string** and
    send it to me.
- This is a **bench demo unit** (not a live hospital panel), so you can test
  everything freely — including the destructive commands and `factory`. The
  write tests below use **channel 4** (`X` = 4) just to keep the configured
  gases 1–3 intact, but on a demo unit any channel is fine.
- **`⚠` = destructive / disruptive** — these need the two-step confirm on the
  device (ENTER to arm, ENTER again to send) and are grouped at the end.

Menu path reminder: `Gas Settings → <command> → <channel> → <value>` and
`General Settings → <command> → <value>`.

---

## Phase 1 — Read-only (no changes)

| # | Do this | Sends | Expect | Pass |
|---|---------|-------|--------|:----:|
| 1 | General → Help | `help` | list of general commands | [ ] |
| 2 | General → Gas help | `help gas` | list of gas commands | [ ] |
| 3 | General → Settings | `settings` | system settings (note firmware version) | [ ] |
| 4 | Gas → List settings → `a` | `gas a list` | full dump of all 6 gases | [ ] |
| 5 | Gas → List settings → `1` | `gas 1 list` | single-gas dump | [ ] |

## Phase 2 — Tone & mute (safe, easily restored)

| # | Do this | Sends | Expect | Pass |
|---|---------|-------|--------|:----:|
| 6 | General → Tone → `Tone 1` | `tone 1` | `OK`, tone changes | [ ] |
| 7 | General → Tone → `Tone 0` | `tone 0` | `OK` (restore) | [ ] |
| 8 | General → Mute timer → `10` | `mute 10` | `OK` (check in Settings) | [ ] |
| 9 | General → Mute timer → `0` | `mute 0` | `OK` (restore) | [ ] |

## Phase 3 — Gas enable & pressure display (spare channel `X` = 4)

| # | Do this | Sends | Expect | Pass |
|---|---------|-------|--------|:----:|
| 10 | Gas → Enable → `4` | `gas 4 on` | `OK`; list shows `Condition: on` | [ ] |
| 11 | Gas → Pressure: show → `4` | `gas 4 press_on` | `OK`; Display = pressure on always | [ ] |
| 12 | Gas → Pressure: in alarm → `4` | `gas 4 press_al` | `OK`; Display changes | [ ] |
| 13 | Gas → Pressure: on test → `4` | `gas 4 press_eng` | `OK`; Display changes | [ ] |
| 14 | Gas → Pressure: hide → `4` | `gas 4 press_off` | `OK`; Display changes | [ ] |
| 15 | Gas → Disable → `4` | `gas 4 off` | `OK`; `Condition: off` | [ ] |

*(re-enable with `gas 4 on` before the next phases so the list shows the fields)*

## Phase 4 — Gas text strings (spare channel `X` = 4)

Verify each with `gas 4 list` — check the text, and that `\n` shows as a line break.

| # | Do this | Sends | Expect | Pass |
|---|---------|-------|--------|:----:|
| 16 | Gas → Name → `4` → `TestGas` | `gas 4 name TestGas` | `OK` | [ ] |
| 17 | Gas → Normal text → `4` → `Normal` | `gas 4 normal Normal` | `OK` | [ ] |
| 18 | Gas → High text → `4` → `High\nPressure` | `gas 4 high High\nPressure` | `OK`; wraps to 2 lines | [ ] |
| 19 | Gas → Drop text → `4` → `Pressure\nDrop` | `gas 4 drop Pressure\nDrop` | `OK` | [ ] |
| 20 | Gas → Low text → `4` → `Low\nPressure` | `gas 4 low Low\nPressure` | `OK` | [ ] |
| 21 | Gas → Fault text → `4` → `Signal\nFault` | `gas 4 fault Signal\nFault` | `OK` | [ ] |

## Phase 5 — Setpoints & differentials (spare channel `X` = 4)

Verify with `gas 4 list` (High/Drop/Low Alarm + Hi/Pd/Lo_Diff).

| # | Do this | Sends | Expect | Pass |
|---|---------|-------|--------|:----:|
| 22 | Gas → Hi pressure set → `4` → `5.6` | `gas 4 hi_set 5.6` | `OK` | [ ] |
| 23 | Gas → Pressure drop set → `4` → `3.55` | `gas 4 pd_set 3.55` | `OK` | [ ] |
| 24 | Gas → Lo pressure set → `4` → `3.35` | `gas 4 lo_set 3.35` | `OK` | [ ] |
| 25 | Gas → Hi differential → `4` → `0.20` | `gas 4 hi_diff 0.20` | `OK` | [ ] |
| 26 | Gas → Drop differential → `4` → `0.10` | `gas 4 pd_diff 0.10` | `OK` | [ ] |
| 27 | Gas → Lo differential → `4` → `0.20` | `gas 4 lo_diff 0.20` | `OK` | [ ] |

## Phase 6 — Units & type (spare channel `X` = 4)

| # | Do this | Sends | Expect | Pass |
|---|---------|-------|--------|:----:|
| 28 | Gas → Units → `4` → `PSI` | `gas 4 units PSI` | `OK`; list Units = PSI | [ ] |
| 29 | Gas → Units → `4` → `Bar` | `gas 4 units Bar` | `OK` (restore) | [ ] |
| 30 | Gas → Type → `4` → `N2` | `gas 4 type N2` | `OK`; loads N2 defaults (overwrites ch.4) | [ ] |

*(also worth trying one type with the manual's old casing to confirm
case-insensitivity, e.g. step 30 vs `n2`)*

## Phase 7 — General config (note the effects)

| # | Do this | Sends | Expect | Pass |
|---|---------|-------|--------|:----:|
| 31 | General → Location → `Ward\s10` | `location Ward\s10` | `OK`; Settings shows `Ward 10` | [ ] |
| 32 | General → Engineer → `Test\sEng` | `engineer Test\sEng` | `OK`; Settings `Engineer No` updates *(verify)* | [ ] |
| 33 | General → Screen saver → `2` | `screensave 2` | `OK`; Settings `Screen saver: 2 Minutes` | [ ] |
| 34 | General → Auto-logout → `15` | `logouttime 15` | `OK` (check Settings) | [ ] |
| 35 | General → Log interval → `15` | `logtime 15` | `OK` (check Settings) | [ ] |
| 36 | General → Log dump | `logdump` | log data (may be long) | [ ] |
| 37 | General → Modbus → `1 1` | `modbus 1 1` | `OK`; changes Modbus addr/baud | [ ] |

## Phase 8 — Calibration ⚠ (spare channel `4` only; affects readings)

> Only run these if you understand the effect — they change the pressure
> reading scaling/zero. Record the originals from your baseline dump first.

| # | Do this | Sends | Expect | Pass |
|---|---------|-------|--------|:----:|
| 38 ⚠ | Gas → Calibrate point → `4` → `<known>` | `gas 4 cal <value>` | `OK` | [ ] |
| 39 ⚠ | Gas → Atmosphere zero → `4` → `0` | `gas 4 atm 0` | `OK` | [ ] |

## Phase 9 — Destructive / session (LAST)

> Do these last and deliberately. Each needs the two-step confirm.

| # | Do this | Sends | Expect | Pass |
|---|---------|-------|--------|:----:|
| 40 ⚠ | General → Log clear | `logclear` | `OK`; log emptied (verify `logdump`) | [ ] |
| 41 ⚠ | General → Change password → `studio3` | `password studio3` | `OK`; **keep it the same** so auto-login still works | [ ] |
| 42 | General → Logout (in menu = Disconnect) | `logout` | session ends, returns to scan list | [ ] |
| 43 ⚠ | General → Reboot | `reboot` | encoder reboots; link drops; back to scan | [ ] |
| 44 ⚠ | General → Factory reset | `factory` | **wipes all config** — only if you intend to reconfigure | [ ] |

## After testing — restore

- Re-run `gas 4 list` / `Settings` and compare to the baseline; reset channel 4
  to `off` and restore anything you changed (or `factory` + reconfigure — fine
  on a demo unit).
- Pull the SD card and send me the session log(s) — I'll review every reply and
  fix any command that didn't behave.

## Things to watch for / report

- Any reply other than `OK` / a sensible value dump (e.g. `Invalid command`,
  nothing at all, or a garbled response).
- Whether `\n` in text strings renders as a line break in `list`.
- Whether gas `type` accepts both `N2` and `n2` (case sensitivity).
- Whether `engineer` and `screensave` behave (both now wired; `engineer` syntax
  is a best guess from the `Engineer No` field — confirm it's right).
