# SAXBLE

A handheld tool for commissioning **SAX-D local alarms**, built for the
**M5Stack Cardputer ADV**. Instead of pairing a phone with a generic *BLE
Terminal* app and hand-typing commands, you connect, log in and drive the
encoder's command-line interface from baked-in menus: pick a command, fill in
only the parameters it accepts, send.

> **Status: feature-complete firmware, untested on hardware.** BLE connection,
> login flow, the **full command set**, parameter entry, microSD capture and
> **Wi-Fi log export** are all implemented. It has not yet been compiled or run
> on a physical Cardputer ADV — `pio run` on your machine is the first real
> validation. See [`docs/COMMANDS.md`](docs/COMMANDS.md) for the command list.

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

On boot the firmware **scans and lists nearby Bluetooth devices** so you can
pick the right encoder (handy when several are in range) — devices advertising
the UART service are marked `*` and each shows its signal strength. After you
select one it connects, subscribes to notifications and, if **auto-login** is
on, sends the saved password. The encoder replies *"Welcome to Shire SAX
Command Line Interface"*, which the firmware detects to light the **AUTH**
indicator. You can return to the picker any time via **Bluetooth (connect)** on
the menu.

These UUIDs, the device-name filter and the write mode are all editable
on-device (**Settings**) and saved to flash — so if the encoder turns out to
use different UUIDs, you don't need to reflash.

## Features

- **Device picker** — boots into a live scan of nearby Bluetooth devices; choose
  the encoder by name/signal strength before connecting, so you log into the
  right unit when several are close together.
- **Menu-driven commands** — browse *Gas Settings* / *General Settings*, choose
  a command, then enter only the valid parameters (gas channel `1`–`6`/`a`,
  numeric setpoints, text strings, or a pick-list for things like gas type).
  The **full SAX-D command set** is wired up.
- **Destructive-command guard** — `Factory`, `Logclear`, `Password`, `reboot`,
  `cal` and `atm` require a two-step confirm (ENTER to arm, ENTER again to send)
  so they can't be fired by accident.
- **Password manager** — keeps a list of "popular passwords" (preloaded with the
  documented default `studio3`). Pick one to log in, or type a new one and it's
  saved for next time.
- **Commissioning log** — every command sent and every reply received is
  captured with a timestamp. **Export to microSD** writes a text file into a
  **per-device folder** (`/saxble/<device>_<id>/session_####.txt`, with the
  device name + address in the header) so each encoder's records stay together.
- **Device registry** — every device you connect to is recorded once (name +
  address) in `/saxble/devices.txt` for traceability.
- **Wi-Fi log export** — the Cardputer brings up its own Wi-Fi hotspot and a web
  page; connect a phone/laptop and download the logs in a browser, no card
  removal or cable needed (see below).
- **On-device settings** — toggle auto-login / write-with-response, set a device
  name filter, rescan, reset to defaults.

## Wi-Fi log export

From the home menu choose **Wi-Fi Log Export**. The screen shows an SSID
(`SAXBLE-Setup`), a password (`saxble1234`) and a URL (`http://192.168.4.1/`).
Join that Wi-Fi from a phone or laptop, open the URL, and you get a page listing
every log on the SD card with **download** / **view** links, plus a button to
save the current in-progress session first.

Because Wi-Fi and BLE share the single radio, entering this screen **pauses the
BLE link**; pressing back stops the hotspot and resumes scanning. So the normal
flow is: do your commissioning over BLE, then switch to Wi-Fi to collect the
report. (SSID/password are editable in `src/config.cpp`.)

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

### First time with PlatformIO

PlatformIO does the fiddly part for you — it reads
[`platformio.ini`](platformio.ini) and installs the **exact** library versions
this firmware needs (in particular NimBLE-Arduino **1.4.x**; the 2.x line
changed APIs and would not compile). One-time setup:

1. Install [Visual Studio Code](https://code.visualstudio.com/).
2. In VS Code, open **Extensions** (the blocks icon), search **PlatformIO IDE**,
   and install it. It bundles everything (no separate Python/toolchain install).
3. **File → Open Folder…** and pick this `SAXBLE` folder.
4. Plug in the Cardputer ADV over USB-C. On the PlatformIO toolbar at the bottom:
   - **✓** builds,
   - **→** builds **and** uploads (flashes),
   - **🔌** opens the serial monitor (115200 baud).

   The first build downloads the ESP32 toolchain and the pinned libraries
   (`M5Cardputer`, `NimBLE-Arduino`) automatically — give it a few minutes.

If the board isn't detected, hold the Cardputer's **G0** button while plugging in
to force the bootloader, then upload.

### From the command line

If you prefer the terminal (PlatformIO Core installed):

```bash
pio run                 # build
pio run -t upload       # flash over USB
pio device monitor      # serial log @ 115200
```

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
| `src/ble_uart.{h,cpp}` | BLE central: scan, connect, notify, write, pause/resume |
| `src/session_log.{h,cpp}` | session capture + microSD export |
| `src/wifi_portal.{h,cpp}` | Wi-Fi hotspot + web page for downloading logs |
| `src/ui.{h,cpp}` | menus, parameter entry, log view, settings |
| `docs/COMMANDS.md` | full SAX-D command reference + how to extend |

## Roadmap

- Verify on hardware (first compile + flash on a real Cardputer ADV).
- Confirm exact command token spelling/casing against a live encoder.
- Optional real-time-clock for wall-clock timestamps in the report.
- On-device UUID editing screen (currently editable via name filter + defaults).
- Optional USB Mass Storage mode (expose the SD card as a USB drive).
