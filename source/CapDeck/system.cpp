// CapDeck - settings, SD config, Wi-Fi, clock, background radio/GPS service
#include "core.h"
#include <ArduinoJson.h>
#include <RadioLib.h>
#include <TinyGPSPlus.h>
#include <sys/time.h>

Settings cfg;
bool sdOK = false;
SPIClass sharedSPI(FSPI);  // SPI2 - the display already owns SPI3

// ------------------------------------------------------------------ NVS settings
static Preferences prefs;

void loadSettings() {
  prefs.begin("capdeck", true);
  cfg.brightness = prefs.getUChar("bright", 160);
  cfg.accentIdx = prefs.getUChar("accent2", 0);
  cfg.theme = prefs.getUChar("theme", 0);
  cfg.meshListen = prefs.getBool("meshon", false);
  cfg.meshPreset = prefs.getUChar("meshpre", 0);
  cfg.portalPass = prefs.getString("ppass", "");
  cfg.sound = prefs.getBool("sound", true);
  cfg.volume = prefs.getUChar("vol", 110);
  cfg.autoWifi = prefs.getBool("autowifi", true);
  cfg.dimSecs = prefs.getUChar("dim", 30);
  cfg.rfRxPin = prefs.getChar("rfrx", PIN_GROVE_WHITE);
  cfg.rfTxPin = prefs.getChar("rftx", -1);
  cfg.rfToHA = prefs.getBool("rf2ha", false);
  cfg.wifiSsid = prefs.getString("ssid", "");
  cfg.wifiPass = prefs.getString("pass", "");
  prefs.end();
}

void saveSettings() {
  prefs.begin("capdeck", false);
  prefs.putUChar("bright", cfg.brightness);
  prefs.putUChar("accent2", cfg.accentIdx);
  prefs.putUChar("theme", cfg.theme);
  prefs.putBool("meshon", cfg.meshListen);
  prefs.putUChar("meshpre", cfg.meshPreset);
  prefs.putString("ppass", cfg.portalPass);
  prefs.putBool("sound", cfg.sound);
  prefs.putUChar("vol", cfg.volume);
  prefs.putBool("autowifi", cfg.autoWifi);
  prefs.putUChar("dim", cfg.dimSecs);
  prefs.putChar("rfrx", cfg.rfRxPin);
  prefs.putChar("rftx", cfg.rfTxPin);
  prefs.putBool("rf2ha", cfg.rfToHA);
  prefs.putString("ssid", cfg.wifiSsid);
  prefs.putString("pass", cfg.wifiPass);
  prefs.end();
}

void applyBrightness() { M5Cardputer.Display.setBrightness(cfg.brightness); }

// ------------------------------------------------------------------ SD config file
static const char *CONFIG_PATH = "/capdeck/config.json";

static const char *CONFIG_TEMPLATE = R"JSON({
  "_readme": "CapDeck config. Edit on a computer, save, reboot the Cardputer.",
  "nickname": "Cardputer",
  "timezone": "EST5EDT,M3.2.0,M11.1.0",
  "wifi": [
    { "ssid": "YourNetwork", "pass": "YourPassword" }
  ],
  "home_assistant": {
    "url": "http://homeassistant.local:8123",
    "token": "PASTE_LONG_LIVED_ACCESS_TOKEN_HERE",
    "pipeline": "",
    "tts_engine": "tts.piper",
    "agent_id": ""
  },
  "local_ai": {
    "url": "http://192.168.1.100:8080",
    "model": "",
    "api_key": "",
    "system": "You are a helpful assistant answering on a tiny handheld screen. Keep replies short and plain-text."
  },
  "ha_buttons": [
    { "name": "Bedroom lights", "service": "light.toggle", "entity": "light.bedroom" },
    { "name": "Fish tank plug", "service": "switch.toggle", "entity": "switch.fish_tank" },
    { "name": "Movie scene", "service": "scene.turn_on", "entity": "scene.movie" },
    { "name": "Webhook test", "webhook": "capdeck_test" }
  ],
  "lora": {
    "freq_mhz": 915.0,
    "bw_khz": 125.0,
    "sf": 9,
    "cr": 7,
    "sync_word": 18,
    "power_dbm": 17
  }
})JSON";

void ensureDirs() {
  if (!sdOK) return;
  const char *dirs[] = {"/capdeck", "/capdeck/ir", "/capdeck/rf", "/capdeck/lora", "/capdeck/gps", "/capdeck/mesh", "/capdeck/map"};
  for (auto d : dirs)
    if (!SD.exists(d)) SD.mkdir(d);
}

bool loadConfigFile() {
  if (!sdOK) return false;
  ensureDirs();
  if (!SD.exists(CONFIG_PATH)) {
    File f = SD.open(CONFIG_PATH, FILE_WRITE);
    if (f) {
      f.print(CONFIG_TEMPLATE);
      f.close();
    }
  }
  File f = SD.open(CONFIG_PATH, FILE_READ);
  if (!f) return false;
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, f);
  f.close();
  if (err) return false;

  cfg.nick = doc["nickname"] | "Cardputer";
  cfg.tz = doc["timezone"] | "EST5EDT,M3.2.0,M11.1.0";
  cfg.extraWifi.clear();
  for (JsonObject w : doc["wifi"].as<JsonArray>()) {
    String s = w["ssid"] | "";
    if (s.length() && s != "YourNetwork") cfg.extraWifi.push_back({s, String(w["pass"] | "")});
  }
  JsonObject ha = doc["home_assistant"];
  cfg.haUrl = ha["url"] | "";
  while (cfg.haUrl.endsWith("/")) cfg.haUrl.remove(cfg.haUrl.length() - 1);
  cfg.haToken = ha["token"] | "";
  if (cfg.haToken.startsWith("PASTE_")) cfg.haToken = "";
  cfg.haPipeline = ha["pipeline"] | "";
  cfg.haTts = ha["tts_engine"] | "";
  cfg.haAgent = ha["agent_id"] | "";
  cfg.haButtons.clear();
  for (JsonObject b : doc["ha_buttons"].as<JsonArray>()) {
    HAButton hb;
    hb.name = b["name"] | "Button";
    hb.service = b["service"] | "";
    hb.entity = b["entity"] | "";
    hb.webhook = b["webhook"] | "";
    if (b["data"].is<JsonObject>()) serializeJson(b["data"], hb.data);
    cfg.haButtons.push_back(hb);
  }
  JsonObject ai = doc["local_ai"];
  cfg.aiUrl = ai["url"] | "";
  while (cfg.aiUrl.endsWith("/")) cfg.aiUrl.remove(cfg.aiUrl.length() - 1);
  if (cfg.aiUrl == "http://192.168.1.100:8080") cfg.aiUrl = "";  // untouched template
  cfg.aiModel = ai["model"] | "";
  cfg.aiKey = ai["api_key"] | "";
  cfg.aiSystem = ai["system"] | "You are a helpful assistant answering on a tiny handheld screen. Keep replies short and plain-text.";
  JsonObject lo = doc["lora"];
  cfg.loraFreq = lo["freq_mhz"] | 915.0f;
  cfg.loraBw = lo["bw_khz"] | 125.0f;
  cfg.loraSf = lo["sf"] | 9;
  cfg.loraCr = lo["cr"] | 7;
  cfg.loraSync = lo["sync_word"] | 0x12;
  cfg.loraPower = lo["power_dbm"] | 17;
  return true;
}

bool saveAiToConfig() {
  if (!sdOK) return false;
  JsonDocument doc;
  File f = SD.open(CONFIG_PATH, FILE_READ);
  if (f) {
    deserializeJson(doc, f);
    f.close();
  }
  JsonObject ai = doc["local_ai"].to<JsonObject>();
  ai["url"] = cfg.aiUrl;
  ai["model"] = cfg.aiModel;
  ai["api_key"] = cfg.aiKey;
  ai["system"] = cfg.aiSystem;
  f = SD.open(CONFIG_PATH, FILE_WRITE);
  if (!f) return false;
  serializeJsonPretty(doc, f);
  f.close();
  return true;
}

// ------------------------------------------------------------------ Wi-Fi
static NetState wifiState = NetState::OFF;
static uint32_t wifiStarted = 0;
static int wifiCandidate = 0;
static bool timeConfigured = false;

static std::vector<std::pair<String, String>> wifiCandidates() {
  std::vector<std::pair<String, String>> v;
  if (cfg.wifiSsid.length()) v.push_back({cfg.wifiSsid, cfg.wifiPass});
  for (auto &w : cfg.extraWifi)
    if (w.first != cfg.wifiSsid) v.push_back(w);
  return v;
}

NetState netState() { return wifiState; }

void wifiStart() {
  auto c = wifiCandidates();
  if (c.empty()) {
    wifiState = NetState::FAILED;
    return;
  }
  if (wifiState == NetState::UP || wifiState == NetState::CONNECTING) return;
  WiFi.mode(portalActive() ? WIFI_AP_STA : WIFI_STA);
  WiFi.setSleep(true);
  wifiCandidate = 0;
  WiFi.begin(c[0].first.c_str(), c[0].second.c_str());
  wifiState = NetState::CONNECTING;
  wifiStarted = millis();
}

void wifiStop() {
  if (portalActive()) {
    WiFi.disconnect(false);
    WiFi.mode(WIFI_AP);
  } else {
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
  }
  wifiState = NetState::OFF;
}

static void wifiTick() {
  if (wifiState == NetState::CONNECTING) {
    if (WiFi.status() == WL_CONNECTED) {
      wifiState = NetState::UP;
      if (!timeConfigured) {
        configTzTime(cfg.tz.c_str(), "pool.ntp.org", "time.nist.gov");
        timeConfigured = true;
      }
    } else if (millis() - wifiStarted > 9000) {
      auto c = wifiCandidates();
      wifiCandidate++;
      if (wifiCandidate < (int)c.size()) {
        WiFi.disconnect();
        WiFi.begin(c[wifiCandidate].first.c_str(), c[wifiCandidate].second.c_str());
        wifiStarted = millis();
      } else {
        wifiState = NetState::FAILED;
        WiFi.mode(portalActive() ? WIFI_AP : WIFI_OFF);
      }
    }
  } else if (wifiState == NetState::UP && WiFi.status() != WL_CONNECTED) {
    wifiState = NetState::CONNECTING;  // auto-reconnect is on by default
    wifiStarted = millis();
    wifiCandidate = 0;
  }
}

bool wifiWait(uint32_t ms, const char *why) {
  if (wifiState == NetState::UP) return true;
  if (wifiState != NetState::CONNECTING) wifiStart();
  uint32_t t = millis();
  int frame = 0;
  while (wifiState == NetState::CONNECTING && millis() - t < ms) {
    Key k = readKey();
    if (k.back) return false;
    drawHeader(why, accent(), ICON_WIFI);
    drawIcon(canvas, ICON_WIFI, SCR_W / 2, 74, 50, (frame / 4) % 2 ? accent() : col::DIM);
    canvas.setFont(&fonts::Font2);
    canvas.setTextDatum(middle_center);
    canvas.setTextColor(col::TEXT);
    auto c = wifiCandidates();
    String s = "Joining " + (wifiCandidate < (int)c.size() ? c[wifiCandidate].first : String("Wi-Fi"));
    canvas.drawString(s, SCR_W / 2, 112);
    canvas.setFont(&fonts::Font0);
    canvas.setTextColor(col::DIM);
    canvas.drawString("DEL to cancel", SCR_W / 2, 127);
    pushFrame();
    frame++;
    delay(60);
  }
  return wifiState == NetState::UP;
}

// ------------------------------------------------------------------ time
bool timeValid() {
  time_t now = time(nullptr);
  return now > 1700000000;
}

String timeStr(bool seconds) {
  time_t now = time(nullptr);
  struct tm t;
  localtime_r(&now, &t);
  char buf[12];
  strftime(buf, sizeof buf, seconds ? "%H:%M:%S" : "%H:%M", &t);
  return buf;
}

// ------------------------------------------------------------------ GPS
static TinyGPSPlus gps;
bool gpsSeen = false;
static bool gpsTimeSynced = false;
TinyGPSPlus &gpsRef() { return gps; }

bool gpsFix() { return gps.location.isValid() && gps.location.age() < 5000; }
int gpsSats() { return gps.satellites.isValid() ? gps.satellites.value() : 0; }

void gpsPoll() {
  while (Serial1.available()) {
    gps.encode(Serial1.read());
    gpsSeen = true;
  }
  // set the system clock from GPS if nothing better is available
  if (!gpsTimeSynced && gps.date.isValid() && gps.time.isValid() && gps.date.year() > 2023 &&
      gps.time.age() < 1500) {
    struct tm t = {};
    t.tm_year = gps.date.year() - 1900;
    t.tm_mon = gps.date.month() - 1;
    t.tm_mday = gps.date.day();
    t.tm_hour = gps.time.hour();
    t.tm_min = gps.time.minute();
    t.tm_sec = gps.time.second();
    setenv("TZ", "UTC0", 1);
    tzset();
    time_t utc = mktime(&t);
    setenv("TZ", cfg.tz.c_str(), 1);
    tzset();
    struct timeval tv = {utc, 0};
    settimeofday(&tv, nullptr);
    gpsTimeSynced = true;
  }
}

// ------------------------------------------------------------------ LoRa
static SX1262 *radio = nullptr;
bool loraPresent = false;
std::vector<LoraMsg> loraLog;
int loraUnread = 0;
static volatile bool loraFlag = false;
static void IRAM_ATTR loraISR() { loraFlag = true; }
SX1262 *loraRadio() { return radio; }

RadioMode radioMode = RM_CAPDECK;

static void applyRadioParams() {
  if (radioMode == RM_MESH) {
    const MeshPreset &p = MESH_PRESETS[cfg.meshPreset % MESH_PRESET_COUNT];
    radio->setFrequency(p.freq);
    radio->setBandwidth(p.bw);
    radio->setSpreadingFactor(p.sf);
    radio->setCodingRate(p.cr);
    radio->setSyncWord(0x2B);
    radio->setPreambleLength(16);
  } else {
    radio->setFrequency(cfg.loraFreq);
    radio->setBandwidth(cfg.loraBw);
    radio->setSpreadingFactor(cfg.loraSf);
    radio->setCodingRate(cfg.loraCr);
    radio->setSyncWord(cfg.loraSync);
    radio->setPreambleLength(8);
  }
  radio->setOutputPower(cfg.loraPower);
}

void loraSetMode(RadioMode m) {
  if (!loraPresent) { radioMode = m; return; }
  radio->standby();
  if (m == RM_SCAN) {
    radio->clearPacketReceivedAction();
    radioMode = m;
    return;
  }
  bool fromScan = radioMode == RM_SCAN;
  radioMode = m;
  if (fromScan) {
    radio->setCRC(true);
    radio->setPacketReceivedAction(loraISR);
  }
  applyRadioParams();
  loraFlag = false;
  radio->startReceive();
}

bool loraInit() {
  if (!radio) radio = new SX1262(new Module(PIN_LORA_CS, PIN_LORA_IRQ, PIN_LORA_RST, PIN_LORA_BUSY, sharedSPI));
  int st = radio->begin(cfg.loraFreq, cfg.loraBw, cfg.loraSf, cfg.loraCr, cfg.loraSync, cfg.loraPower, 8, 1.8, false);
  if (st != RADIOLIB_ERR_NONE) {
    // some modules have no TCXO - retry without it
    st = radio->begin(cfg.loraFreq, cfg.loraBw, cfg.loraSf, cfg.loraCr, cfg.loraSync, cfg.loraPower, 8, 0, false);
  }
  loraPresent = (st == RADIOLIB_ERR_NONE);
  if (!loraPresent) return false;
  radio->setDio2AsRfSwitch(true);
  radio->setCurrentLimit(140);
  radio->setCRC(true);
  radio->setPacketReceivedAction(loraISR);
  if (cfg.meshListen) radioMode = RM_MESH;
  applyRadioParams();
  radio->startReceive();
  return true;
}

void loraApplyParams() {
  if (!loraPresent || radioMode == RM_SCAN) return;
  radio->standby();
  applyRadioParams();
  loraFlag = false;
  radio->startReceive();
}

bool loraSend(const String &payload) {
  if (!loraPresent || radioMode != RM_CAPDECK) return false;
  radio->standby();
  int st = radio->transmit((uint8_t *)payload.c_str(), payload.length());
  loraFlag = false;  // transmit-done also fires DIO1
  radio->startReceive();
  if (st == RADIOLIB_ERR_NONE) {
    loraLog.push_back({payload, 0, 0, true, millis()});
    if (loraLog.size() > 60) loraLog.erase(loraLog.begin());
  }
  return st == RADIOLIB_ERR_NONE;
}

void loraPoll() {
  if (!loraPresent || !loraFlag || radioMode == RM_SCAN) return;
  loraFlag = false;
  size_t len = radio->getPacketLength();
  if (len > 0 && len < 256) {
    uint8_t buf[256];
    int st = radio->readData(buf, len);
    if (st == RADIOLIB_ERR_NONE && radioMode == RM_MESH) {
      meshHandlePacket(buf, len, radio->getRSSI(), radio->getSNR());
    } else if (st == RADIOLIB_ERR_NONE) {
      String s;
      for (size_t i = 0; i < len; i++) s += (buf[i] >= 32 && buf[i] < 127) ? (char)buf[i] : '.';
      loraLog.push_back({s, radio->getRSSI(), radio->getSNR(), false, millis()});
      if (loraLog.size() > 60) loraLog.erase(loraLog.begin());
      loraUnread++;
      beep(3000, 40);
      if (sdOK) {
        File f = SD.open("/capdeck/lora/log.txt", FILE_APPEND);
        if (f) {
          f.printf("[%s] %.0fdBm %s\n", timeValid() ? timeStr(true).c_str() : "--", radio->getRSSI(), s.c_str());
          f.close();
        }
      }
    }
  }
  radio->startReceive();
}

// ------------------------------------------------------------------ background tick
// last known position (persisted so the map has a centre before the GPS fixes)
static double lastLat = 0, lastLon = 0;
static bool lastPosLoaded = false;
static uint32_t lastPosSave = 0;
bool myPosition(double &lat, double &lon) {
  if (!lastPosLoaded) {
    prefs.begin("capdeck", true);
    lastLat = prefs.getDouble("lat", 0);
    lastLon = prefs.getDouble("lon", 0);
    prefs.end();
    lastPosLoaded = true;
  }
  if (gpsFix()) {
    lastLat = gps.location.lat();
    lastLon = gps.location.lng();
    if (millis() - lastPosSave > 300000 || lastPosSave == 0) {
      prefs.begin("capdeck", false);
      prefs.putDouble("lat", lastLat);
      prefs.putDouble("lon", lastLon);
      prefs.end();
      lastPosSave = millis();
    }
  }
  lat = lastLat;
  lon = lastLon;
  return lastLat != 0 || lastLon != 0;
}

double distanceM(double lat1, double lon1, double lat2, double lon2) {
  return TinyGPSPlus::distanceBetween(lat1, lon1, lat2, lon2);
}
double bearingDeg(double lat1, double lon1, double lat2, double lon2) {
  return TinyGPSPlus::courseTo(lat1, lon1, lat2, lon2);
}
String fmtDistance(double m) {
  // US units: feet under a tenth of a mile, then miles
  double mi = m / 1609.344;
  if (mi < 0.1) return String((int)(m * 3.28084)) + " ft";
  if (mi < 10) return String(mi, 1) + " mi";
  return String((int)mi) + " mi";
}
String fmtAge(uint32_t ms) {
  uint32_t s = ms / 1000;
  if (s < 60) return String(s) + "s";
  if (s < 3600) return String(s / 60) + "m";
  if (s < 86400) return String(s / 3600) + "h";
  return String(s / 86400) + "d";
}

void sysTick() {
  M5Cardputer.update();
  gpsPoll();
  loraPoll();
  wifiTick();
  portalPoll();
  static uint32_t lastMeshSave = 0;
  if (millis() - lastMeshSave > 30000) {
    lastMeshSave = millis();
    meshSaveIfDirty();
  }
}
