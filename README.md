# SAXBLE

A handheld tool for commissioning **SAX-D local alarms**, built for the
**M5Stack Cardputer ADV**. Instead of pairing a phone with a generic *BLE
Terminal* app and hand-typing commands, you connect, log in and drive the
encoder's command-line interface from baked-in menus: pick a command, fill in
only the parameters it accepts, send.

> **Status: verified on hardware** against SAX-D firmware **`V002_RC302`**
> (RN4870 V1.40). BLE connect, auto-login, the **full command set** (gas +
> general), parameter entry, destructive-command Y/N auto-confirm, graceful
> reconnect-after-reboot, and microSD capture into per-device folders are all
> working. See [`docs/COMMANDS.md`](docs/COMMANDS.md) and
> [`docs/TEST_PLAN.md`](docs/TEST_PLAN.md).

> **Experimental — M5Stack Tab5 port:** bring-up tests for an ESP32-P4 Tab5
> (5" touchscreen, BLE via the on-board ESP32-C6) live in
> [`tab5/`](tab5/README.md) — a touchscreen test and a BLE-central spike to the
> encoder. These are separate firmwares with their own toolchain; the long-term
> plan is a touch UI plus a live dashboard fed by the encoder's RS485 output.

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

On boot the firmware **scans and lists nearby devices** so you can pick the
right encoder (handy when several are in range) — devices advertising the UART
service are marked `*` and each shows its signal strength. After you select one
it connects, subscribes to notifications and, if **auto-login** is on, sends the
saved password. The encoder replies *"Welcome to Shire SAX…"*, which lights the
**AUTH** indicator. **Disconnect** or **Logout** drops the link and returns to
the scan list.

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
- **Commissioning log** — every command and reply is **auto-saved to microSD as
  it happens** (no manual export). Each connection writes a timestamped file in a
  **per-device folder** (`/saxble/<device>_<id>/session_####.txt`, with the
  device name + address in the header) so each encoder's records stay together.
- **Device registry** — every device you connect to is recorded once (name +
  address) in `/saxble/devices.txt` for traceability.
- **Presets** — one menu pick runs a whole sequence (e.g. set gas types, enable
  pressure display, set the password, prompt for the location), waiting for each
  `OK` and auto-answering any `Y or N`. Edit the list in `src/presets.cpp`.
- **Clock** — set the date/time once (Settings → Set date & time) and the logs
  switch from power-on-relative to real timestamps for the commissioning report.
- **On-device settings** — toggle auto-login / write-with-response, set a device
  name filter, rescan, set the clock, reset to defaults.

## Getting the logs off

Pop the **microSD card** into a card reader and copy the **`/saxble`** folder —
it contains one subfolder per device (session logs) plus `devices.txt`. (USB
Mass Storage was tried but the SD-over-SPI path wouldn't mount reliably; the
card reader is the dependable route and keeps flashing one-click.)

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

If the board isn't detected, hold the Cardputer's **G0** button while plugging
in to force the bootloader, then upload.

### From the command line

If you prefer the terminal (PlatformIO Core installed):

```bash
pio run                 # build
pio run -t upload       # flash over USB
pio device monitor      # serial log @ 115200
```

### M5Stack Tab5 tests (experimental)

The Tab5 bring-up tests are a **separate** PlatformIO project — different MCU
(ESP32-P4), toolchain and libraries — so they have their own `platformio.ini`
inside [`tab5/`](tab5/README.md). You don't point PlatformIO at a branch; git
handles branches, and PlatformIO just opens the folder that holds the
`platformio.ini` you want to build.

1. Get the code onto your machine:
   ```bash
   git fetch origin
   git checkout claude/sax-d-alarm-setup-device-gc00k9
   git pull
   ```
2. In VS Code, **File → Open Folder…** and pick the **`tab5`** folder — *not*
   the SAXBLE root (the root is the Cardputer project). PlatformIO then shows two
   environments: `tab5-touch-test` and `tab5-ble-test`.
3. Plug in the Tab5 over USB-C and flash one (toolbar Upload, or from inside
   `tab5/`):
   ```bash
   pio run -e tab5-touch-test -t upload && pio device monitor   # start here
   pio run -e tab5-ble-test   -t upload && pio device monitor   # the BLE spike
   ```

Only one firmware lives on the board at a time. The first build downloads the
ESP32-P4 toolchain (a few hundred MB) — that's normal and one-time. See
[`tab5/README.md`](tab5/README.md) for what each test should show and how to
read a failure (the BLE-via-C6 path is the genuine unknown).

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
| `src/ui.{h,cpp}` | menus, parameter entry, log view, settings |
| `docs/COMMANDS.md` | full SAX-D command reference + how to extend |

## Roadmap

- Verify on hardware (first compile + flash on a real Cardputer ADV).
- Confirm exact command token spelling/casing against a live encoder.
- Optional real-time-clock for wall-clock timestamps in the report.
- On-device UUID editing screen (currently editable via name filter + defaults).
