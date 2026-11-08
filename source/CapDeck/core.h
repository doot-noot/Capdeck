// CapDeck - custom firmware for M5Stack Cardputer ADV (+ Cap LoRa-1262)
// Shared declarations used by every app.
#pragma once
#include <Arduino.h>
#include <FS.h>  // must come before M5GFX so drawPngFile/drawJpgFile(SD, ...) exist
#include <SPI.h>
#include <SD.h>
#include <M5Cardputer.h>
#include <WiFi.h>
#include <Preferences.h>
#include <vector>
#include <functional>

#define CAPDECK_VERSION "2.0.0"
#define CAPDECK_BUILD "2026-11-08"  // build marker; bump per build.

// ---------------------------------------------------------------- pins
// Shared SPI bus (microSD + Cap LoRa-1262)
#define PIN_SPI_SCK 40
#define PIN_SPI_MOSI 14
#define PIN_SPI_MISO 39
#define PIN_SD_CS 12
// Cap LoRa-1262 (SX1262)
#define PIN_LORA_CS 5
#define PIN_LORA_IRQ 4  // DIO1
#define PIN_LORA_RST 3
#define PIN_LORA_BUSY 6
// Cap GPS (ATGM336H) UART: Cardputer RX <- GPS TX, Cardputer TX -> GPS RX
#define PIN_GPS_RX 15
#define PIN_GPS_TX 13
// Built-in IR LED
#define PIN_IR_TX 44
// On-board RGB LED (WS2812 on the Stamp-S3A). Shares power with the backlight on the ADV.
#define PIN_LED 21
// Grove port (HY2.0-4P): yellow = G2, white = G1
#define PIN_GROVE_YELLOW 2
#define PIN_GROVE_WHITE 1

// ---------------------------------------------------------------- screen
#define SCR_W 240
#define SCR_H 135
#define BAR_H 14
// Full-screen frame buffer. drawString() is wrapped so text can never run off the edge of the screen:
// a line that would overflow is shortened with "..". Set canvas.fit = false for deliberate off-screen text.
class FitCanvas : public M5Canvas {
 public:
  using M5Canvas::M5Canvas;
  using M5Canvas::drawString;
  bool fit = true;
  size_t drawString(const char *s, int32_t x, int32_t y);
  size_t drawString(const String &s, int32_t x, int32_t y) { return drawString(s.c_str(), x, y); }
};
extern FitCanvas canvas;
extern SPIClass sharedSPI;

// ---------------------------------------------------------------- colours
// Runtime palette - themes overwrite these (see applyTheme in ui.cpp)
namespace col {
inline uint16_t BG = 0x0861;      // near-black blue
inline uint16_t PANEL = 0x10A3;   // card background
inline uint16_t PANEL2 = 0x18E5;
inline uint16_t TEXT = 0xEF7D;
inline uint16_t DIM = 0x8410;
inline uint16_t FAINT = 0x3186;
inline uint16_t OK = 0x2FE6;
inline uint16_t WARN = 0xFE60;
inline uint16_t BAD = 0xF8C3;
// per-app colours
inline uint16_t HA = 0x3D7F;     // home assistant blue
inline uint16_t AI = 0x5EDF;     // local ai
inline uint16_t LORA = 0xB01F;   // violet
inline uint16_t MESH = 0x67F0;   // mesh green
inline uint16_t GPS = 0x07F0;    // spring green
inline uint16_t IR = 0xF8A6;     // infrared red
inline uint16_t RF = 0xFD20;     // amber
inline uint16_t PORTAL = 0x07FF; // cyan
inline uint16_t FILES = 0xFEA0;  // folder yellow
inline uint16_t CONF = 0x9CF3;   // steel
inline uint16_t BAR = 0x0020;    // status bar background (dark themes: near black; light themes: panel)
}  // namespace col
// Every theme is alive: each has its own animated wallpaper. "Still life" freezes them all to plain colour.
enum ThemeStyle : uint8_t {
  STYLE_PLAIN, STYLE_SYNTHWAVE, STYLE_MATRIX, STYLE_CRT, STYLE_EMBER,
  STYLE_SPORE, STYLE_OCEAN, STYLE_STORM, STYLE_AURORA, STYLE_ABYSS, STYLE_VOID
};
struct Theme {
  const char *name;   // living name (Ocean)
  const char *still;  // still-life name (Blue)
  uint16_t bg, panel, panel2, text, dim, faint;
  uint16_t ha, ai, lora, mesh, gps, ir, rf, portal, files, conf;
  uint16_t accent;
  ThemeStyle style;
  uint32_t led;  // RGB888 for the on-board LED
};
extern const Theme THEMES[];
extern const int THEME_COUNT;
void applyTheme(uint8_t idx);
ThemeStyle themeStyle();  // STYLE_PLAIN when still life / low battery
const char *themeName(int idx);  // living or still-life name, whichever is active
uint16_t accent();  // theme accent (user selectable)
void drawBackdrop(uint32_t t);  // the live wallpaper (no-op in still life)
bool themePicker();             // full-screen picker with live preview; true if a theme was chosen
uint16_t blend565(uint16_t a, uint16_t b, float t);  // t=1 -> a

// ---------------------------------------------------------------- input
struct Key {
  bool any = false;
  bool enter = false, back = false, esc = false;
  bool up = false, down = false, left = false, right = false;
  bool tab = false, space = false, del = false, fn = false;
  char ch = 0;  // printable char (raw, for text entry)
  bool repeat = false;
};
// Polls hardware (call often). Returns a key on press / auto-repeat.
Key readKey();
// Lower level: is a key physically held right now (space / enter / G0)?
bool talkKeyHeld();
// Background work: GPS feed, LoRa RX, Wi-Fi, clock. Called by readKey().
void sysTick();

// ---------------------------------------------------------------- icons
enum AppIcon : uint8_t {
  ICON_HA,
  ICON_LORA,
  ICON_GPS,
  ICON_IR,
  ICON_RF,
  ICON_FILES,
  ICON_CONF,
  ICON_CLOCK,
  ICON_BATTERY,
  ICON_WIFI,
  ICON_DISPLAY,
  ICON_SOUND,
  ICON_INFO,
  ICON_POWER,
  ICON_AI,
  ICON_MESH,
  ICON_PORTAL,
  ICON_SCAN,
  ICON_MAP,
  ICON_MACRO,
  ICON_MYCO,
  ICON_FORAGE,
  ICON_RHIZO,
  ICON_NIGHTCAP,
  ICON_COUNT
};
void drawIcon(LovyanGFX &g, uint8_t icon, int cx, int cy, int size, uint16_t color, uint16_t bg = col::BG);

// ---------------------------------------------------------------- ui
void drawStatusBar(const char *title, uint16_t color);
void drawHeader(const char *title, uint16_t color, uint8_t icon = 255);  // status bar + title strip
void pushFrame();
void toast(const String &msg, uint16_t color = 0, uint32_t ms = 1300);  // blocking popup
bool confirmBox(const String &question, uint16_t color);
// Scrollable list. returns index or -1 for back. `sel` keeps cursor between calls.
struct ListItem {
  String label;
  String right;    // small right-aligned text
  uint8_t icon = 255;
  uint16_t color = 0;
};
int listMenu(const char *title, uint16_t color, std::vector<ListItem> &items, int &sel, uint8_t icon = 255,
             std::function<void()> onIdle = nullptr);
// Single-line text entry. returns false on cancel.
bool textInput(const char *title, String &value, uint16_t color, bool secret = false, int maxLen = 120);
// Show multi-line text, scroll with ; and .
void textViewer(const char *title, const String &text, uint16_t color);
void wrapText(const String &text, int width, std::vector<String> &out);  // for current canvas font
void beep(int freq = 2200, int ms = 25);
// Truncates text (with "..") so it can never run off the edge of a 240px screen.
String fitText(const String &s, int maxW);
// Picks black or white text for whatever colour sits behind it, so nothing is ever unreadable.
uint16_t textOn(uint16_t bg);
bool batteryCharging();
bool lowPowerMode();  // low battery & unplugged: drop the animated backdrops
void drawBattery(LovyanGFX &g, int x, int y, int level, uint16_t c);
void drawWifiGlyph(LovyanGFX &g, int x, int y, int bars, uint16_t c);
String humanSize(uint64_t bytes);

// ---------------------------------------------------------------- settings / config
struct HAButton {
  String name;
  String service;  // e.g. "light.toggle"
  String entity;   // e.g. "light.bedroom"
  String webhook;  // alternative: webhook id
  String data;     // optional extra JSON object for service data
};
struct Settings {
  // persisted in NVS
  uint8_t brightness = 160;
  uint8_t accentIdx = 0;  // 0 = theme default
  uint8_t theme = 0;
  uint8_t ledMode = 1;    // 0 off, 1 theme colour, 2 breathe, 3 brand green, 4 rainbow, 5 alerts-only
  bool stillLife = false; // freeze every live wallpaper to its plain colour
  bool tips = true;       // longer hints for an app's first few opens
  bool doze = true;       // light sleep (radios off) once the screen is off
  uint8_t sporeMins = 30; // deep sleep after this long dozing; 0 = never
  uint8_t wakeKey = 0;    // 0 Enter, 1 any key, 2 Fn+Enter (pocket mode)
  uint8_t ledBright = 90;
  bool meshListen = false;
  bool meshTx = false;     // allow sending our own packets (Client-Mute role; never rebroadcasts)
  uint8_t meshPreset = 0;  // index into MESH_PRESETS
  String meshShort, meshLong;  // our node names on the mesh
  bool sound = true;
  uint8_t volume = 110;
  bool autoWifi = true;
  uint8_t dimSecs = 30;  // 0 = never
  int8_t rfRxPin = PIN_GROVE_WHITE;
  int8_t rfTxPin = PIN_GROVE_YELLOW;
  bool rfToHA = false;
  String wifiSsid, wifiPass;
  // loaded from /capdeck/config.json on SD
  String haUrl, haToken, haPipeline, haTts, haAgent;
  std::vector<HAButton> haButtons;
  uint8_t band = 0;  // 0 = US 915 (902-928 MHz), 1 = EU 868 (863-870 MHz)
  float loraFreq = 915.0;
  float loraBw = 125.0;
  uint8_t loraSf = 9, loraCr = 7, loraSync = 0x12;
  int8_t loraPower = 17;
  String nick = "Cardputer";
  String aiUrl, aiModel, aiKey, aiSystem;
  String portalPass;
  String tz = "EST5EDT,M3.2.0,M11.1.0";
  std::vector<std::pair<String, String>> extraWifi;
};
extern Settings cfg;
extern bool sdOK;
void loadSettings();
void saveSettings();
bool loadConfigFile();      // reads /capdeck/config.json (creates template if missing)
void ensureDirs();
void applyBrightness();
void wakeScreen();

// ---------------------------------------------------------------- wifi / time
enum class NetState { OFF, CONNECTING, UP, FAILED };
NetState netState();
void wifiStart();  // non-blocking
bool wifiWait(uint32_t ms, const char *why);  // shows a small spinner screen
void wifiStop();
bool timeValid();
String timeStr(bool seconds = false);

// ---------------------------------------------------------------- radio / gps shared state
struct LoraMsg {
  String text;
  float rssi, snr;
  bool mine;
  uint32_t at;
};
enum RadioMode : uint8_t { RM_CAPDECK, RM_MESH, RM_SCAN };
extern RadioMode radioMode;
void loraSetMode(RadioMode m);  // RM_SCAN detaches the RX handler for the scanner
class SX1262;
SX1262 *loraRadio();
extern bool loraPresent;
extern std::vector<LoraMsg> loraLog;
extern int loraUnread;
bool loraInit();
extern String loraInitLog;
extern float loraTcxoUsed;
int loraRssiHealth(int samples, int *outMin, int *outMax);
int loraTryTcxo(float tcxo, bool &healthy);
void loraSaveTcxo(float v);
uint16_t loraDeviceErrors();
uint8_t loraChipStatus();
bool loraSend(const String &payload);
void loraApplyParams();
void loraPoll();  // called from sysTick

extern bool gpsSeen;  // any NMEA received
// Satellites in view, straight from the GPS module's GSV/GSA sentences (real positions, not decoration).
struct SatInfo {
  char sys;        // 'G' GPS, 'R' GLONASS, 'E' Galileo, 'C' BeiDou, 'J' QZSS, '?' unknown
  uint8_t prn;
  int8_t elev;     // degrees above the horizon
  uint16_t az;     // degrees from north
  int8_t snr;      // dB-Hz, 0 = not tracked
  bool used;       // part of the current fix
  uint32_t seenMs;
};
extern SatInfo gpsSatTable[48];
int gpsSatList(std::vector<SatInfo> &out);  // fresh satellites (seen in the last 15 s)
String satName(const SatInfo &s);           // "GPS PRN 13", "BeiDou C22", "Michibiki J02"...
bool gpsFix();
int gpsSats();
void gpsPoll();

// ---------------------------------------------------------------- meshtastic
struct MeshPreset {
  const char *name;
  float freq, bw;
  uint8_t sf, cr;
};
extern const MeshPreset MESH_PRESETS[];
extern const int MESH_PRESET_COUNT;
bool meshPresetAllowed(const MeshPreset &p);  // false for Turbo presets on EU 868
float meshFreq(const MeshPreset &p);  // the preset's frequency in the current band
float bandCenter();
float bandLo();
float bandHi();
const char *bandName();
struct MeshNode {
  uint32_t num = 0;
  String shortName, longName;
  bool hasPos = false;
  double lat = 0, lon = 0;
  int32_t alt = 0;
  float rssi = 0, snr = 0;
  uint8_t hops = 0;
  uint32_t lastHeard = 0;  // unix time if clock valid, else 0
  uint32_t lastMs = 0;     // millis() when heard this session
  uint32_t packets = 0;
};
struct MeshText {
  uint32_t from;
  String text;
  uint32_t at;
  String channel;
};
extern std::vector<MeshNode> meshNodes;
extern std::vector<MeshText> meshTexts;
extern uint32_t meshPackets, meshUndecoded;
void meshHandlePacket(const uint8_t *buf, size_t len, float rssi, float snr);
MeshNode *meshFind(uint32_t num);
void meshNodeDetail(const MeshNode &n);  // app_mesh.cpp
String meshName(uint32_t num);
void meshLoad();
void meshBuildChannels();  // call after preset/config changes
// Meshtastic transmit (Client-Mute role: sends our own packets, never rebroadcasts)
bool meshSendText(const String &channelName, const String &text);
bool meshSendNodeInfo();
bool meshSendPosition();
uint32_t meshMyNum();
void meshTxTick();
void meshSaveIfDirty();
bool myPosition(double &lat, double &lon);  // GPS fix, or last known
double distanceM(double lat1, double lon1, double lat2, double lon2);
double bearingDeg(double lat1, double lon1, double lat2, double lon2);
String fmtDistance(double m);
String fmtAge(uint32_t ms);

// ---------------------------------------------------------------- portal / ai
bool portalActive();
void portalStart();
void portalStop();
void portalPoll();
String portalSsid();
String aiChat(const std::vector<std::pair<String, String>> &history, String &err);  // role,content pairs

// ---------------------------------------------------------------- apps
void appHomeAssistant();
void appLora();
void appGps();
void appIR();
void appRF();
void appFiles(const char *startDir = "/");
String pickFile(const char *title, const char *startDir, const char *ext, uint16_t color, uint8_t icon);
void appSettings();
void appMesh();
void appAI();
void appPortal();
void appScanner();
void appMacros();
void appMycorrhiza();  // downloads & lookups: DDG/Wikipedia, map regions, IR remotes, sky data
void appNightcap();    // the sky above you
void appRhizomorph();  // Reticulum radio: the Cardputer as an RNode for Sideband (BLE)
extern bool rhizoRunning;
void irLibrarySearch();  // mycorrhiza.cpp: search the Flipper-IRDB index, download on demand
void appForage();      // collect spores: passive Wi-Fi / Bluetooth / mesh finds
void appMap(int focusLayer = -1);  // one map: roads (lines) + finds and nodes (dots)

// ---------------------------------------------------------------- forage ("collecting spores")
// Every find is a mushroom. Plain label in brackets so nobody's lost.
enum FindKind : uint8_t {
  FK_SPORE,     // LoRa / mesh packet
  FK_FRUIT,     // mesh node (fruiting body)
  FK_PUFFBALL,  // Bluetooth device
  FK_FAIRY,     // Wi-Fi network
  FK_DEATHCAP,  // hidden Wi-Fi network
  FK_MOREL,     // rare / strong find
  FK_CORDYCEPS, // LoRaWAN device
  FK_KIND_COUNT
};
struct Find {
  FindKind kind;
  String label;      // SSID / node / device name
  uint8_t mac[6];
  uint8_t macLen;    // 6 for wifi/ble, 0 for packet-only
  int16_t rssi;
  uint8_t channel;
  uint8_t auth;      // Wi-Fi: 0 open, else secured
  bool hasPos;
  double lat, lon;
  uint32_t firstMs, lastMs;
  uint32_t firstUnix;
  uint32_t hits;
};
extern std::vector<Find> forageFinds;   // this session's finds (map + list read it)
extern bool forageRunning;               // true while a foray is active: keeps radios up but lets the screen sleep (pocket mode)
const char *findName(FindKind k);       // "Fairy ring"
const char *findPlain(FindKind k);      // "Wi-Fi network"
void drawFindSymbol(LovyanGFX &g, int x, int y, FindKind k, uint16_t c);  // map.cpp
extern double mapStartLat, mapStartLon;  // Mycorrhiza sets these to centre the map on a saved region
// Web-Mercator helpers (shared by Map and Nightcap ground tracks)
double lonToPx(double lon, int z);
double latToPx(double lat, int z);
double metersPerPx(double lat, int z);
// shared downloader (Mycorrhiza)
bool httpGetToString(const String &url, String &body, String &err, size_t maxBytes = 60000);
bool httpGetToFile(const String &url, const char *path, String &err, std::function<void(size_t)> progress = nullptr);
String urlEncode(const String &s, bool keepSlash = false);
bool irFetchRemote(const String &relPath, String &localPath, String &err);  // Flipper-IRDB on demand
// non-interactive senders used by the macro engine
String irSendSignal(const String &path, const String &name);  // "" ok, else error
String rfSendSaved(const String &name);
String haCallService(const String &service, const String &entity, const String &dataJson);
String haCallWebhook(const String &id);
int macroCount();
void macroEnsureTemplate();
bool devMode();          // developer options toggle (NVS)
void setDevMode(bool on);
// on-board RGB LED
void ledApply();                               // (re)start from cfg.ledMode
void ledTick();                                // animations; called from sysTick
void ledFlash(uint8_t r, uint8_t g, uint8_t b, uint32_t ms = 350);  // soft pulse (rate-limited, never strobes)
void ledPulse(uint32_t rgb = 0);  // soft pulse; 0 = the theme's LED colour
bool screenIsOff();               // power.cpp
// ---------------------------------------------------------------- power: Dim -> Doze -> Spore mode
void powerBootCheck();  // FIRST thing in setup(): decide whether a wake from Spore mode should really wake
void powerAfterBoot();  // after the UI is up: handle a timed alarm wake
bool powerOnKey();      // a key arrived; true if it only woke the screen (swallow it)
void powerIdle();       // called while idle; dims, turns the screen off, dozes
void stayAwake();       // apps doing real work call this in their loop (blocks Doze for a few seconds)
void sporeMode(const char *why);  // deep sleep now
void powerSetAlarm(uint32_t unixTime, const char *what);  // wake (even from sleep) to chime; 0 clears
extern const char *WAKE_KEY_NAMES[];
// ---------------------------------------------------------------- tips
void tipsOpened(const char *app);  // counts opens; tips show for an app's first 3
bool tipsActive();
const char *tip(const char *longText, const char *shortText);
extern const char *LED_MODE_NAMES[];
#define LED_MODE_COUNT 6
void drawChatView(const char *title, uint16_t color, uint8_t icon, const std::vector<std::pair<String, bool>> &chat, int scrollUp,
                  const String *input, const String &status, uint16_t statusColor);
// IR can be opened directly on a file from the file manager
void irOpenFile(const String &path);
// HA helpers used by other apps
bool haReady();
int haPost(const String &path, const String &body, String *resp = nullptr, bool auth = true);
