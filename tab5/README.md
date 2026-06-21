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
It auto-scans for the encoder's ISSC transparent-UART service, connects,
subscribes, logs in (sends `MMSmms659` char-by-char with CR+LF on the
`Password:` prompt), and on `Welcome to Shire` sends `gas a list`. Everything
the encoder says is shown on screen and on serial. **Tap the screen** to force
a rescan.

### If the BLE test fails (and how to read it)
This is the test most likely to need iteration. Known P4/C6 facts baked into
the sketch and what to try:

- **`sdmmc send_op_cond ... 0x107` / `card init failed`, then a crash in
  `ble_transport_ll_init`** → the P4 can't reach the C6 over SDIO because the
  *pins* are wrong. The Tab5 has no dedicated PlatformIO board, so we build as
  `esp32-p4-evboard`, whose default SDIO pins differ from the Tab5. The sketch
  fixes this with `WiFi.setPins(12,13,11,10,9,8,15)` (clk,cmd,d0,d1,d2,d3,rst)
  **before** `BLEDevice::init()` — both BLE and Wi-Fi share that one SDIO link.
  This was the first real blocker we hit and it's now handled.
- **Link comes up but `Slave firmware version: 0.0.0` / version mismatch** →
  the C6's ESP-Hosted firmware needs refreshing to match the core. Use M5Burner
  or M5Stack's [C6 firmware-restore guide](https://docs.m5stack.com/en/guide/restore_factory/m5tab5_c6_wifi).
  Try the pin fix first — the version read only succeeds once the link is up.
- **Uses the core's bundled BLE stack (`BLEDevice.h`), not NimBLE-Arduino** —
  NimBLE-Arduino has historically failed to compile on the P4
  ([#906](https://github.com/h2zero/NimBLE-Arduino/issues/906)); the core stack
  is the one that received the P4/ESP-Hosted fixes (BLE on Tab5 tracked in
  arduino-esp32 [#12324](https://github.com/espressif/arduino-esp32/issues/12324)).
- **Scan is PASSIVE on purpose** — active scanning on P4+C6 has a known bug
  where advertising reports stop after ~60–90s
  ([esp-hosted-mcu #180](https://github.com/espressif/esp-hosted-mcu/issues/180)).
  Passive scan is steadier; we match the encoder by **service UUID** (we may not
  get its name, which rides in the active-scan response).

These cannot be compiled or tested in the dev container (no network for the
toolchain, no hardware) — same workflow as the Cardputer: flash, watch serial,
report back, iterate.

## Notes
- Board `esp32-p4-evboard`: the Tab5 has no dedicated PlatformIO board yet; the
  generic P4 definition matches the chip and **M5Unified auto-detects the Tab5**
  at runtime.
- This is separate from the Cardputer firmware in the repo root — different MCU,
  toolchain, and libraries, so it has its own `platformio.ini`.
