# SAXBLE

A handheld tool for commissioning **SAX-D local alarms**, built for the
**M5Stack Cardputer ADV**. Instead of pairing a phone with a generic *BLE
Terminal* app and hand-typing commands, you connect, log in and drive the
encoder's command-line interface from baked-in menus: pick a command, fill in
only the parameters it accepts, send.

> **Status: scaffold.** The architecture, BLE connection, login flow, parameter
> entry and microSD export are complete and a representative slice of the
> command set is wired end-to-end. The remaining commands are documented in
> [`docs/COMMANDS.md`](docs/COMMANDS.md) and drop straight into one table.

## Why the Cardputer ADV

It's the right tool for the job: ESP32-S3 with built-in BLE, a 56-key keyboard,
a 1.14" screen, a big battery and — importantly for the commissioning report —
a microSD slot. It acts as a BLE **central** (client), exactly the role the
phone app plays.

## How it works

The SAX-D encoder presents its serial console over BLE using the **Nordic UART
Service (NUS)** — the standard that *BLE Terminal*-style apps speak:

| Role | UUID |
|------|------|
| Service | `6E400001-B5A3-F393-E0A9-E50E24DCCA9E` |
| Write (commands → encoder) | `6E400002-…` (Write Without Response) |
| Notify (responses ← encoder) | `6E400003-…` |

On connect the firmware subscribes to notifications and, if **auto-login** is
on, sends the saved password. The encoder replies *"Welcome to Shire SAX
Command Line Interface"*, which the firmware detects to light the **AUTH**
indicator.

These UUIDs, the device-name filter and the write mode are all editable
on-device (**Settings**) and saved to flash — so if the encoder turns out to
use different UUIDs, you don't need to reflash.

## Features

- **Menu-driven commands** — browse *Gas Settings* / *General Settings*, choose
  a command, then enter only the valid parameters (gas channel `1`–`6`/`a`,
  numeric setpoints, text strings, or a pick-list for things like gas type).
- **Password manager** — keeps a list of "popular passwords" (preloaded with the
  documented default `studio3`). Pick one to log in, or type a new one and it's
  saved for next time.
- **Commissioning log** — every command sent and every reply received is
  captured with a timestamp. **Export to microSD** writes a text file
  (`/saxble/session_####.txt`) you can drop into a commissioning report.
- **On-device settings** — toggle auto-login / write-with-response, set a device
  name filter, rescan, reset to defaults.

## Controls (Cardputer keyboard)

| Key | Action |
|-----|--------|
| `;` / `.` | up / down |
| `,` / `/` | left / right |
| `Enter` | select / send |
| `` ` `` | back / cancel |
| `Del` | backspace (text entry) |
| `c` | clear log (Session Log screen) |

## Build & flash

Requires [PlatformIO](https://platformio.org/).

```bash
pio run                 # build
pio run -t upload       # flash over USB
pio device monitor      # serial log @ 115200
```

The first build downloads the dependencies pinned in
[`platformio.ini`](platformio.ini): `M5Cardputer` and `NimBLE-Arduino`.

> **Cardputer ADV notes.** The ADV is new (2026) and uses the Stamp **S3A**.
> If the keyboard/display init misbehaves, update `M5Unified`/`M5GFX` to their
> latest releases. The microSD pins are isolated in
> [`src/session_log.cpp`](src/session_log.cpp) — verify them against the ADV
> schematic if export reports "No SD card".

## Project layout

| File | Responsibility |
|------|----------------|
| `src/main.cpp` | startup + wiring; auto-login on connect |
| `src/config.{h,cpp}` | persistent settings (UUIDs, passwords, flags) in NVS |
| `src/commands.{h,cpp}` | declarative command tables + line builder |
| `src/ble_uart.{h,cpp}` | BLE central: scan, connect, notify, write |
| `src/session_log.{h,cpp}` | session capture + microSD export |
| `src/ui.{h,cpp}` | menus, parameter entry, log view, settings |
| `docs/COMMANDS.md` | full SAX-D command reference + how to extend |

## Roadmap

- Wire up the remaining commands from `docs/COMMANDS.md`.
- Confirmation prompts for destructive commands (`Factory`, `Logclear`, …).
- Optional real-time-clock for wall-clock timestamps in the report.
- On-device UUID editing screen (currently editable via name filter + defaults).
