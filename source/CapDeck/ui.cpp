// CapDeck - UI toolkit: input, status bar, lists, text entry, popups
#include "core.h"

FitCanvas canvas(&M5Cardputer.Display);

// The safety net for every line of text on the device: if it would run off the screen edge, shorten it with "..".
// Only steps in when the anchor point is on screen (text deliberately parked off-screen is left alone).
size_t FitCanvas::drawString(const char *s, int32_t x, int32_t y) {
  if (!fit || !s || !*s || x < 0 || x > width()) return M5Canvas::drawString(s, x, y);
  const int32_t W = width(), w = textWidth(s);
  const int h = getTextDatum() & 3;  // 0 left, 1 centre, 2 right
  const int32_t left = h == 0 ? x : h == 1 ? x - w / 2 : x - w;
  if (left >= 0 && left + w <= W) return M5Canvas::drawString(s, x, y);
  int32_t avail = (h == 0 ? W - x : h == 2 ? x : 2 * min(x, W - x)) - 1;
  const int32_t dots = textWidth("..");
  if (avail <= dots) return 0;
  char buf[160];
  size_t n = strnlen(s, sizeof buf - 3);
  auto snap = [&](size_t m) {  // never cut a multi-byte UTF-8 character in half
    while (m > 0 && ((uint8_t)s[m] & 0xC0) == 0x80) m--;
    return m;
  };
  size_t lo = 0, hi = n;
  while (lo < hi) {
    size_t mid = (lo + hi + 1) / 2, m = snap(mid);
    memcpy(buf, s, m);
    buf[m] = 0;
    if (textWidth(buf) + dots <= avail) lo = mid;
    else hi = mid - 1;
  }
  size_t m = snap(lo);
  while (m > 0 && s[m - 1] == ' ') m--;
  memcpy(buf, s, m);
  strcpy(buf + m, "..");
  return M5Canvas::drawString(buf, x, y);
}

// ------------------------------------------------------------------ themes
// Every theme is alive (its own wallpaper, see backdrops.cpp) and one colour each. Still life freezes them.
#define C565(rgb) (uint16_t)((((rgb) >> 8) & 0xF800) | (((rgb) >> 5) & 0x07E0) | (((rgb) >> 3) & 0x001F))
#define T(name, still, bg, pn, p2, tx, dm, fa, ha, ai, lo, me, gp, ir, rf, po, fi, co, ac, st, led)                      \
  {name, still, C565(bg), C565(pn), C565(p2), C565(tx), C565(dm), C565(fa), C565(ha), C565(ai), C565(lo), C565(me),       \
   C565(gp), C565(ir), C565(rf), C565(po), C565(fi), C565(co), C565(ac), st, led}
const Theme THEMES[] = {
    //  living        still       bg        panel     panel2    text      dim       faint     | ha        ai        lora      mesh      gps       ir        rf        portal    files     conf    | accent    style            LED
    T("Spore", "Lime", 0x000000, 0x0A140C, 0x14261A, 0xD8F5DC, 0x6FA07A, 0x2A4A30, 0x7CFF9E, 0xB6FFCB, 0x39FF6A, 0x5CE68A, 0xA8F0B8, 0x39D46A, 0x8CFFB0, 0x39FF6A, 0xC8F5D0, 0x6FA07A, 0x39FF6A, STYLE_SPORE, 0x14E628),
    T("Spore Light", "Bone", 0xE6EDE1, 0xD2DECD, 0xBFD0BA, 0x0F2A14, 0x46664C, 0xA2B8A0, 0x1E7A36, 0x2F6B40, 0x1E8A3A, 0x287A44, 0x3D7A2F, 0x2E6B3A, 0x4F7A1E, 0x1E8A3A, 0x5A6B1E, 0x46664C, 0x1E8A3A, STYLE_SPORE, 0x14E628),
    T("Matrix", "Green", 0x000000, 0x002400, 0x003C00, 0xA8FFA8, 0x009000, 0x006000, 0x00FC40, 0x58FC58, 0x00FC00, 0x38FC30, 0x98FC98, 0x00F820, 0x60FC60, 0x00FC00, 0xB0FCB0, 0x00BC00, 0x00FC00, STYLE_MATRIX, 0x00FF30),
    T("Ocean", "Blue", 0x020A18, 0x06162E, 0x0C2444, 0xDCEBFF, 0x6F8FB8, 0x1F3657, 0x5FB8FF, 0x8FD0FF, 0x3F7BFF, 0x3FE0D0, 0x7FE8FF, 0x6F9BFF, 0x4FC8FF, 0x3FA9FF, 0xB8DCFF, 0x8FA8C8, 0x3FA9FF, STYLE_OCEAN, 0x1060FF),
    T("Lava", "Red", 0x140302, 0x2A0806, 0x3E0E0A, 0xFFE8DC, 0xC0806A, 0x4A1A12, 0xFF7A3A, 0xFFB040, 0xFF4A6A, 0xFF8A50, 0xFFC060, 0xFF3020, 0xFF9A30, 0xFFD080, 0xFFC080, 0xC89A88, 0xFF4A1A, STYLE_EMBER, 0xFF2000),
    T("Amber", "Orange", 0x0A0500, 0x201000, 0x301800, 0xFFD9A0, 0xB07A30, 0x402810, 0xFFB040, 0xFFC860, 0xFF9020, 0xFFB040, 0xFFD080, 0xFF8020, 0xFFA840, 0xFFC860, 0xFFD080, 0xC89050, 0xFFA020, STYLE_CRT, 0xFF7000),
    T("Storm", "Yellow", 0x0A0B10, 0x181A24, 0x242838, 0xF2F2E8, 0x9A9AA8, 0x30344A, 0xFFE040, 0xFFF080, 0xD8E0FF, 0xFFD000, 0xFFF6A0, 0xFFC020, 0xFFE870, 0xB0C8FF, 0xFFF0B0, 0xA8ACC0, 0xFFE040, STYLE_STORM, 0xFFC800),
    T("Cyber", "Magenta", 0x000010, 0x100030, 0x200048, 0xD8FCF8, 0x8888F8, 0x300048, 0x00FCF8, 0xF800F8, 0xF800F8, 0x00FCF8, 0xF8FC00, 0xF800F8, 0xF8FC00, 0x00FCF8, 0xF8FC00, 0xB0B0F8, 0xF800F8, STYLE_SYNTHWAVE, 0xB000FF),
    T("Aurora", "Purple", 0x06040F, 0x140C26, 0x22163A, 0xEDE4FF, 0x9A88C0, 0x2E2248, 0x8A6AFF, 0xC49AFF, 0xB06AFF, 0x4AF0B0, 0x7AE0FF, 0xE06AFF, 0xFF8AE0, 0x6AD8FF, 0xD8C8FF, 0xA898C8, 0xB06AFF, STYLE_AURORA, 0x9030FF),
    T("Abyss", "Cyan", 0x000A0C, 0x00181C, 0x00262C, 0xD8FFFC, 0x5FA8A8, 0x103638, 0x20D8F0, 0x80FFF0, 0x20F0E0, 0x40FFC0, 0xA0FFF8, 0x20B0FF, 0x60E0D0, 0x20F0E0, 0xB0FFF8, 0x70A8A8, 0x20F0E0, STYLE_ABYSS, 0x00E0D0),
    T("Void", "White", 0x000000, 0x141414, 0x222222, 0xF4F4F4, 0x9A9A9A, 0x333333, 0xFFFFFF, 0xDADADA, 0xC8C8C8, 0xEDEDED, 0xBEBEBE, 0xF0F0F0, 0xD4D4D4, 0xFFFFFF, 0xE0E0E0, 0xA8A8A8, 0xFFFFFF, STYLE_VOID, 0xC0C0C0),
};
#undef T
const int THEME_COUNT = sizeof(THEMES) / sizeof(THEMES[0]);

uint16_t blend565(uint16_t a, uint16_t b, float t) {  // t=1 -> a
  int ar = (a >> 11) & 31, ag = (a >> 5) & 63, ab = a & 31;
  int br = (b >> 11) & 31, bg = (b >> 5) & 63, bb = b & 31;
  return ((int)(br + (ar - br) * t) << 11) | ((int)(bg + (ag - bg) * t) << 5) | (int)(bb + (ab - bb) * t);
}

static int lum565(uint16_t c) {
  int r = ((c >> 11) & 0x1F) * 255 / 31, g = ((c >> 5) & 0x3F) * 255 / 63, b = (c & 0x1F) * 255 / 31;
  return (r * 299 + g * 587 + b * 114) / 1000;
}

void applyTheme(uint8_t idx) {
  const Theme &t = THEMES[idx % THEME_COUNT];
  col::BG = t.bg; col::PANEL = t.panel; col::PANEL2 = t.panel2;
  col::TEXT = t.text; col::DIM = t.dim; col::FAINT = t.faint;
  col::HA = t.ha; col::AI = t.ai; col::LORA = t.lora; col::MESH = t.mesh; col::GPS = t.gps;
  col::IR = t.ir; col::RF = t.rf; col::PORTAL = t.portal; col::FILES = t.files; col::CONF = t.conf;
  bool light = lum565(t.bg) > 128;
  col::BAR = light ? t.panel2 : 0x0020;
  if (t.style == STYLE_MATRIX || t.style == STYLE_CRT) {  // monochrome themes keep status colours on-palette
    col::OK = t.ha; col::WARN = t.rf; col::BAD = t.style == STYLE_CRT ? 0xF9A0 : 0xF8C3;
  } else if (light) {  // darker status colours that read on a pale background
    col::OK = 0x1C64; col::WARN = 0xA3E0; col::BAD = 0xB0A2;
  } else {
    col::OK = 0x2FE6; col::WARN = 0xFE60; col::BAD = 0xF8C3;
  }
}
ThemeStyle themeStyle() {
  if (cfg.stillLife || lowPowerMode()) return STYLE_PLAIN;
  return THEMES[cfg.theme % THEME_COUNT].style;
}
const char *themeName(int idx) {
  const Theme &t = THEMES[idx % THEME_COUNT];
  return cfg.stillLife ? t.still : t.name;
}

static const uint16_t ACCENTS[] = {0x07FF, 0xF81F, 0x07E0, 0xFD20, 0x3D7F, 0xF8A6};
uint16_t accent() {
  if (cfg.accentIdx == 0) return THEMES[cfg.theme % THEME_COUNT].accent;
  return ACCENTS[(cfg.accentIdx - 1) % (sizeof(ACCENTS) / sizeof(ACCENTS[0]))];
}

// ------------------------------------------------------------------ input
static Key lastKey;
static bool keyHeld = false;
static uint32_t heldSince = 0, lastRepeat = 0;

static Key decode(Keyboard_Class::KeysState &st) {
  Key k;
  k.fn = st.fn;
  k.enter = st.enter;
  k.tab = st.tab;
  k.space = st.space;
  k.del = st.backspace || st.del;
  if (!st.word.empty()) k.ch = st.word.back();
  if (st.fn) {
    k.up = st.up;
    k.down = st.down;
    k.left = st.left;
    k.right = st.right;
    k.esc = st.esc;
    k.ch = 0;
  } else {
    switch (k.ch) {
      case ';': k.up = true; break;
      case '.': k.down = true; break;
      case ',': k.left = true; break;
      case '/': k.right = true; break;
      case '`': k.esc = true; break;
    }
  }
  k.back = k.del || k.esc;
  k.any = k.enter || k.tab || k.del || k.esc || k.up || k.down || k.left || k.right || k.ch || k.space;
  return k;
}

Key readKey() {
  sysTick();
  Key k;
  auto &kb = M5Cardputer.Keyboard;
  uint32_t now = millis();
  if (kb.isChange()) {
    if (kb.isPressed()) {
      k = decode(kb.keysState());
      keyHeld = k.any;
      lastKey = k;
      heldSince = lastRepeat = now;
    } else {
      keyHeld = false;
    }
  } else if (keyHeld && kb.isPressed()) {
    bool repeatable = lastKey.up || lastKey.down || lastKey.left || lastKey.right || lastKey.del;
    if (repeatable && now - heldSince > 380 && now - lastRepeat > 70) {
      k = lastKey;
      k.repeat = true;
      lastRepeat = now;
    }
  }
  if (M5Cardputer.BtnA.wasPressed()) {
    k = Key();
    k.enter = k.any = true;
  }
  if (k.any) {
    if (powerOnKey()) return Key();  // first press only wakes the screen
  } else {
    powerIdle();  // dim -> screen off -> doze -> Spore mode (power.cpp)
  }
  return k;
}

bool talkKeyHeld() {
  auto &st = M5Cardputer.Keyboard.keysState();
  return st.space || st.enter || M5Cardputer.BtnA.isPressed();
}

void beep(int freq, int ms) {
  if (cfg.sound) M5Cardputer.Speaker.tone(freq, ms);
}

void pushFrame() { canvas.pushSprite(0, 0); }

String humanSize(uint64_t b) {
  char buf[16];
  if (b < 1024) snprintf(buf, sizeof buf, "%uB", (unsigned)b);
  else if (b < 1024 * 1024) snprintf(buf, sizeof buf, "%.1fK", b / 1024.0);
  else if (b < 1024ULL * 1024 * 1024) snprintf(buf, sizeof buf, "%.1fM", b / 1048576.0);
  else snprintf(buf, sizeof buf, "%.1fG", b / 1073741824.0);
  return buf;
}

// ------------------------------------------------------------------ fit / contrast helpers
// Nothing should ever run off a 240px screen. Truncate to fit the current font.
String fitText(const String &s, int maxW) {
  if (canvas.textWidth(s) <= maxW) return s;
  String t = s;
  while (t.length() > 1 && canvas.textWidth(t + "..") > maxW) t.remove(t.length() - 1);
  return t + "..";
}

// Black or white, whichever is readable on `bg`. Keeps bubbles/rows legible in every theme.
uint16_t textOn(uint16_t bg) { return lum565(bg) > 140 ? 0x0000 : 0xFFFF; }

bool batteryCharging() {
  // (int) cast keeps this working whether the core returns bool or an is_charging_t enum.
  return (int)M5Cardputer.Power.isCharging() == 1;
}

// Cached: reading the battery fuel gauge every frame is wasted I2C traffic.
bool lowPowerMode() {
  static uint32_t at = 0;
  static bool low = false;
  if (!at || millis() - at > 5000) {
    at = millis();
    low = M5Cardputer.Power.getBatteryLevel() <= 15 && !batteryCharging();
  }
  return low;
}

// ------------------------------------------------------------------ status bar
void drawBattery(LovyanGFX &g, int x, int y, int level, uint16_t c) {
  g.drawRoundRect(x, y, 18, 9, 2, c);
  g.fillRect(x + 18, y + 2, 2, 5, c);
  int w = constrain(level, 0, 100) * 14 / 100;
  uint16_t fill = level > 50 ? col::OK : level > 20 ? col::WARN : col::BAD;
  if (w > 0) g.fillRect(x + 2, y + 2, w, 5, fill);
  if (batteryCharging()) {  // the cap's lightning bolt, striking the cell
    g.fillTriangle(x + 11, y + 1, x + 6, y + 5, x + 9, y + 5, 0xFFE0);
    g.fillTriangle(x + 8, y + 8, x + 13, y + 4, x + 10, y + 4, 0xFFE0);
  }
}

void drawWifiGlyph(LovyanGFX &g, int x, int y, int bars, uint16_t c) {
  for (int i = 0; i < 4; i++) {
    int h = 2 + i * 2;
    g.fillRect(x + i * 3, y + 9 - h, 2, h, i < bars ? c : col::FAINT);
  }
}

void drawStatusBar(const char *title, uint16_t color) {
  canvas.fillRect(0, 0, SCR_W, BAR_H, col::BAR);
  canvas.drawFastHLine(0, BAR_H - 1, SCR_W, col::FAINT);
  canvas.setFont(&fonts::Font0);
  canvas.setTextSize(1);
  canvas.setTextDatum(middle_left);
  canvas.setTextColor(col::TEXT);
  String clk = timeValid() ? timeStr() : String("--:--");
  canvas.drawString(clk, 4, 7);
  if (devMode()) {  // live free-heap meter for the curious (placed after the clock, never on top of it)
    uint32_t kb = ESP.getFreeHeap() / 1024;
    canvas.setTextColor(kb < 40 ? col::BAD : kb < 80 ? col::WARN : col::OK);
    canvas.drawString(String(kb) + "k", 4 + canvas.textWidth(clk) + 5, 7);
  }
  if (title && *title) {
    canvas.setTextDatum(middle_center);
    canvas.setTextColor(color);
    canvas.drawString(fitText(title, 120), SCR_W / 2, 7);  // never overlap the clock or the icons
  }
  // right side icons, laid out right-to-left
  int x = SCR_W - 4;
  int lvl = M5Cardputer.Power.getBatteryLevel();
  x -= 20;
  drawBattery(canvas, x, 2, lvl, col::TEXT);
  canvas.setTextDatum(middle_right);
  canvas.setTextColor(col::DIM);
  char pct[6];
  snprintf(pct, sizeof pct, "%d%%", lvl);
  canvas.drawString(pct, x - 2, 7);
  x -= canvas.textWidth(pct) + 6;
  if (netState() == NetState::UP) {
    int rssi = WiFi.RSSI();
    int bars = rssi > -55 ? 4 : rssi > -67 ? 3 : rssi > -78 ? 2 : 1;
    x -= 12;
    drawWifiGlyph(canvas, x, 2, bars, col::OK);
    x -= 4;
  } else if (netState() == NetState::CONNECTING) {
    x -= 12;
    drawWifiGlyph(canvas, x, 2, (millis() / 300) % 5, col::WARN);
    x -= 4;
  }
  if (gpsFix()) {
    x -= 8;
    canvas.fillCircle(x + 3, 6, 3, col::GPS);
    canvas.fillTriangle(x, 7, x + 6, 7, x + 3, 12, col::GPS);
    canvas.fillCircle(x + 3, 6, 1, col::BAR);
    x -= 4;
  }
  if (portalActive()) {
    x -= 9;
    canvas.fillCircle(x + 4, 7, 2, col::PORTAL);
    canvas.drawCircle(x + 4, 7, 4, col::PORTAL);
    x -= 3;
  }
  if (radioMode == RM_MESH) {
    x -= 10;
    canvas.drawLine(x, 10, x + 4, 3, col::MESH);
    canvas.drawLine(x + 4, 3, x + 8, 10, col::MESH);
    canvas.drawLine(x, 10, x + 8, 10, col::MESH);
    canvas.fillCircle(x + 4, 3, 1, col::MESH);
    x -= 3;
  }
  if (loraUnread > 0) {
    x -= 12;
    canvas.drawRect(x, 3, 11, 8, col::LORA);
    canvas.drawLine(x, 3, x + 5, 7, col::LORA);
    canvas.drawLine(x + 10, 3, x + 5, 7, col::LORA);
    x -= 4;
  }
}

void drawHeader(const char *title, uint16_t color, uint8_t icon) {
  canvas.fillScreen(col::BG);
  drawStatusBar(nullptr, color);
  canvas.fillRect(0, BAR_H, SCR_W, 18, col::PANEL);
  canvas.fillRect(0, BAR_H + 17, SCR_W, 1, color);
  int tx = 6;
  if (icon != 255) {
    drawIcon(canvas, icon, 13, BAR_H + 9, 14, color, col::PANEL);
    tx = 24;
  }
  canvas.setFont(&fonts::FreeSansBold9pt7b);
  canvas.setTextDatum(middle_left);
  canvas.setTextColor(col::TEXT);
  canvas.drawString(fitText(title, SCR_W - tx - 48), tx, BAR_H + 9);  // leave room for "3/12" or a status
}

// ------------------------------------------------------------------ text wrap / popups
void wrapText(const String &text, int width, std::vector<String> &out) {
  int start = 0;
  int n = text.length();
  while (start <= n) {
    int nl = text.indexOf('\n', start);
    String para = text.substring(start, nl < 0 ? n : nl);
    // greedy wrap of one paragraph
    while (true) {
      if (canvas.textWidth(para) <= width) {
        out.push_back(para);
        break;
      }
      int cut = para.length();
      while (cut > 1 && canvas.textWidth(para.substring(0, cut)) > width) cut--;
      int sp = para.lastIndexOf(' ', cut);
      if (sp > 0) cut = sp;
      out.push_back(para.substring(0, cut));
      para = para.substring(cut);
      para.trim();
      if (!para.length()) break;
    }
    if (nl < 0) break;
    start = nl + 1;
  }
}

static void drawPanel(int x, int y, int w, int h, uint16_t color) {
  canvas.fillRoundRect(x + 2, y + 3, w, h, 6, 0x0000);
  canvas.fillRoundRect(x, y, w, h, 6, col::PANEL2);
  canvas.drawRoundRect(x, y, w, h, 6, color);
}

void toast(const String &msg, uint16_t color, uint32_t ms) {
  if (!color) color = accent();
  canvas.setFont(&fonts::Font2);
  std::vector<String> lines;
  wrapText(msg, 196, lines);
  if (lines.size() > 5) lines.resize(5);
  int h = lines.size() * 16 + 16;
  int y = (SCR_H - h) / 2 + 6;
  drawPanel(14, y, SCR_W - 28, h, color);
  canvas.setTextDatum(top_center);
  canvas.setTextColor(col::TEXT);
  for (size_t i = 0; i < lines.size(); i++) canvas.drawString(lines[i], SCR_W / 2, y + 8 + i * 16);
  pushFrame();
  uint32_t t = millis();
  while (millis() - t < ms) {
    Key k = readKey();
    if (k.any && millis() - t > 150) break;
    delay(10);
  }
}

bool confirmBox(const String &q, uint16_t color) {
  canvas.setFont(&fonts::Font2);
  std::vector<String> lines;
  wrapText(q, 196, lines);
  if (lines.size() > 4) lines.resize(4);
  int h = lines.size() * 16 + 34;
  int y = (SCR_H - h) / 2 + 6;
  drawPanel(14, y, SCR_W - 28, h, color);
  canvas.setTextDatum(top_center);
  canvas.setTextColor(col::TEXT);
  for (size_t i = 0; i < lines.size(); i++) canvas.drawString(lines[i], SCR_W / 2, y + 7 + i * 16);
  canvas.setFont(&fonts::Font0);
  canvas.setTextColor(color);
  canvas.drawString("ENTER = yes    DEL = no", SCR_W / 2, y + h - 14);
  pushFrame();
  while (true) {
    Key k = readKey();
    if (k.enter || k.ch == 'y') return true;
    if (k.back || k.ch == 'n') return false;
    delay(10);
  }
}

// ------------------------------------------------------------------ list menu
int listMenu(const char *title, uint16_t color, std::vector<ListItem> &items, int &sel, uint8_t icon,
             std::function<void()> onIdle) {
  const int top = BAR_H + 19, rowH = 20;
  const int rows = (SCR_H - top) / rowH;
  int scroll = 0;
  bool dirty = true;
  uint32_t lastDraw = 0;
  if (sel >= (int)items.size()) sel = items.size() - 1;
  if (sel < 0) sel = 0;
  while (true) {
    Key k = readKey();
    int n = items.size();
    if (k.up && n) { sel = (sel - 1 + n) % n; dirty = true; beep(1800, 8); }
    if (k.down && n) { sel = (sel + 1) % n; dirty = true; beep(1800, 8); }
    if (k.left && n) { sel = max(0, sel - rows); dirty = true; }
    if (k.right && n) { sel = min(n - 1, sel + rows); dirty = true; }
    if (k.enter && n) { beep(2600, 20); return sel; }
    if (k.back) { beep(1200, 20); return -1; }
    if (k.ch >= '1' && k.ch <= '9' && (k.ch - '1') < n) { sel = k.ch - '1'; beep(2600, 20); return sel; }
    if (onIdle) onIdle();
    if (millis() - lastDraw > 1000) dirty = true;  // keep clock fresh
    if (!dirty) { delay(8); continue; }
    dirty = false;
    lastDraw = millis();
    if (sel < scroll) scroll = sel;
    if (sel >= scroll + rows) scroll = sel - rows + 1;
    drawHeader(title, color, icon);
    canvas.setFont(&fonts::Font0);
    canvas.setTextDatum(middle_right);
    canvas.setTextColor(col::DIM);
    char pos[12];
    snprintf(pos, sizeof pos, "%d/%d", n ? sel + 1 : 0, n);
    canvas.drawString(pos, SCR_W - 6, BAR_H + 9);
    if (!n) {
      canvas.setFont(&fonts::Font2);
      canvas.setTextDatum(middle_center);
      canvas.drawString("(empty)", SCR_W / 2, top + 40);
    }
    for (int r = 0; r < rows && scroll + r < n; r++) {
      int i = scroll + r;
      int y = top + r * rowH;
      ListItem &it = items[i];
      uint16_t ic = it.color ? it.color : color;
      bool on = i == sel;
      if (on) {
        canvas.fillRoundRect(3, y + 1, SCR_W - 12, rowH - 2, 5, ic);
      }
      int tx = 9;
      if (it.icon != 255) {
        drawIcon(canvas, it.icon, 17, y + rowH / 2, 13, on ? textOn(ic) : ic, on ? ic : col::BG);
        tx = 29;
      }
      canvas.setFont(&fonts::Font2);
      canvas.setTextDatum(middle_left);
      canvas.setTextColor(on ? textOn(ic) : col::TEXT);
      String label = it.label;
      int maxW = SCR_W - tx - 14 - (it.right.length() ? canvas.textWidth(it.right) + 6 : 0);
      while (label.length() > 1 && canvas.textWidth(label) > maxW) label.remove(label.length() - 1);
      canvas.drawString(label, tx, y + rowH / 2 + 1);
      if (it.right.length()) {
        canvas.setTextDatum(middle_right);
        canvas.setTextColor(on ? textOn(ic) : col::DIM);
        canvas.drawString(it.right, SCR_W - 14, y + rowH / 2 + 1);
      }
    }
    // scrollbar
    if (n > rows) {
      int trackH = SCR_H - top - 4;
      int barH = max(8, trackH * rows / n);
      int barY = top + 2 + (trackH - barH) * scroll / max(1, n - rows);
      canvas.fillRect(SCR_W - 5, top + 2, 2, trackH, col::FAINT);
      canvas.fillRect(SCR_W - 6, barY, 4, barH, color);
    }
    pushFrame();
  }
}

// ------------------------------------------------------------------ text input
bool textInput(const char *title, String &value, uint16_t color, bool secret, int maxLen) {
  String v = value;
  bool dirty = true;
  uint32_t blink = millis();
  bool caret = true;
  while (true) {
    Key k = readKey();
    if (k.any) {
      dirty = true;
      if (k.enter) { value = v; beep(2600, 20); return true; }
      if (k.esc) { beep(1200, 20); return false; }
      if (k.del) {
        if (v.length()) v.remove(v.length() - 1);
        else { beep(1200, 20); return false; }  // DEL on empty = cancel
      } else if (k.tab) {
        if ((int)v.length() < maxLen) v += ' ';
      } else if (k.ch && !k.fn && (int)v.length() < maxLen) {
        v += k.ch;
      }
    }
    if (millis() - blink > 450) { blink = millis(); caret = !caret; dirty = true; }
    if (!dirty) { delay(10); continue; }
    dirty = false;
    drawHeader(title, color);
    // input box
    canvas.fillRoundRect(6, 44, SCR_W - 12, 34, 6, col::PANEL);
    canvas.drawRoundRect(6, 44, SCR_W - 12, 34, 6, color);
    canvas.setFont(&fonts::FreeSans9pt7b);
    canvas.setTextDatum(middle_left);
    canvas.setTextColor(col::TEXT);
    String shown = v;
    if (secret && shown.length()) {
      String m;
      for (size_t i = 0; i + 1 < shown.length(); i++) m += '*';
      m += shown[shown.length() - 1];
      shown = m;
    }
    while (shown.length() && canvas.textWidth(shown) > SCR_W - 30) shown.remove(0, 1);
    canvas.drawString(shown, 14, 61);
    if (caret) canvas.fillRect(14 + canvas.textWidth(shown) + 1, 52, 2, 18, color);
    canvas.setFont(&fonts::Font0);
    canvas.setTextDatum(top_left);
    canvas.setTextColor(col::DIM);
    canvas.drawString("ENTER save   DEL erase   ` cancel", 8, 88);
    char cnt[16];
    snprintf(cnt, sizeof cnt, "%d/%d", (int)v.length(), maxLen);
    canvas.setTextDatum(top_right);
    canvas.drawString(cnt, SCR_W - 8, 100);
    canvas.setTextDatum(top_left);
    canvas.drawString("Shift for capitals & symbols", 8, 100);
    pushFrame();
  }
}

// ------------------------------------------------------------------ text viewer
void textViewer(const char *title, const String &text, uint16_t color) {
  canvas.setFont(&fonts::Font0);
  std::vector<String> lines;
  wrapText(text, SCR_W - 16, lines);
  const int top = BAR_H + 21, lh = 10;
  const int rows = (SCR_H - top) / lh;
  int scroll = 0;
  bool dirty = true;
  while (true) {
    Key k = readKey();
    int maxScroll = max(0, (int)lines.size() - rows);
    if (k.up) { scroll = max(0, scroll - 1); dirty = true; }
    if (k.down) { scroll = min(maxScroll, scroll + 1); dirty = true; }
    if (k.left) { scroll = max(0, scroll - rows); dirty = true; }
    if (k.right || k.space) { scroll = min(maxScroll, scroll + rows); dirty = true; }
    if (k.back || k.enter) return;
    if (!dirty) { delay(10); continue; }
    dirty = false;
    drawHeader(title, color);
    canvas.setFont(&fonts::Font0);
    canvas.setTextDatum(top_left);
    canvas.setTextColor(col::TEXT);
    for (int r = 0; r < rows && scroll + r < (int)lines.size(); r++)
      canvas.drawString(lines[scroll + r], 6, top + r * lh);
    if ((int)lines.size() > rows) {
      int trackH = SCR_H - top - 2;
      int barH = max(6, trackH * rows / (int)lines.size());
      int barY = top + (trackH - barH) * scroll / max(1, maxScroll);
      canvas.fillRect(SCR_W - 4, barY, 3, barH, color);
    }
    pushFrame();
  }
}

// ------------------------------------------------------------------ shared chat view
void drawChatView(const char *title, uint16_t color, uint8_t icon, const std::vector<std::pair<String, bool>> &chat, int scrollUp,
                  const String *input, const String &status, uint16_t statusColor) {
  drawHeader(title, color, icon);
  int bottom = input ? SCR_H - 22 : SCR_H - 2;
  canvas.setFont(&fonts::Font2);
  struct L { String s; bool me; };
  std::vector<L> lines;
  for (auto &c : chat) {
    std::vector<String> w;
    wrapText(c.first, SCR_W - 40, w);
    for (auto &s : w) lines.push_back({s, c.second});
    lines.push_back({"", c.second});
  }
  const int lh = 15;
  int y = bottom - lh;
  for (int i = (int)lines.size() - 1 - scrollUp; i >= 0 && y >= BAR_H + 18; i--, y -= lh) {
    if (!lines[i].s.length()) { y += lh / 2; continue; }
    canvas.setTextDatum(top_left);
    int w = canvas.textWidth(lines[i].s);
    if (lines[i].me) {
      canvas.fillRoundRect(SCR_W - w - 14, y, w + 10, lh, 4, color);
      canvas.setTextColor(textOn(color));  // auto-contrast: was col::BG, unreadable on some themes
      canvas.drawString(lines[i].s, SCR_W - w - 9, y);
    } else {
      canvas.fillRoundRect(4, y, w + 10, lh, 4, col::PANEL2);
      canvas.setTextColor(textOn(col::PANEL2));
      canvas.drawString(lines[i].s, 9, y);
    }
  }
  if (input) {
    canvas.fillRect(0, SCR_H - 20, SCR_W, 20, col::PANEL);
    canvas.drawFastHLine(0, SCR_H - 20, SCR_W, color);
    canvas.setFont(&fonts::Font2);
    canvas.setTextDatum(middle_left);
    canvas.setTextColor(input->length() ? col::TEXT : col::DIM);
    String shown = input->length() ? *input : String("type a message, ENTER sends");
    while (canvas.textWidth(shown) > SCR_W - 14) shown.remove(0, 1);
    canvas.drawString(shown, 5, SCR_H - 10);
    if (input->length() && (millis() / 450) % 2) canvas.fillRect(6 + canvas.textWidth(shown), SCR_H - 17, 2, 14, color);
  }
  if (status.length()) {
    canvas.setFont(&fonts::Font0);
    canvas.setTextDatum(middle_right);
    canvas.setTextColor(statusColor);
    canvas.drawString(fitText(status, 120), SCR_W - 6, BAR_H + 9);
  }
}
