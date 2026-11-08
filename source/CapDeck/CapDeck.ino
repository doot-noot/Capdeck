// =====================================================================
//  CapDeck  -  custom firmware for the M5Stack Cardputer ADV
//  Home Assistant deck + voice, Cap LoRa-1262 chat, GPS, IR, 433 MHz,
//  file manager and a settings hub. Built to be installed via M5Launcher.
// =====================================================================
#include "core.h"

bool needsFirstRun();
void firstRunWizard();

struct App {
  const char *name;
  const char *sub;
  uint8_t icon;
  uint16_t *colorRef;  // points at the live theme colour
  void (*run)();
  uint16_t color() const { return *colorRef; }
};

static void runFiles() { appFiles("/"); }

static void runMap() { appMap(-1); }

static const App APPS[] = {
    {"Home Assistant", "deck  -  talk  -  type", ICON_HA, &col::HA, appHomeAssistant},
    {"Macros", "one tap, many actions", ICON_MACRO, &col::HA, appMacros},
    {"Local AI", "chat with your own model", ICON_AI, &col::AI, appAI},
    {"Mycorrhiza", "downloads  -  lookups", ICON_MYCO, &col::AI, appMycorrhiza},
    {"LoRaWAN", "chat  -  scanner  -  ping", ICON_LORA, &col::LORA, appLora},
    {"Mycelium", "Meshtastic nodes", ICON_MESH, &col::MESH, appMesh},
    {"Rhizomorph", "Reticulum radio for Sideband", ICON_RHIZO, &col::LORA, appRhizomorph},
    {"Forage", "collect spores nearby", ICON_FORAGE, &col::GPS, appForage},
    {"Map", "roads, nodes and finds", ICON_MAP, &col::MESH, runMap},
    {"Nightcap", "the sky above you", ICON_NIGHTCAP, &col::PORTAL, appNightcap},
    {"GPS", "position  -  satellites", ICON_GPS, &col::GPS, appGps},
    {"Infrared", "Flipper .ir remotes", ICON_IR, &col::IR, appIR},
    {"RF 433", "receive  -  scope  -  replay", ICON_RF, &col::RF, appRF},
    {"Portal", "phone control panel", ICON_PORTAL, &col::PORTAL, appPortal},
    {"Files", "SD card manager", ICON_FILES, &col::FILES, runFiles},
    {"Config", "theme  -  power  -  wifi", ICON_CONF, &col::CONF, appSettings},
};
static const int APP_COUNT = sizeof(APPS) / sizeof(APPS[0]);

static uint16_t blend(uint16_t a, uint16_t b, float t) { return blend565(a, b, t); }

// Outlined text: readable over any live wallpaper without hiding it behind a box.
static void outlined(const char *s, int x, int y, uint16_t fg) {
  canvas.setTextColor(col::BG);
  for (int dx = -1; dx <= 1; dx++)
    for (int dy = -1; dy <= 1; dy++)
      if (dx || dy) canvas.drawString(s, x + dx, y + dy);
  canvas.setTextColor(fg);
  canvas.drawString(s, x, y);
}

// pos is fractional so the carousel can glide between apps
static void drawCarousel(float pos) {
  canvas.fillScreen(col::BG);
  drawBackdrop(millis());
  int cur = ((int)lroundf(pos) % APP_COUNT + APP_COUNT) % APP_COUNT;
  const App &a = APPS[cur];
  // soft glow behind the focused icon
  for (int r = 40; r > 26; r -= 3) canvas.fillCircle(SCR_W / 2, 62, r, blend(a.color(), col::BG, 0.06f + (40 - r) * 0.012f));
  canvas.drawCircle(SCR_W / 2, 62, 40, blend(a.color(), col::BG, 0.35f));
  for (int k = -2; k <= 2; k++) {
    int idx = (((int)floorf(pos) + k) % APP_COUNT + APP_COUNT) % APP_COUNT;
    float dx = (floorf(pos) + k - pos) * 82.0f;
    float d = fminf(1.0f, fabsf(dx) / 82.0f);
    int size = 56 - (int)(d * 26);
    int x = SCR_W / 2 + (int)dx;
    if (x < -30 || x > SCR_W + 30) continue;
    uint16_t c = blend(APPS[idx].color(), col::BG, 1.0f - d * 0.65f);
    uint16_t bg = d < 0.5f ? blend(a.color(), col::BG, 0.2f) : col::BG;
    drawIcon(canvas, APPS[idx].icon, x, 62, size, c, bg);
  }
  // chevrons
  canvas.fillTriangle(6, 62, 13, 55, 13, 69, col::FAINT);
  canvas.fillTriangle(SCR_W - 6, 62, SCR_W - 13, 55, SCR_W - 13, 69, col::FAINT);
  // name + subtitle
  canvas.setFont(&fonts::FreeSansBold12pt7b);
  canvas.setTextDatum(middle_center);
  if (themeStyle() == STYLE_MATRIX) canvas.fillRect(0, 92, SCR_W, 32, col::BG);  // the rain is too busy for text
  if (themeStyle() == STYLE_SYNTHWAVE) {  // chromatic split title
    canvas.setTextColor(0xF81F);
    canvas.drawString(a.name, SCR_W / 2 - 1, 104);
    canvas.setTextColor(0x07FF);
    canvas.drawString(a.name, SCR_W / 2 + 1, 104);
    canvas.setTextColor(col::TEXT);
    canvas.drawString(a.name, SCR_W / 2, 104);
  } else {
    outlined(a.name, SCR_W / 2, 104, col::TEXT);
  }
  canvas.setFont(&fonts::Font0);
  outlined(a.sub, SCR_W / 2, 119, a.color());
  // page dots
  int dw = APP_COUNT * 9;
  for (int i = 0; i < APP_COUNT; i++) {
    int x = SCR_W / 2 - dw / 2 + i * 9 + 4;
    if (i == cur) canvas.fillRoundRect(x - 3, 128, 7, 4, 2, a.color());
    else canvas.fillCircle(x, 130, 1, col::DIM);
  }
  drawStatusBar("CAPDECK", accent());
  pushFrame();
}

static void bootScreen() {
  struct Step {
    const char *label;
    String result;
    uint16_t color;
  } steps[4];
  auto draw = [&](int upto) {
    canvas.fillScreen(col::BG);
    for (int i = 0; i < APP_COUNT; i++)  // the icons light up one by one as each check passes
      drawIcon(canvas, APPS[i].icon, 10 + i * (SCR_W - 20) / (APP_COUNT - 1), 26, 13,
               i <= (upto + 1) * APP_COUNT / 4 ? APPS[i].color() : col::FAINT);
    canvas.setFont(&fonts::FreeSansBold12pt7b);
    canvas.setTextDatum(middle_left);
    canvas.setTextColor(col::TEXT);
    canvas.drawString("CapDeck", 8, 56);
    canvas.setFont(&fonts::Font0);
    canvas.setTextColor(accent());
    canvas.drawString("v" CAPDECK_VERSION "  Cardputer ADV", 118, 58);
    for (int i = 0; i <= upto && i < 4; i++) {
      canvas.setTextColor(col::DIM);
      canvas.drawString(steps[i].label, 10, 76 + i * 13);
      canvas.setTextColor(steps[i].color);
      canvas.drawString(steps[i].result, 110, 76 + i * 13);
    }
    pushFrame();
  };
  steps[0].label = "microSD";
  steps[1].label = "config.json";
  steps[2].label = "Cap LoRa-1262";
  steps[3].label = "Wi-Fi";

  draw(-1);
  pinMode(PIN_LORA_CS, OUTPUT);
  digitalWrite(PIN_LORA_CS, HIGH);  // keep the radio off the bus while the SD mounts
  sharedSPI.begin(PIN_SPI_SCK, PIN_SPI_MISO, PIN_SPI_MOSI, PIN_SD_CS);
  sdOK = SD.begin(PIN_SD_CS, sharedSPI, 20000000);
  steps[0].result = sdOK ? humanSize(SD.cardSize()) + " ok" : "not found";
  steps[0].color = sdOK ? col::OK : col::BAD;
  draw(0);

  bool conf = loadConfigFile();
  steps[1].result = !sdOK ? "skipped" : conf ? "loaded" : "invalid JSON";
  steps[1].color = conf ? col::OK : col::WARN;
  setenv("TZ", cfg.tz.c_str(), 1);
  tzset();
  draw(1);

  bool lora = loraInit();
  bool rxDead = loraInitLog.indexOf("WARNING") >= 0;
  steps[2].result = !lora ? "not attached" : rxDead ? "RX dead? see Radio test" : "ok  TCXO " + String(loraTcxoUsed, 1) + "V";
  steps[2].color = !lora ? col::DIM : rxDead ? col::WARN : col::OK;
  Serial1.begin(115200, SERIAL_8N1, PIN_GPS_RX, PIN_GPS_TX);
  meshLoad();
  macroEnsureTemplate();
  draw(2);

  if (cfg.autoWifi && (cfg.wifiSsid.length() || cfg.extraWifi.size())) {
    wifiStart();
    steps[3].result = "connecting...";
    steps[3].color = col::WARN;
  } else {
    steps[3].result = "off";
    steps[3].color = col::DIM;
  }
  draw(3);
  delay(700);
}

void setup() {
  powerBootCheck();  // before anything else: a stray pocket press can go straight back to sleep
  auto mcfg = M5.config();
  M5Cardputer.begin(mcfg, true);
  M5Cardputer.Display.setRotation(1);
  canvas.setColorDepth(16);
  canvas.createSprite(SCR_W, SCR_H);
  loadSettings();
  applyTheme(cfg.theme);
  applyBrightness();
  ledApply();
  M5Cardputer.Speaker.setVolume(cfg.volume);
  bootScreen();
  wakeScreen();
  if (needsFirstRun()) firstRunWizard();
  powerAfterBoot();
}

void loop() {
  static float pos = 0;
  static int target = 0;
  static uint32_t lastDraw = 0;
  Key k = readKey();
  if (k.left || k.up) { target--; beep(1900, 8); }
  if (k.right || k.down || k.tab) { target++; beep(1900, 8); }
  if ((k.ch >= '1' && k.ch <= '9') || k.ch == '0') {
    int want = k.ch == '0' ? 9 : k.ch - '1';
    if (want >= APP_COUNT) want = APP_COUNT - 1;
    int cur = ((target % APP_COUNT) + APP_COUNT) % APP_COUNT;
    target += want - cur;
  }
  if (k.enter) {
    int cur = ((target % APP_COUNT) + APP_COUNT) % APP_COUNT;
    pos = target;
    beep(2600, 25);
    tipsOpened(APPS[cur].name);
    APPS[cur].run();
    lastDraw = 0;
  }
  // glide toward target
  if (fabsf(pos - target) > 0.01f) {
    pos += (target - pos) * 0.35f;
    if (fabsf(pos - target) < 0.02f) pos = target;
    drawCarousel(pos);
    lastDraw = millis();
  } else if (millis() - lastDraw > (themeStyle() == STYLE_PLAIN || lowPowerMode() ? 1000u : 60u)) {
    drawCarousel(pos);
    lastDraw = millis();
  }
  delay(12);
}
