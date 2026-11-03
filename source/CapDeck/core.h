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

#define CAPDECK_VERSION "1.1.0"

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
// Grove port (HY2.0-4P): yellow = G2, white = G1
#define PIN_GROVE_YELLOW 2
#define PIN_GROVE_WHITE 1

// ---------------------------------------------------------------- screen
#define SCR_W 240
#define SCR_H 135
#define BAR_H 14
extern M5Canvas canvas;  // full-screen frame buffer
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
}  // namespace col
enum ThemeStyle : uint8_t { STYLE_PLAIN, STYLE_SYNTHWAVE, STYLE_MATRIX, STYLE_CRT };
struct Theme {
  const char *name;
  uint16_t bg, panel, panel2, text, dim, faint;
  uint16_t ha, ai, lora, mesh, gps, ir, rf, portal, files, conf;
  uint16_t accent;
  ThemeStyle style;
};
extern const Theme THEMES[];
extern const int THEME_COUNT;
void applyTheme(uint8_t idx);
ThemeStyle themeStyle();
uint16_t accent();  // theme accent (user selectable)

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
  bool meshListen = false;
  uint8_t meshPreset = 0;  // index into MESH_PRESETS
  bool sound = true;
  uint8_t volume = 110;
  bool autoWifi = true;
  uint8_t dimSecs = 30;  // 0 = never
  int8_t rfRxPin = PIN_GROVE_WHITE;
  int8_t rfTxPin = -1;
  bool rfToHA = false;
  String wifiSsid, wifiPass;
  // loaded from /capdeck/config.json on SD
  String haUrl, haToken, haPipeline, haTts, haAgent;
  std::vector<HAButton> haButtons;
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
bool loraSend(const String &payload);
void loraApplyParams();
void loraPoll();  // called from sysTick

extern bool gpsSeen;  // any NMEA received
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
};
extern std::vector<MeshNode> meshNodes;
extern std::vector<MeshText> meshTexts;
extern uint32_t meshPackets, meshUndecoded;
void meshHandlePacket(const uint8_t *buf, size_t len, float rssi, float snr);
MeshNode *meshFind(uint32_t num);
String meshName(uint32_t num);
void meshLoad();
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
void drawChatView(const char *title, uint16_t color, uint8_t icon, const std::vector<std::pair<String, bool>> &chat, int scrollUp,
                  const String *input, const String &status, uint16_t statusColor);
// IR can be opened directly on a file from the file manager
void irOpenFile(const String &path);
// HA helpers used by other apps
bool haReady();
int haPost(const String &path, const String &body, String *resp = nullptr, bool auth = true);
