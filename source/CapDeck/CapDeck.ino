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

static const App APPS[] = {
    {"Home Assistant", "deck  -  talk  -  type", ICON_HA, &col::HA, appHomeAssistant},
    {"Macros", "one tap, many actions", ICON_MACRO, &col::HA, appMacros},
    {"Local AI", "chat with your own model", ICON_AI, &col::AI, appAI},
    {"Duck", "look anything up, no setup", ICON_DUCK, &col::AI, appDuck},
    {"LoRa", "chat  -  scanner  -  ping", ICON_LORA, &col::LORA, appLora},
    {"Mycelium", "Meshtastic nodes on a map", ICON_MESH, &col::MESH, appMesh},
    {"GPS", "position  -  satellites", ICON_GPS, &col::GPS, appGps},
    {"Infrared", "Flipper .ir remotes", ICON_IR, &col::IR, appIR},
    {"RF 433", "receive  -  scope  -  replay", ICON_RF, &col::RF, appRF},
    {"Portal", "phone control panel", ICON_PORTAL, &col::PORTAL, appPortal},
    {"Files", "SD card manager", ICON_FILES, &col::FILES, runFiles},
    {"Config", "theme  -  clock  -  wifi", ICON_CONF, &col::CONF, appSettings},
};
static const int APP_COUNT = sizeof(APPS) / sizeof(APPS[0]);

static uint16_t blend(uint16_t a, uint16_t b, float t) {  // t=1 -> a
  int ar = (a >> 11) & 31, ag = (a >> 5) & 63, ab = a & 31;
  int br = (b >> 11) & 31, bg = (b >> 5) & 63, bb = b & 31;
  return ((int)(br + (ar - br) * t) << 11) | ((int)(bg + (ag - bg) * t) << 5) | (int)(bb + (ab - bb) * t);
}

// ---- Spore: living mycelium (hyphae grow in, nutrient pulses travel outward, spores drift up)
static inline uint32_t hash32(uint32_t x) {  // deterministic noise, so wallpapers need no per-frame state
  x ^= x >> 16; x *= 0x7feb352d; x ^= x >> 15; x *= 0x846ca68b; x ^= x >> 16;
  return x;
}
static inline float frand(uint32_t seed) { return (hash32(seed) & 0xFFFF) / 65535.0f; }
namespace spore {
struct Seg { int16_t x0, y0, x1, y1; uint8_t depth; int16_t parent; };
static const int MAXSEG = 150;
static Seg seg[MAXSEG];
static int16_t kid[MAXSEG][2];
static int nseg = 0;
static uint32_t seed = 1;
static uint32_t bornAt = 0;
static uint32_t rnd() { seed = hash32(seed + 0x9E3779B9); return seed; }

static void grow(float x, float y, float ang, int depth, int parent) {
  if (nseg >= MAXSEG || depth > 10) return;
  float len = 7 + rnd() % 8;
  float nx = x + cosf(ang) * len, ny = y + sinf(ang) * len;
  if (nx < -6 || nx > SCR_W + 6 || ny < BAR_H + 2 || ny > SCR_H + 6) return;
  int me = nseg++;
  seg[me] = {(int16_t)x, (int16_t)y, (int16_t)nx, (int16_t)ny, (uint8_t)depth, (int16_t)parent};
  kid[me][0] = kid[me][1] = -1;
  if (parent >= 0) kid[parent][kid[parent][0] < 0 ? 0 : 1] = me;
  grow(nx, ny, ang + ((int)(rnd() % 41) - 20) * 0.0175f, depth + 1, me);
  if (rnd() % 100 < 36 && (parent < 0 || kid[me][1] < 0))
    grow(nx, ny, ang + ((rnd() & 1) ? 1 : -1) * (0.5f + (rnd() % 35) * 0.01f), depth + 1, me);
}

static void build() {
  nseg = 0;
  seed = esp_random();
  const float roots[][3] = {{18, SCR_H + 2, -1.35f}, {74, SCR_H + 2, -1.75f}, {150, SCR_H + 2, -1.45f},
                            {222, SCR_H + 2, -1.9f}, {-4, 70, -0.25f}, {SCR_W + 4, 58, 3.4f}};
  for (auto &r : roots) grow(r[0], r[1], r[2], 0, -1);
  bornAt = millis();
}

struct Pulse { int16_t s; float p; };
static Pulse pulses[7];
static uint32_t lastT = 0;

static int randomRoot() {
  for (int tries = 0; tries < 20; tries++) {
    int i = rnd() % max(1, nseg);
    if (seg[i].depth == 0) return i;
  }
  return 0;
}

void draw(uint32_t t) {
  static uint8_t builtFor = 255;
  if (!nseg || builtFor != cfg.theme) { build(); builtFor = cfg.theme; for (auto &p : pulses) { p.s = randomRoot(); p.p = frand(rnd()); } }
  uint16_t thread = blend(col::DIM, col::BG, 0.55f);
  uint16_t fresh = blend(col::TEXT, col::BG, 0.55f);
  uint32_t age = t - bornAt;
  for (int i = 0; i < nseg; i++) {
    const Seg &s = seg[i];
    int32_t start = s.depth * 170;
    if ((int32_t)age < start) continue;
    float f = min(1.0f, (age - start) / 170.0f);
    int x1 = s.x0 + (s.x1 - s.x0) * f, y1 = s.y0 + (s.y1 - s.y0) * f;
    canvas.drawLine(s.x0, s.y0, x1, y1, f < 1 ? fresh : thread);
  }
  // nutrient pulses flowing outward along the hyphae
  float dt = lastT ? min((uint32_t)120, t - lastT) / 1000.0f : 0;
  lastT = t;
  if (age > 1900) {
    uint16_t glow = blend(accent(), col::BG, 0.35f);
    for (auto &p : pulses) {
      const Seg &s = seg[p.s];
      float len = max(1.0f, hypotf(s.x1 - s.x0, s.y1 - s.y0));
      p.p += dt * 34.0f / len;
      if (p.p >= 1) {
        int next = kid[p.s][(rnd() & 1) && kid[p.s][1] >= 0 ? 1 : 0];
        if (next < 0) { p.s = randomRoot(); p.p = 0; }
        else { p.s = next; p.p -= 1; }
        continue;
      }
      int x = s.x0 + (s.x1 - s.x0) * p.p, y = s.y0 + (s.y1 - s.y0) * p.p;
      canvas.fillCircle(x, y, 2, glow);
      canvas.drawPixel(x, y, accent());
    }
  }
  // spores, drifting up and swaying
  uint16_t sporeC = blend(col::TEXT, col::BG, 0.5f);
  const int span = SCR_H - BAR_H;
  for (int i = 0; i < 18; i++) {
    float speed = 0.008f + frand(i * 7 + 1) * 0.012f;
    int y = SCR_H - (int)((t * speed + frand(i * 7 + 2) * span)) % span;
    int x = (int)(frand(i * 7 + 3) * SCR_W + 5 * sinf(t / 900.0f + i)) % SCR_W;
    canvas.drawPixel(x, y, sporeC);
    if (i % 3 == 0) canvas.drawPixel(x + 1, y, sporeC);
  }
}
}  // namespace spore

// theme-specific animated backgrounds behind the carousel
static void drawBackdrop(uint32_t t) {
  if (lowPowerMode()) return;  // low battery: flat background, no animation to feed
  switch (themeStyle()) {
    case STYLE_SYNTHWAVE: {  // neon horizon grid
      int hy = 88;
      for (int i = 0; i < 6; i++) canvas.drawFastHLine(0, hy - 30 + i * 5, SCR_W, i % 2 ? 0x1003 : 0x0802);
      canvas.drawFastHLine(0, hy, SCR_W, 0x780F);
      float ph = (t % 1000) / 1000.0f;
      for (int i = 0; i < 6; i++) {
        float z = (i + ph) / 6.0f;
        int y = hy + (int)(z * z * (SCR_H - hy));
        canvas.drawFastHLine(0, y, SCR_W, 0x5008);
      }
      for (int i = -8; i <= 8; i++) canvas.drawLine(SCR_W / 2 + i * 6, hy, SCR_W / 2 + i * 40, SCR_H, 0x5008);
      break;
    }
    case STYLE_MATRIX: {  // falling glyph rain
      static uint8_t drop[40];
      static bool init = false;
      if (!init) { for (auto &d : drop) d = esp_random() % 20; init = true; }
      canvas.setFont(&fonts::Font0);
      canvas.setTextDatum(top_left);
      for (int c = 0; c < 40; c++) {
        int head = (drop[c] + t / 90) % 24;
        for (int k = 0; k < 6; k++) {
          int row = head - k;
          if (row < 0) continue;
          canvas.setTextColor(k == 0 ? 0x5FEB : k < 3 ? 0x0320 : 0x0180);
          canvas.drawChar((char)(33 + ((c * 7 + row * 13) % 90)), c * 6, BAR_H + row * 9);
        }
      }
      break;
    }
    case STYLE_EMBER: {  // molten floor with embers drifting up
      for (int i = 0; i < 12; i++)
        canvas.drawFastHLine(0, SCR_H - 1 - i, SCR_W, blend(0xFC00, col::BG, 1.0f - i / 12.0f));
      for (int v = 0; v < 8; v++) {  // flickering vents
        int h = 3 + ((t / 80 + v * 3) % 8);
        canvas.drawFastVLine(12 + v * 31, SCR_H - h, h, 0xFDA0);
      }
      static uint8_t ex[24], ph[24];
      static bool einit = false;
      if (!einit) {
        for (int i = 0; i < 24; i++) { ex[i] = esp_random() % 240; ph[i] = esp_random() % 120; }
        einit = true;
      }
      const int span = SCR_H - BAR_H;
      for (int i = 0; i < 24; i++) {
        int y = SCR_H - 1 - ((ph[i] + t / 28) % span);
        if (y < BAR_H + 2) continue;
        int x = (ex[i] + ((t / 200 + i) % 5)) % SCR_W;
        canvas.fillCircle(x, y, 1, y > 105 ? 0xFDC0 : y > 75 ? 0xFB20 : 0xA800);
      }
      break;
    }
    case STYLE_SPORE: spore::draw(t); break;
    case STYLE_CRT:  // scanlines
      for (int y = BAR_H; y < SCR_H; y += 3) canvas.drawFastHLine(0, y, SCR_W, 0x1000);
      break;
    default: break;
  }
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
  if (themeStyle() == STYLE_MATRIX || themeStyle() == STYLE_EMBER)
    canvas.fillRect(0, 92, SCR_W, 32, col::BG);  // keep the rain / embers off the title
  if (themeStyle() == STYLE_SYNTHWAVE) {  // chromatic split title
    canvas.setTextColor(0xF81F);
    canvas.drawString(a.name, SCR_W / 2 - 1, 104);
    canvas.setTextColor(0x07FF);
    canvas.drawString(a.name, SCR_W / 2 + 1, 104);
  }
  canvas.setTextColor(col::TEXT);
  canvas.drawString(a.name, SCR_W / 2, 104);
  canvas.setFont(&fonts::Font0);
  canvas.setTextColor(a.color());
  canvas.drawString(a.sub, SCR_W / 2, 119);
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
    for (int i = 0; i < APP_COUNT; i++)
      drawIcon(canvas, APPS[i].icon, 12 + i * (SCR_W - 24) / (APP_COUNT - 1), 26, 18,
               i <= upto * 2 ? APPS[i].color() : col::FAINT);
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
