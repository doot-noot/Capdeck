# CapDeck — notes for AI coding assistants (and humans)

Custom Arduino firmware for the **M5Stack Cardputer ADV** (ESP32-S3FN8, 8 MB flash, no PSRAM, 240x135 ST7789,
TCA8418 keyboard, ES8311 audio) with the optional **Cap LoRa-1262** (SX1262 + ATGM336H GPS). Distributed for
M5Launcher (app `.bin`) and as a full 0x0 image.

## Build

- Core: **esp32 by Espressif 3.3.x** (built with 3.3.12), board `esp32:esp32:m5stack_cardputer`,
  options `PartitionScheme=huge_app,FlashSize=8M`.
- Libraries (versions used for 2.0.0):
  M5Cardputer 1.1.1 · M5Unified 0.2.29 · M5GFX 0.2.36 · **RadioLib 7.8.1 (tag; master has a different `begin()` API)** ·
  IRremoteESP8266 2.9.0 · rc-switch 2.6.4 · TinyGPSPlus 1.1.0 · ArduinoJson 7.4.2 · WebSockets (links2004) 2.7.3
- Command:
  ```
  arduino-cli compile --fqbn "esp32:esp32:m5stack_cardputer:PartitionScheme=huge_app,FlashSize=8M" \
    --output-dir build source/CapDeck
  ```
  `build/CapDeck.ino.bin` is the M5Launcher image, `build/CapDeck.ino.merged.bin` is the full 0x0 image.

## Layout (`source/CapDeck/`)

| File | Role |
|------|------|
| `CapDeck.ino` | app table, carousel + theme backdrops, boot sequence |
| `core.h` | pins, runtime palette (`col::`), shared structs/APIs |
| `ui.cpp` | themes, input (`readKey`), status bar, list menu, text input, popups, chat view |
| `icons.cpp` | vector icons on a 64-unit grid (`drawIcon`) |
| `system.cpp` | NVS settings, `config.json`, Wi-Fi state machine, clock, GPS, LoRa radio + modes, `sysTick()` |
| `mesh.cpp` | Meshtastic decode/send (header, AES-CTR, minimal protobuf), node DB, band helpers (`meshFreq`, `bandCenter`) |
| `setup_wizard.cpp` | first-run wizard + card layout |
| `led.cpp` / `macros.cpp` | RGB LED modes / macro engine |
| `app_*.cpp` | one file per app; `portal.cpp` is hotspot + web app |

## Rules of the road

- **Shared SPI**: SD (CS 12) and LoRa (CS 5) share SCK 40 / MOSI 14 / MISO 39 on `sharedSPI(FSPI)`. The display owns
  SPI3, so never use `HSPI` for SD/LoRa.
- LoRa: RST 3, DIO1 4, BUSY 6, TCXO 1.8 V, DIO2 as RF switch. GPS on Serial1 RX 15 / TX 13 @115200.
- Everything long-running must call `readKey()` or `sysTick()` regularly. That keeps GPS, LoRa RX, Wi-Fi and the
  portal alive.
- Radio ownership goes through `loraSetMode(RM_CAPDECK | RM_MESH | RM_SCAN)`. Scanners must restore the previous mode.
- Colours come from `col::` (runtime, themed). Don't hard-code app colours.
- `<FS.h>`/`<SD.h>` must be included before `<M5Cardputer.h>` (enables `drawPngFile(SD, ...)`). `core.h` does this.
- Meshtastic sending is **Client-Mute only and off by default** (owner's decision). Never add rebroadcasting.
- Band-dependent frequencies go through `meshFreq()` / `bandCenter()`; never hard-code 915.
- `arduino-cli` needs `ctags` for `.ino` preprocessing. Keep functions in the `.ino` defined before use.

## Release checklist

1. Bump `CAPDECK_VERSION` in `core.h`, add a `CHANGELOG.md` entry with credits.
2. Build, copy `CapDeck.ino.bin` → `firmware/CapDeck.bin` and `sd_card/CapDeck.bin`, merged → `firmware/CapDeck-vX.Y.Z-full-0x0.bin`.
3. Regenerate `SHA256SUMS.txt`, tag `vX.Y.Z`, attach both binaries to the GitHub release.
