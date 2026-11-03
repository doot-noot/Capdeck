# Third-party notices

CapDeck firmware binaries include the following open-source software. Their licenses apply to their code;
CapDeck's own code is under the MIT license in `LICENSE`.

| Component | Version / commit | License | Source |
|-----------|------------------|---------|--------|
| Arduino core for ESP32 (incl. ESP-IDF, mbedTLS) | 3.3.12 | LGPL-2.1 / Apache-2.0 | github.com/espressif/arduino-esp32 |
| M5Cardputer | 1.1.1 (bf64d31) | MIT | github.com/m5stack/M5Cardputer |
| M5Unified | 0.2.29 (44952cb) | MIT | github.com/m5stack/M5Unified |
| M5GFX (LovyanGFX) | 0.2.36 (7cb26b4) | MIT / FreeBSD | github.com/m5stack/M5GFX |
| RadioLib | 7.8.1 (2325a38) | MIT | github.com/jgromes/RadioLib |
| IRremoteESP8266 | 2.9.0 (1e2f0f3) | LGPL-2.1 | github.com/crankyoldgit/IRremoteESP8266 |
| rc-switch | 2.6.4 (436a74b) | LGPL-2.1 | github.com/sui77/rc-switch |
| TinyGPSPlus | 1.1.0 (0a20575) | LGPL-2.1 | github.com/mikalhart/TinyGPSPlus |
| ArduinoJson | 7.4.2 (1cd478f) | MIT | github.com/bblanchon/ArduinoJson |
| arduinoWebSockets | 2.7.3 (1c77130) | LGPL-2.1 | github.com/Links2004/arduinoWebSockets |

The LGPL-2.1 libraries are used unmodified. CapDeck's complete source and build instructions (`CLAUDE.md`) are
published in this repository, so the binaries can be rebuilt and relinked against modified versions of those
libraries.

Meshtastic is a trademark of Meshtastic LLC. CapDeck is not affiliated with or endorsed by
Meshtastic. Its receiver implements the publicly documented packet format, and it contains no Meshtastic code.
