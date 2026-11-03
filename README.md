# CapDeck — custom firmware for the M5Stack Cardputer ADV

![main menu](screenshots/main_menu.png) ![cyber theme](screenshots/theme_cyber.png)

A Bruce-style icon carousel with ten apps, built-in themes, and a Wi-Fi portal so you can set it up
from your phone instead of pulling the SD card.

**Current version: 1.1.0** · [Changelog](CHANGELOG.md) · Download everything: [CapDeck-v1.1.0.zip](CapDeck-v1.1.0.zip)

| # | App | What it does |
|---|-----|--------------|
| 1 | **Home Assistant** | Button deck with live on/off states and OK/FAILED notices, **push-to-talk Assist** (hold SPACE or G0), typed Assist chat |
| 2 | **Local AI** | Chat with your own model through any OpenAI-compatible server (llama.cpp `llama-server`, Ollama, LM Studio) |
| 3 | **LoRa** | CapDeck-to-CapDeck chat, packet monitor, ping, **scanner** (902–928 MHz spectrum + waterfall, LoRa activity hunter) |
| 4 | **Mesh** | Passive **Meshtastic** listener: nodes heard, public messages, and a **map** of node positions around your GPS fix |
| 5 | **GPS** | Fix, satellite radar, waypoints to CSV; sets the clock |
| 6 | **Infrared** | Sends Flipper Zero `.ir` files (raw, NEC/NECext, Samsung32, SIRC, RC5/RC5X, RC6) |
| 7 | **RF 433** | Decodes fixed-code remotes from an RX470C, logic scope, saved codes, forward to Home Assistant, replay with a transmitter |
| 8 | **Portal** | The Cardputer's own hotspot + phone web app: AI chat, config editor, SD file manager (upload/download), mesh node list |
| 9 | **Files** | On-device SD browser: view, rename, delete, folders, open `.ir` files |
| 10 | **Config** | Themes (Default, Cyber, Matrix, Amber), clock, battery graph, Wi-Fi, display, sound, system info, back to M5Launcher |

Hardware: Cardputer ADV, with the Cap LoRa-1262 (LoRa + GPS) for LoRa, Mesh and GPS. Everything else works without the Cap.

## Install

**M5Launcher (keeps your launcher):** copy `sd_card/` onto the microSD card's root, then in M5Launcher install
`CapDeck.bin` from SD.

**Full flash (M5Burner / esptool / web flasher):** flash `firmware/CapDeck-v1.1.0-full-0x0.bin` at address `0x0`.
This replaces whatever was on the device, M5Launcher included.

The first boot creates `/capdeck/config.json` on the SD card if it isn't there.

## Set it up from your phone (no SD swapping)

1. On the Cardputer, open **Portal** and press **ENTER**.
2. Scan the QR code with your phone (or join `CapDeck-XXXX` with the password on screen). The control panel opens
   on its own, or go to `http://192.168.4.1`.
3. **Config** tab: fill in your Wi-Fi, Home Assistant URL + token, and local AI server, then tap *Save & reload*.
4. **Files** tab: upload `.ir` remotes, firmware `.bin` files, map tiles, and so on.

When the Cardputer is also on your home Wi-Fi, the panel is at its home IP too (login `capdeck` + the portal password).

## Config reference (`/capdeck/config.json`)

```json
"wifi": [ { "ssid": "YourNetwork", "pass": "YourPassword" } ],
"home_assistant": { "url": "http://192.168.1.50:8123", "token": "...", "tts_engine": "tts.piper" },
"local_ai": { "url": "http://192.168.1.60:8080", "model": "", "system": "Keep replies short." },
"ha_buttons": [ { "name": "Bedroom lights", "service": "light.toggle", "entity": "light.bedroom" } ],
"lora": { "freq_mhz": 915.0, "bw_khz": 125.0, "sf": 9, "cr": 7, "sync_word": 18, "power_dbm": 17 }
```

* **Home Assistant:** use the local `http://IP:8123` address (Talk doesn't support https). Talk uses your default
  Assist pipeline, e.g. Whisper plus the conversation agent of your choice.
* **Local AI:** start `llama-server` with `--host 0.0.0.0` (Ollama: `OLLAMA_HOST=0.0.0.0`, port 11434) and allow the
  port through your firewall.

## Meshtastic notes

CapDeck listens on the US default slot for the chosen modem preset (LongFast = 906.875 MHz). It is **receive-only**
and never transmits into the mesh. Packets on the public default channel are decoded (names, positions, public
texts). Private channels and DMs only show the sender. Recent Meshtastic firmware shares location only when the
owner opts in, so some nodes appear in the list without a map position.

Optional offline map tiles go in `/capdeck/map/<z>/<x>/<y>.png` (or `.jpg`, standard web-mercator tiles). Without
tiles the map shows a grid with range rings.

## Keys

`;` `.` up/down · `,` `/` left/right · **ENTER** or **G0** select · **DEL** back · `1`–`9`, `0` jump ·
chats: **Fn+;/Fn+.** scroll · **`** cancels text entry.

## 433 MHz wiring (RX470C → Grove)

GND → black, VCC → red (5 V), DATA → white (G1) **through a divider** (DATA —10 kΩ— G1 —20 kΩ— GND). The ESP32-S3
pins only take 3.3 V.

## Building

See [CLAUDE.md](CLAUDE.md) for the exact toolchain, library versions and build command.

## Credits & license

CapDeck is written by **doot-noot** with **Claude** (Anthropic) as an AI coding assistant. See
[CHANGELOG.md](CHANGELOG.md) for how each release was made. CapDeck's own code is MIT-licensed ([LICENSE](LICENSE)).
It builds on open-source libraries listed in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

Use the radio features only on your own devices and within your local rules (in the US, 902–928 MHz for LoRa).
