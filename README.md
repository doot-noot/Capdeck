# Capdeck
Cardputer ADV firmware: Home Assistant voice + buttons, LoRa, GPS, IR, 433 MHz and a file manager behind an icon menu.

[README.md](https://github.com/user-attachments/files/34321685/README.md)
# CapDeck v1.0.0 — custom firmware for the M5Stack Cardputer ADV

A Bruce-style scrolling icon menu with seven apps, each with its own icon:

| # | App | What it does |
|---|-----|--------------|
| 1 | **Home Assistant** | Trigger deck (buttons from config, shows live on/off state, clear OK/FAILED notice), **push-to-talk Assist** (hold SPACE or G0), typed Assist chat, connection check |
| 2 | **LoRa** | Chat between CapDecks on the Cap LoRa-1262, packet monitor with signal meter, ping, radio settings. Incoming messages beep and show an envelope from any screen |
| 3 | **GPS** | Cap GPS fix, satellite radar, lat/lon/alt/speed, save waypoints to CSV. Also sets the clock automatically |
| 4 | **Infrared** | Sends signals from Flipper Zero `.ir` files (raw + NEC, NECext, Samsung32, SIRC, RC5/RC5X, RC6), single button or sweep |
| 5 | **RF 433** | Decodes fixed-code remotes from an RX470C, live logic scope, saved codes, optional forward to an HA webhook, replay if you add a transmitter |
| 6 | **Files** | SD card browser: view text/hex, rename, delete, new folder, open `.ir` files straight into Infrared |
| 7 | **Config** | Big 7-segment clock, battery meter + voltage graph, Wi-Fi scan/connect, brightness/accent colour/dim timer, sound/volume, system info, back to M5Launcher, sleep, reboot |

## Install

1. Copy everything inside `sd_card/` to the root of the Cardputer's microSD card
   (you get `/CapDeck.bin` and a `/capdeck/` folder).
2. In M5Launcher choose **SD** → `CapDeck.bin` → install.
3. To go back to the launcher later: Config → **Back to M5Launcher**
   (or use the launcher's usual power-on key).

`firmware/CapDeck-full-flash-0x0.bin` is a full 8 MB image for flashing directly at 0x0
with esptool / a web flasher. **Don't use it if you want to keep M5Launcher** — it replaces it.

## Set up Home Assistant (once)

Edit `/capdeck/config.json` on a computer:

```json
"wifi": [ { "ssid": "YourNetwork", "pass": "YourPassword" } ],
"home_assistant": {
  "url": "http://192.168.1.50:8123",
  "token": "long-lived token from your HA profile > Security",
  "tts_engine": "tts.piper"
},
"ha_buttons": [
  { "name": "Bedroom lights", "service": "light.toggle", "entity": "light.bedroom" },
  { "name": "Run my automation", "webhook": "capdeck_test" }
]
```

* Use the **local http://IP:8123** address. Talk mode doesn't support https.
* Buttons can also carry service data: `"data": { "brightness_pct": 40 }`.
* **Talk** runs your *default* Assist pipeline (or the one named in `"pipeline"`). Whisper does
  speech-to-text, and whatever conversation agent you picked answers (e.g. Ollama on your PC).
  Replies are spoken through `tts_engine` (Piper) and shown as text.
* Config → *Reload config.json* picks up changes without rebooting.

## Keys

`;` `.` up/down · `,` `/` left/right · **ENTER** or **G0** select · **DEL** back ·
`1`–`9` jump · in chats **Fn+;/Fn+.** scroll · **`** cancels text entry.

## 433 MHz wiring (RX470C → Grove)

GND → black, VCC → red (5 V), DATA → white (G1) **through a divider**
(DATA —10 kΩ— G1 —20 kΩ— GND). The Grove port is 5 V, and the ESP32-S3 pins only take 3.3 V.
You can change the pin in RF 433 → Pin setup.

## Rebuilding from source

Arduino IDE / arduino-cli with board **esp32 3.3.x → M5Cardputer** (8 MB, Huge APP partition), plus:
M5Cardputer, M5Unified, M5GFX, RadioLib **7.8.1**, IRremoteESP8266, rc-switch, TinyGPSPlus,
ArduinoJson 7, WebSockets (links2004).

## Pins used

SD + LoRa share SPI (SCK 40, MOSI 14, MISO 39; SD CS 12, LoRa CS 5) · LoRa RST 3, DIO1 4,
BUSY 6, TCXO 1.8 V · GPS RX 15 / TX 13 @115200 · IR TX 44 · Grove G1/G2.
