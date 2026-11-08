# CapDeck — custom firmware for the M5Stack Cardputer ADV

![main menu](screenshots/main_menu.png) ![cyber theme](screenshots/theme_cyber.png)

A Bruce-style icon carousel with twelve apps, living themes, and a Wi-Fi portal so you can set it up
from your phone instead of pulling the SD card.

**Current version: 2.0.0** · [Changelog](CHANGELOG.md) · firmware + SD kits in [Releases](../../releases)

| # | App | What it does |
|---|-----|--------------|
| 1 | **Home Assistant** | Button deck with live on/off states, **push-to-talk Assist** (hold SPACE or G0), typed Assist chat |
| 2 | **Macros** | One tap fires a whole chain: HA + IR + RF + LoRa + waits, from `/capdeck/macros.json` |
| 3 | **Local AI** | Chat with your own model through any OpenAI-compatible server (Ollama, LM Studio, llama.cpp). Built-in setup guide |
| 4 | **Duck** | Look things up with zero setup: DuckDuckGo Instant Answers, Wikipedia as the fallback |
| 5 | **LoRa** | CapDeck-to-CapDeck chat, packet monitor, ping, **scanner** (spectrum + waterfall, activity hunter). **US 915 or EU 868** |
| 6 | **Mycelium** | **Meshtastic**: nodes heard, public messages, a GPS map, Find-the-mesh. Optional sending (Client-Mute, off by default) |
| 7 | **GPS** | Fix, satellite radar, waypoints to CSV; sets the clock |
| 8 | **Infrared** | Flipper `.ir` remotes; the Full SD kit bundles ~9,000 (Flipper-IRDB) with search and a TV power-off sweep |
| 9 | **RF 433** | Decodes fixed-code remotes from an RX470C, logic scope, saved codes, forward to HA, replay with a transmitter |
| 10 | **Portal** | The Cardputer's own hotspot + phone web app: AI chat, config editor, SD file manager, mesh node list |
| 11 | **Files** | On-device SD browser: view, rename, delete, folders, open `.ir` files |
| 12 | **Config** | Themes, clock, battery, Wi-Fi, display, sound, LED, system info, back to M5Launcher |

**Themes:** Spore (living mycelium, the default), Amanita, Lava, Classic, Cyber, Amber, Matrix (digital rain), and
Low Power, which switches on by itself when the battery is low.

Hardware: Cardputer ADV, with the Cap LoRa-1262 (LoRa + GPS) for LoRa, Mycelium and GPS. Everything else works
without the Cap.

## Install

**M5Launcher (keeps your launcher):**
1. Download an SD kit from [Releases](../../releases) and unzip it onto the root of the microSD card:
   - **Lite** (~1 MB): `CapDeck.bin`, config template and folders.
   - **Full** (~21 MB): Lite plus the Flipper-IRDB remote library (~9,000 remotes).
2. In M5Launcher, install `CapDeck.bin` from the SD card.

**Full flash (M5Burner / esptool / web flasher):** flash `firmware/CapDeck-v2.0.0-full-0x0.bin` at address `0x0`.
This replaces whatever was on the device, M5Launcher included.

The first boot runs a short setup wizard (theme, Wi-Fi, Mycelium) and creates `/capdeck/config.json` if it
isn't there.

## Set it up from your phone (no SD swapping)

1. On the Cardputer, open **Portal** and press **ENTER**.
2. Scan the QR code with your phone (or join `CapDeck-XXXX` with the password on screen). The control panel opens
   on its own, or go to `http://192.168.4.1`.
3. **Config** tab: fill in your Wi-Fi, Home Assistant URL + token, and local AI server, then tap *Save & reload*.
4. **Files** tab: upload `.ir` remotes, map tiles, and so on.

## Config reference (`/capdeck/config.json`)

```json
"wifi": [ { "ssid": "YourNetwork", "pass": "YourPassword" } ],
"home_assistant": { "url": "http://192.168.1.50:8123", "token": "...", "tts_engine": "tts.piper" },
"local_ai": { "url": "http://192.168.1.60:11434", "model": "", "system": "Keep replies short." },
"ha_buttons": [ { "name": "Bedroom lights", "service": "light.toggle", "entity": "light.bedroom" } ],
"lora": { "freq_mhz": 915.0, "bw_khz": 125.0, "sf": 9, "cr": 7, "sync_word": 18, "power_dbm": 17 }
```

* **Home Assistant:** use the local `http://IP:8123` address (Talk doesn't support https).
* **Local AI:** Local AI > Setup guide walks through it. In short: Ollama with `OLLAMA_HOST=0.0.0.0` (port 11434)
  or LM Studio's "serve on local network" (port 1234), then allow the port through your firewall.

## Radio band (US 915 / EU 868)

LoRa > Radio settings > **Band**. Fit the antenna for that band before transmitting.

* **US 915:** 902–928 MHz. Mycelium uses Meshtastic's US slot for the chosen preset (LongFast = 906.875 MHz).
* **EU 868:** 863–870 MHz. Mycelium uses Meshtastic's EU sub-band (869.4–869.65 MHz): 869.525 MHz for the 250 kHz
  presets, 869.4625 for LongSlow, 869.5875 for LongMod. The 500 kHz Turbo presets don't fit there and are hidden.
  The scanner sweeps 860–886 MHz. CapDeck does not enforce the EU duty-cycle limit, so keep transmissions short.

## Meshtastic (Mycelium) notes

Listening is off until you turn it on (the setup wizard asks). **Find the mesh** shows which preset nearby nodes use. Packets on the public default
channel are decoded (names, positions, public texts); add your own channels under `mesh_channels` in
`config.json`. Private channels and DMs only show the sender. Recent Meshtastic firmware shares location only when
the owner opts in, so some nodes appear in the list without a map position.

Sending is **off by default**. When enabled it uses the Client-Mute role: your own text, node info and position
only, never rebroadcasting anyone else's packets. Test it on a private channel first.

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
It builds on open-source libraries listed in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md). The Flipper-IRDB
library in the Full SD kit is CC0 (commits before 2319685 aren't covered by that dedication).

Use the radio features only on your own devices and within your local rules.
