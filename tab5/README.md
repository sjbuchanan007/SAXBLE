# SAXBLE — M5Stack Tab5 bring-up tests

Two minimal, independent firmwares to de-risk a Tab5 port of SAXBLE **before**
porting the full app. The Tab5 is an **ESP32-P4** (no built-in radio) with an
**ESP32-C6** co-processor for BLE/Wi-Fi over ESP-Hosted, and a 5" 1280×720
capacitive touchscreen — both are new ground compared to the Cardputer.

| Test | Env | Proves | Risk |
|------|-----|--------|------|
| Touchscreen | `tab5-touch-test` | 5" panel + multi-touch + M5GFX drawing | low |
| BLE central | `tab5-ble-test` | P4→C6 BLE central can talk to the SAX-D encoder | **the real unknown** |

> The eventual live dashboard is planned to read the encoder's **RS485** output
> directly (separate wire), so the BLE side only needs to do command/login work —
> this spike just confirms that path exists on the P4/C6.

## Build & flash

From this `tab5/` directory:

```bash
# Touchscreen test (start here — it's the safe one)
pio run -e tab5-touch-test -t upload
pio device monitor

# BLE central test
pio run -e tab5-ble-test -t upload
pio device monitor
```

The first build downloads the **pioarduino** ESP32-P4 toolchain (a few hundred
MB) — the official PlatformIO `espressif32` platform doesn't carry the P4 yet.

## Touch test — what to expect
Title bar + a **CLEAR** button, a coloured dot tracking each finger (up to 5),
a live finger/coordinate readout, and the same coords on serial. If touches
register but X/Y look swapped or mirrored, that's just orientation — change
`setRotation()` in `src/touch_test.cpp`; the panel is working.

## BLE test — what to expect
It auto-scans, lists nearby devices with a live "seen" count, matches the
encoder by name (`kTargetName`, default `mmssjbt1`) or its ISSC service UUID,
connects, subscribes, logs in (sends `MMSmms659` char-by-char with CR+LF on the
`Password:` prompt), and on `Welcome to Shire` sends `gas a list`. Everything is
shown on screen and on serial; the header also shows battery (`CHG/BAT nn%`).
**Tap the screen** to force a rescan.

## Status (hardware bring-up, June 2026)

| Stage | Result |
|-------|--------|
| Display + multi-touch (touch test) | ✅ works |
| BLE init via P4→C6 (ESP-Hosted) | ✅ works (after SDIO-pin fix) |
| Scan + find the encoder | ✅ works (active scan, name match) |
| **Connect / login** | ⛔ **blocked by outdated C6 firmware** |

**The remaining blocker:** every boot prints `Version on Host is NEWER than
version on co-processor`. The C6's ESP-Hosted firmware is older than the Arduino
core (3.3.9) expects, so the connection handshake fails — the radio link
half-establishes (`Client busy, connected … id=0`) but `connect()` returns
`status=2` and the chip resets. Power was ruled out (battery 100%). The fix is
to **update the C6 firmware**; everything else is proven working.

### Resuming: update the C6 firmware
The Tab5's USB-C port is wired to the **P4**, not the C6 — esptool over USB-C
reports *"not an ESP32-P4 image … expected 18 but value was 13"* (18 = P4,
13 = C6). **Do not `--force`** that; it would write a C6 image onto the P4.

The C6 is flashed via the **reserved download header on the PCB with a 3.3 V
USB-TTL adapter** (M5Stack's *ESP32 Downloader* matches the pinout). Then, in
M5Burner: burn **Tab5 ESP32-C6 Wi-Fi SDIO v2.12.6** — long-press reset until the
internal green LED flashes fast, select the **adapter's** serial port (not the
P4 `usbmodem` port), Start. See M5Stack's
[C6 firmware-restore guide](https://docs.m5stack.com/en/guide/restore_factory/m5tab5_c6_wifi).
After flashing, reflash this test and confirm the version banner is gone and the
connect reaches `connected → Password:`.

### Fixes already baked into the sketch (the journey here)
- **SDIO pins**: `WiFi.setPins(12,13,11,10,9,8,15)` before `BLEDevice::init()` —
  without it the P4 can't reach the C6 (`sdmmc … 0x107`, crash in
  `ble_transport_ll_init`). The generic `esp32-p4-evboard` defaults are wrong.
- **Core's bundled BLE stack** (`BLEDevice.h`), not NimBLE-Arduino, which has
  failed to compile on the P4
  ([#906](https://github.com/h2zero/NimBLE-Arduino/issues/906)); the core stack
  got the P4/ESP-Hosted fixes (Tab5 BLE tracked in arduino-esp32
  [#12324](https://github.com/espressif/arduino-esp32/issues/12324)).
- **Active scan + name match**: the encoder's 128-bit service UUID and name ride
  in the scan response, so passive scan never matched it.
- **Scan dedupe + `CORE_DEBUG_LEVEL=2`**: a noisy RF environment otherwise
  floods the log; there's also a known P4+C6 active-scan stall after ~60–90s
  ([esp-hosted-mcu #180](https://github.com/espressif/esp-hosted-mcu/issues/180)),
  but we connect within seconds so it doesn't bite.
- **Connect handling**: stop the scan before connecting (NimBLE won't connect
  while scanning → `status=2`), trust `isConnected()` over the return code, and
  retry with cleanup. (Still defeated by the C6-firmware mismatch above.)
- **Display init retry** for the racy Tab5 panel detection, and dimmed backlight.

These cannot be compiled or tested in the dev container (no network for the
toolchain, no hardware) — same workflow as the Cardputer: flash, watch serial,
report back, iterate.

## Notes
- Board `esp32-p4-evboard`: the Tab5 has no dedicated PlatformIO board yet; the
  generic P4 definition matches the chip and **M5Unified auto-detects the Tab5**
  at runtime.
- This is separate from the Cardputer firmware in the repo root — different MCU,
  toolchain, and libraries, so it has its own `platformio.ini`.
