# Changelog

All notable changes to CapDeck. Versions follow [Semantic Versioning](https://semver.org/).

CapDeck is developed by **doot-noot** (project owner: features, hardware testing, releases) with
**Claude (Anthropic)**, an AI coding assistant that wrote the firmware code to the owner's specifications.
Release binaries are built from `source/` at the same tag; SHA-256 checksums are in `SHA256SUMS.txt`.

---

## [2.0.0] - 2026-11-08

The big one: everything since 1.1.0 (1.2.0-beta was never published here). Compiled and code-reviewed; it still
needs a full hardware pass, and the newest parts are marked experimental.

### Added
- **Nightcap**, a live planetarium for where you stand. Stars by brightness with constellation lines, the
  **Summer Triangle** (Vega, Deneb, Altair) in gold, planets drawn as their classical glyphs, the real Moon phase
  (with an eye), GPS satellites as **Tracers** with trails from the module's own data, and the **ISS**,
  propagated from its orbit. It chimes before visible passes (10 and 5 minutes by default, night only), even
  while the device sleeps. Time travel with the arrows; **T** for toggles. Bruise Blue sky.
- **Rhizomorph** (Reticulum radio, experimental): the Cardputer + Cap as an **RNode for Sideband** over Bluetooth
  LE, with PIN pairing, KISS protocol and split packets up to Reticulum's 500-byte MTU.
- **Forage** (collect spores): passive Wi-Fi, Bluetooth and mesh logging. Fairy ring = Wi-Fi, Death cap = hidden
  Wi-Fi, Puffball = Bluetooth, Fruiting body = mesh node, Spore = packet, Morel = rare find. Pocket mode keeps
  foraging with the screen off. **Spore print** exports a WiGLE CSV.
- **Map**: one map with layers. Roads are drawn as mycelial threads (downloaded per city from OpenStreetMap), with
  nodes and finds as distinct symbols, optional tiles, and your position with range rings.
- **Mycorrhiza** (downloads & lookups, replaces Duck): DuckDuckGo + Wikipedia, map region downloads, IR remotes on
  demand from Flipper-IRDB, and ISS orbit data.
- **Power chain**: Dim → Doze (screen + radios off, light sleep, instant wake) → Spore mode (deep sleep). Wake
  key Enter / Any key / Fn+Enter; apps doing real work hold the device awake.
- **Files**: view any text file, view **images** (PNG/JPG/BMP/QOI), **edit `.json`** line by line with a validity
  check before saving, and **launch another firmware `.bin`** from SD (when the flash has a spare app slot).
- **Themes**: every theme is alive. Spore (mycelium), Spore Light, **Matrix** (rain), Ocean, Lava, Amber, Storm,
  Cyber, Aurora, Abyss, Void. A **Still life** toggle freezes them, and a low battery does it automatically.
  The LED matches the theme and pulses gently.
- **EU 868 band**: Meshtastic EU slots, an 860–886 MHz scanner sweep, and EU LoRaWAN channels in the hunter.
- **LoRaMAN**: the LoRaWAN app's launch flips the W into an M.
- **First-boot tips** (can be turned off), a Local AI setup guide, Macros, Local AI, Portal, the IR library, the
  setup wizard, LED modes, Developer mode.

### Changed
- Duck became Mycorrhiza; Mesh is Mycelium.
- The Portal has a simple default password (`deathcaps`); press P to set your own.
- The GPS radar now plots the real satellites the module reports. Earlier builds drew placeholder dots.

### Fixed
- Text can no longer run off the screen (long lines end in "..", wizard cards wrap).
- The clock and the developer memory readout no longer overlap.
- The Spore theme showed the Matrix rain, and Matrix was missing.
- The Cyber LED strobed in a busy mesh; LEDs now pulse slowly in the theme's colour.

### Known limitations
- Rhizomorph hasn't been tested against Sideband on hardware yet.
- The EU duty-cycle limit isn't enforced; keep EU transmissions short.
- Launching a `.bin` needs a spare app slot (M5Launcher layouts); a full-flash CapDeck says how to do it instead.
- Meshtastic sending is Client-Mute only (no router/ACK/DM/telemetry). Test it on a private channel first.

### Credits
- doot-noot: feature direction, names and branding (Deathcaps), hardware and testing.
- Claude (Anthropic): implementation. Meshtastic framing checked against meshtastic/firmware; EU slot frequencies
  from Meshtastic's slot formula (verified against all 10 US preset frequencies); RNode protocol from Reticulum's
  RNodeInterface.py; star positions from the Yale Bright Star catalogue; planets from JPL's Keplerian elements.

## [1.1.0] - 2026-11-03

### Added
- Home Assistant push-to-talk + typed Assist; LoRa chat/monitor/ping; GPS; Infrared (Flipper .ir); RF 433
  decode/scope/replay; Files; Config; icon-carousel UI.
