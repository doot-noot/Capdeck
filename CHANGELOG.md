# Changelog

All notable changes to CapDeck. Versions follow [Semantic Versioning](https://semver.org/).

CapDeck is developed by **doot-noot** (project owner: features, hardware testing, releases) with
**Claude (Anthropic)**, an AI coding assistant that wrote the firmware code to the owner's specifications.
Release binaries are built from `source/` at the same tag; SHA-256 checksums are in `SHA256SUMS.txt`.

---

## [2.0.0] - 2026-11-07

Everything since 1.1.0 (1.2.0-beta was never published here). Compiled and UI-checked; still needs a full
hardware pass.

### Added
- **Macros**: one tap fires a chain of actions (HA + IR + RF + LoRa + waits/beeps) from `/capdeck/macros.json`.
- **Local AI** app for OpenAI-compatible servers, with an on-device **setup guide** (Ollama / LM Studio, IP, port,
  firewall) that opens the first time no server is set.
- **Duck**: zero-setup lookups (DuckDuckGo Instant Answers, Wikipedia fallback).
- **Mycelium** (Meshtastic): node list, GPS map, public + custom channel decode, Find-the-mesh, and optional
  **Client-Mute sending** (off by default; never rebroadcasts).
- **EU 868 band** (LoRa > Radio settings > Band): Meshtastic EU slots, an 860–886 MHz scanner sweep, and the EU LoRaWAN
  channels in the hunter. Turbo presets are hidden on EU because they don't fit the sub-band.
- **LoRa scanner**: spectrum + waterfall and an activity hunter. LoRa Radio test with automatic TCXO detection.
- **Portal**: the Cardputer's own hotspot + phone web app (AI chat, config editor, SD file manager, mesh nodes).
- **Infrared library**: the Full SD kit ships Flipper-IRDB (~9,000 remotes, CC0) with search and a TV power-off sweep.
- **First-run setup wizard** (theme, Wi-Fi, Mycelium).
- **On-board RGB LED** modes, **Developer mode** (memory meter, diagnostics).
- **Themes**: Spore (living mycelium wallpaper, the default), Amanita, Lava, Classic, Cyber, Amber, **Matrix**
  (digital rain), and Low Power (switches on automatically when the battery is low).

### Fixed
- Text can no longer run off the screen: long lines are shortened with "..", and setup cards wrap.
- The clock and the developer memory readout no longer overlap in the status bar.

### Known limitations
- The EU duty-cycle limit isn't enforced; keep EU transmissions short.
- Meshtastic sending is Client-Mute only (no router/ACK/DM/telemetry). Test it on a private channel first.

### Credits
- doot-noot: feature direction, branding, hardware and testing.
- Claude (Anthropic): implementation. Meshtastic framing/crypto checked against meshtastic/firmware; EU slot
  frequencies computed with Meshtastic's slot formula (verified against all 10 US preset frequencies).

## [1.1.0] - 2026-11-03

### Added
- Home Assistant push-to-talk + typed Assist; LoRa chat/monitor/ping; GPS; Infrared (Flipper .ir); RF 433
  decode/scope/replay; Files; Config; icon-carousel UI.
