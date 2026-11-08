// CapDeck - live wallpapers. Every theme is alive; "Still life" (or a low battery) freezes them all.
// Budget: each frame must stay cheap (a few ms) - the carousel redraws ~16x a second.
#include "core.h"

static inline uint32_t hash32(uint32_t x) {  // deterministic noise, so wallpapers need no per-frame state
  x ^= x >> 16; x *= 0x7feb352d; x ^= x >> 15; x *= 0x846ca68b; x ^= x >> 16;
  return x;
}
static inline float frand(uint32_t seed) { return (hash32(seed) & 0xFFFF) / 65535.0f; }

// ================================================================== Spore: a mycelium network that grows,
// with nutrient pulses travelling down the hyphae and spores drifting up.
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
  uint16_t thread = blend565(col::DIM, col::BG, 0.55f);
  uint16_t fresh = blend565(col::TEXT, col::BG, 0.55f);
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
    uint16_t glow = blend565(accent(), col::BG, 0.35f);
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
  uint16_t sporeC = blend565(col::TEXT, col::BG, 0.5f);
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

// ================================================================== Matrix: falling glyph rain
static void drawMatrix(uint32_t t) {
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
}

// ================================================================== Ocean: rolling waves, glints on the water
static void drawOcean(uint32_t t) {
  const float ts = t / 1000.0f;
  // a pale moon low in the sky, and its broken reflection
  uint16_t moon = blend565(col::TEXT, col::BG, 0.5f);
  canvas.fillCircle(196, 34, 7, moon);
  canvas.fillCircle(199, 32, 6, col::BG);
  struct Layer { float base, amp, k, w, mix; } L[] = {{74, 2.0f, 0.11f, 0.7f, 0.16f}, {90, 3.5f, 0.075f, 1.0f, 0.27f}, {108, 5.0f, 0.052f, 1.4f, 0.40f}};
  for (int li = 0; li < 3; li++) {
    const Layer &l = L[li];
    uint16_t body = blend565(accent(), col::BG, l.mix);
    uint16_t foam = blend565(col::TEXT, body, 0.35f);
    for (int x = 0; x < SCR_W; x += 2) {
      float y = l.base + l.amp * sinf(x * l.k + ts * l.w + li) + l.amp * 0.45f * sinf(x * l.k * 2.3f - ts * l.w * 1.3f);
      int yi = (int)y;
      canvas.fillRect(x, yi, 2, SCR_H - yi, body);
      canvas.fillRect(x, yi, 2, 1, foam);
    }
  }
  // moon glints dancing on the water
  for (int i = 0; i < 9; i++) {
    float on = sinf(ts * 3.1f + i * 1.7f);
    if (on < 0.55f) continue;
    int x = 186 + (int)(frand(i + 11) * 22) + (int)(3 * sinf(ts + i));
    int y = 80 + i * 5;
    canvas.drawFastHLine(x, y, 2 + (i % 3), moon);
  }
}

// ================================================================== Lava: a molten floor with drifting crust,
// bubbles that pop, and embers that cool as they rise.
static void drawLava(uint32_t t) {
  const float ts = t / 1000.0f;
  const int base = 112;
  // heat haze above the surface
  for (int i = 0; i < 10; i++) canvas.drawFastHLine(0, base - 10 + i, SCR_W, blend565(0x8000, col::BG, i * 0.02f));
  for (int x = 0; x < SCR_W; x += 2) {
    int top = base + (int)(2.5f * sinf(x * 0.06f + ts * 0.9f) + 1.5f * sinf(x * 0.15f - ts * 1.7f));
    canvas.fillRect(x, top, 2, 2, 0xFE60);                 // glowing lip
    canvas.fillRect(x, top + 2, 2, 4, 0xFC40);              // orange
    canvas.fillRect(x, top + 6, 2, SCR_H - top - 6, 0xB0A0);  // deep red
    // cooling crust plates sliding across the melt; the cracks between them glow
    float c = sinf(x * 0.11f + ts * 0.35f) * sinf(x * 0.047f - ts * 0.22f);
    if (c > 0.22f) canvas.fillRect(x, top + 4, 2, SCR_H - top - 4, c > 0.5f ? 0x3000 : 0x5800);
  }
  // bubbles: swell, then pop into a ring
  for (int i = 0; i < 3; i++) {
    uint32_t period = 2600 + i * 700;
    float a = ((t + i * 911) % period) / (float)period;
    int bx = 40 + (int)(frand((t + i * 911) / period * 3 + i) * 160);
    if (a < 0.8f) canvas.fillCircle(bx, base + 1, 1 + (int)(a * 4), 0xFDA0);
    else canvas.drawCircle(bx, base - 1, 3 + (int)((a - 0.8f) * 30), blend565(0xFD20, col::BG, 1.0f - (a - 0.8f) * 5));
  }
  // embers: born yellow-hot, cool through orange and red, then fade out
  for (int i = 0; i < 26; i++) {
    uint32_t period = 2800 + (hash32(i) % 2400);
    float a = ((t + hash32(i * 3) % period) % period) / (float)period;
    int x0 = hash32(i * 5) % SCR_W;
    int x = x0 + (int)(7 * sinf(a * 5.5f + i)) + (int)(a * 10);
    int y = base - (int)(a * (base - BAR_H - 4));
    uint16_t c = a < 0.25f ? 0xFFE0 : a < 0.5f ? 0xFD20 : a < 0.75f ? 0xF8A0 : blend565(0x9000, col::BG, 1.0f - (a - 0.75f) * 4);
    canvas.drawPixel(x, y, c);
    if (a < 0.3f) canvas.drawPixel(x, y + 1, c);
  }
}

// ================================================================== Amber: an old terminal, still running
static void drawAmber(uint32_t t) {
  static const char *LOG[] = {
      "> mount /sd ............. ok",   "> config.json parsed",          "> sx1262 tcxo 1.8V  rx ok",
      "> mycelium: listening",          "> spores in the air: 0x2A",     "> heap 214k free",
      "> 0x3FC9  8A 2F 11 C0 FE ED",    "> deathcaps // cap cracked",    "> gnss: 9 sats  fix 3D",
      "> scan 902.3 -> 927.9 MHz",      "> nothing to see here",         "> portal: idle",
      "> ir db: 9k remotes",            "> do not touch.json",           "> reticulating spores",
      "> uptime ok   mood: fungal",
  };
  const int n = sizeof(LOG) / sizeof(LOG[0]);
  for (int y = BAR_H; y < SCR_H; y += 3) canvas.drawFastHLine(0, y, SCR_W, blend565(col::BG, 0x0000, 0.6f));
  canvas.setFont(&fonts::Font0);
  canvas.setTextDatum(top_left);
  canvas.setTextColor(blend565(col::TEXT, col::BG, 0.22f));
  uint32_t step = t / 1300;
  int rows = 12;
  for (int r = 0; r < rows; r++) {
    int idx = (step + r) % n;
    canvas.drawString(LOG[idx], 4, BAR_H + 3 + r * 10);
  }
  if ((t / 500) % 2) {  // the cursor at the bottom line
    int w = canvas.textWidth(LOG[(step + rows - 1) % n]);
    canvas.fillRect(6 + w, BAR_H + 3 + (rows - 1) * 10, 5, 8, blend565(col::TEXT, col::BG, 0.4f));
  }
  // the slow roll bar every old monitor had
  int ry = BAR_H + (t / 40) % (SCR_H - BAR_H);
  for (int i = 0; i < 5; i++) canvas.drawFastHLine(0, ry + i, SCR_W, blend565(col::TEXT, col::BG, 0.04f));
}

// ================================================================== Storm: clouds, rain, and the bolt
// (the same bolt that cracks the cap in the logo). Lit gently - no full-screen flashes.
static void drawStorm(uint32_t t) {
  const uint32_t cycle = 8200;
  uint32_t n = t / cycle, in = t % cycle;
  bool strike = in < 260 && !(in > 70 && in < 120);  // flicker: on, off, on
  uint16_t cloud = blend565(col::PANEL2, col::BG, strike ? 1.0f : 0.75f);
  if (strike) cloud = blend565(col::TEXT, cloud, 0.18f);
  // rain
  uint16_t rain = blend565(col::DIM, col::BG, 0.45f);
  for (int i = 0; i < 46; i++) {
    int x = (int)(hash32(i) % 280 + t * 0.06f) % 280 - 20;
    int y = BAR_H + (int)(hash32(i * 7) % 120 + t * 0.22f) % (SCR_H - BAR_H);
    canvas.drawLine(x, y, x - 2, y + 5, rain);
  }
  // the bolt
  if (strike) {
    uint32_t s = hash32(n + 77);
    int x = 50 + s % 140, y = 30;
    uint16_t bolt = accent();
    for (int seg = 0; seg < 8 && y < 118; seg++) {
      int nx = x + (int)(hash32(s + seg) % 23) - 11, ny = y + 10 + hash32(s * 3 + seg) % 6;
      canvas.drawLine(x, y, nx, ny, bolt);
      canvas.drawLine(x + 1, y, nx + 1, ny, bolt);
      if (seg == 3) {  // a branch
        int bx = nx + 14 - (int)(hash32(s + 99) % 28);
        canvas.drawLine(nx, ny, bx, ny + 14, blend565(bolt, col::BG, 0.6f));
      }
      x = nx; y = ny;
    }
  }
  // clouds drifting along the top
  for (int i = 0; i < 7; i++) {
    int cx = (int)(hash32(i * 13) % 300 + t * (0.004f + i * 0.0015f)) % 300 - 30;
    int cy = 24 + hash32(i * 17) % 12;
    canvas.fillCircle(cx, cy, 11, cloud);
    canvas.fillCircle(cx + 12, cy + 3, 9, cloud);
    canvas.fillCircle(cx - 12, cy + 4, 8, cloud);
  }
}

// ================================================================== Cyber: the neon horizon
static void drawCyber(uint32_t t) {
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
}

// ================================================================== Aurora: curtains of light over a treeline
static void drawAurora(uint32_t t) {
  const float ts = t / 1000.0f;
  for (int i = 0; i < 34; i++) {  // stars
    int x = hash32(i) % SCR_W, y = BAR_H + hash32(i * 3) % 70;
    if (sinf(ts * 1.3f + i) > -0.4f) canvas.drawPixel(x, y, blend565(col::TEXT, col::BG, 0.45f));
  }
  const uint16_t hue[2] = {col::MESH, accent()};
  for (int c = 0; c < 2; c++) {
    for (int x = 0; x < SCR_W; x += 2) {
      float top = 28 + c * 10 + 12 * sinf(x * 0.022f + ts * 0.42f + c * 2) + 7 * sinf(x * 0.061f - ts * 0.6f + c);
      float h = 22 + 9 * sinf(x * 0.04f + ts * 0.5f + c * 1.3f);
      float k = 0.5f + 0.5f * sinf(x * 0.07f + ts * 0.9f + c * 3);
      int y0 = (int)top, y1 = (int)(top + h);
      int third = max(1, (y1 - y0) / 3);
      canvas.fillRect(x, y0, 2, third, blend565(hue[c], col::BG, 0.07f + 0.10f * k));
      canvas.fillRect(x, y0 + third, 2, third, blend565(hue[c], col::BG, 0.12f + 0.18f * k));
      canvas.fillRect(x, y0 + 2 * third, 2, y1 - y0 - 2 * third, blend565(hue[c], col::BG, 0.2f + 0.3f * k));
    }
  }
  // hills and pines
  uint16_t land = blend565(0x0000, col::BG, 0.6f);
  for (int x = 0; x < SCR_W; x += 2) {
    int y = 118 + (int)(4 * sinf(x * 0.05f) + 3 * sinf(x * 0.13f + 1));
    canvas.fillRect(x, y, 2, SCR_H - y, land);
  }
  for (int i = 0; i < 26; i++) {
    int x = i * 9 + hash32(i) % 5, hgt = 6 + hash32(i * 9) % 9;
    int y = 118 + (int)(4 * sinf(x * 0.05f) + 3 * sinf(x * 0.13f + 1));
    canvas.fillTriangle(x, y - hgt, x - 3, y + 1, x + 3, y + 1, land);
  }
}

// ================================================================== Abyss: the deep, where things glow
static void drawAbyss(uint32_t t) {
  const float ts = t / 1000.0f;
  for (int i = 0; i < 4; i++)  // it gets darker the deeper you go
    canvas.fillRect(0, SCR_H - 30 + i * 8, SCR_W, 8, blend565(0x0000, col::BG, 0.25f + i * 0.2f));
  uint16_t snow = blend565(col::DIM, col::BG, 0.35f);
  for (int i = 0; i < 30; i++) {  // marine snow, sinking
    int x = (int)(hash32(i) % SCR_W + 3 * sinf(ts * 0.7f + i)) % SCR_W;
    int y = BAR_H + (int)(hash32(i * 5) % 120 + t * 0.006f) % (SCR_H - BAR_H);
    canvas.drawPixel(x, y, snow);
  }
  for (int i = 0; i < 22; i++) {  // bioluminescence: slow wanderers that pulse
    float x = frand(i * 3) * SCR_W + 10 * sinf(ts * 0.21f + i);
    float y = BAR_H + 6 + frand(i * 3 + 1) * 110 + 6 * sinf(ts * 0.17f + i * 2);
    float b = 0.5f + 0.5f * sinf(ts * (0.9f + frand(i) * 0.8f) + i * 1.9f);
    if (b > 0.75f) canvas.fillCircle(x, y, 2, blend565(accent(), col::BG, 0.25f * b));
    canvas.drawPixel(x, y, blend565(accent(), col::BG, 0.2f + 0.8f * b));
  }
  // a jellyfish, drifting up through the dark
  float a = (t % 24000) / 24000.0f;
  int jx = 40 + (int)(frand(t / 24000) * 160) + (int)(8 * sinf(a * 9));
  int jy = SCR_H + 20 - (int)(a * (SCR_H + 40));
  float pulse = 0.5f + 0.5f * sinf(ts * 3.2f);
  int bw = 7 + (int)(pulse * 2);
  uint16_t jc = blend565(accent(), col::BG, 0.55f);
  canvas.fillArc(jx, jy, 0, bw, 180, 360, jc);
  for (int k = -2; k <= 2; k++)
    for (int s = 0; s < 4; s++) {
      int y = jy + 1 + s * 3;
      canvas.drawPixel(jx + k * 2 + (int)(1.5f * sinf(ts * 4 + s + k)), y, blend565(jc, col::BG, 1.0f - s * 0.2f));
    }
}

// ================================================================== Void: a slow drift through the stars
static void drawVoid(uint32_t t) {
  struct Star { float x, y, z; };
  static Star st[70];
  static bool init = false;
  static uint32_t last = 0;
  if (!init) {
    for (int i = 0; i < 70; i++) st[i] = {frand(i * 2) * 2 - 1, frand(i * 2 + 1) * 2 - 1, 0.1f + frand(i * 9) * 0.9f};
    init = true;
  }
  float dt = last ? min((uint32_t)120, t - last) / 1000.0f : 0;
  last = t;
  const int cx = SCR_W / 2, cy = 62;
  for (int i = 0; i < 70; i++) {
    Star &s = st[i];
    s.z -= dt * 0.11f;
    if (s.z <= 0.05f) s = {frand(t + i) * 2 - 1, frand(t + i * 3) * 2 - 1, 1.0f};
    int x = cx + (int)(s.x / s.z * 52), y = cy + (int)(s.y / s.z * 38);
    if (x < 0 || x >= SCR_W || y < BAR_H || y >= SCR_H) { s.z = 0; continue; }
    uint16_t c = blend565(col::TEXT, col::BG, min(1.0f, (1.05f - s.z) * 1.1f));
    canvas.drawPixel(x, y, c);
    if (s.z < 0.3f) canvas.drawPixel(x + 1, y, c);
  }
  // now and then, a shooting star
  uint32_t in = t % 13000;
  if (in < 600) {
    float a = in / 600.0f;
    int sx = 30 + (int)(a * 120), sy = 26 + (int)(a * 30);
    canvas.drawLine(sx, sy, sx - 14, sy - 4, blend565(col::TEXT, col::BG, 1.0f - a));
  }
}

void drawBackdrop(uint32_t t) {
  switch (themeStyle()) {
    case STYLE_SPORE: spore::draw(t); break;
    case STYLE_MATRIX: drawMatrix(t); break;
    case STYLE_OCEAN: drawOcean(t); break;
    case STYLE_EMBER: drawLava(t); break;
    case STYLE_CRT: drawAmber(t); break;
    case STYLE_STORM: drawStorm(t); break;
    case STYLE_SYNTHWAVE: drawCyber(t); break;
    case STYLE_AURORA: drawAurora(t); break;
    case STYLE_ABYSS: drawAbyss(t); break;
    case STYLE_VOID: drawVoid(t); break;
    default: break;
  }
}

// ================================================================== theme picker
// Full-screen live preview: the list sits on the left, the highlighted theme's wallpaper runs behind it.
// The bar at the bottom freezes / revives every wallpaper in place. Back always leaves the picker.
bool themePicker() {
  const uint8_t orig = cfg.theme;
  const int n = THEME_COUNT;
  int sel = cfg.theme % n;  // n == the Still life bar
  int scroll = 0;
  const int rows = 7, rowH = 14, top = BAR_H + 4;
  uint32_t lastDraw = 0;
  bool dirty = true;
  while (true) {
    Key k = readKey();
    if (k.up) { sel = (sel - 1 + n + 1) % (n + 1); dirty = true; beep(1800, 8); }
    if (k.down || k.tab) { sel = (sel + 1) % (n + 1); dirty = true; beep(1800, 8); }
    if (k.back) {
      cfg.theme = orig;
      applyTheme(orig);
      ledApply();
      beep(1200, 20);
      return false;
    }
    if (k.ch == 's' || (k.enter && sel == n)) {  // freeze / revive, right here
      cfg.stillLife = !cfg.stillLife;
      saveSettings();
      beep(cfg.stillLife ? 1500 : 2600, 30);
      dirty = true;
    } else if (k.enter) {
      cfg.theme = sel;
      applyTheme(sel);
      saveSettings();
      ledApply();
      beep(2600, 20);
      return true;
    }
    if (sel < n && sel != cfg.theme) {  // preview the highlighted theme live
      cfg.theme = sel;
      applyTheme(sel);
      ledApply();
    }
    bool animated = themeStyle() != STYLE_PLAIN;
    if (!dirty && millis() - lastDraw < (animated ? 50u : 500u)) { delay(8); continue; }
    dirty = false;
    lastDraw = millis();
    if (sel < n) {
      if (sel < scroll) scroll = sel;
      if (sel >= scroll + rows) scroll = sel - rows + 1;
    }
    canvas.fillScreen(col::BG);
    drawBackdrop(millis());
    drawStatusBar("THEMES", accent());
    // list panel
    canvas.fillRoundRect(4, top - 2, 118, rows * rowH + 4, 5, blend565(col::PANEL, col::BG, 0.92f));
    canvas.drawRoundRect(4, top - 2, 118, rows * rowH + 4, 5, blend565(accent(), col::BG, 0.5f));
    canvas.setFont(&fonts::Font2);
    canvas.setTextDatum(middle_left);
    for (int r = 0; r < rows && scroll + r < n; r++) {
      int i = scroll + r, y = top + r * rowH;
      bool on = i == sel;
      if (on) canvas.fillRoundRect(6, y, 114, rowH, 4, accent());
      canvas.fillCircle(14, y + rowH / 2, 4, THEMES[i].accent);
      canvas.drawCircle(14, y + rowH / 2, 4, on ? textOn(accent()) : col::FAINT);
      canvas.setTextColor(on ? textOn(accent()) : col::TEXT);
      canvas.drawString(themeName(i), 23, y + rowH / 2 + 1);
    }
    if (n > rows) {  // scroll hint
      int barH = max(6, rows * rowH * rows / n), barY = top + (rows * rowH - barH) * scroll / max(1, n - rows);
      canvas.fillRect(119, barY, 2, barH, col::DIM);
    }
    // the Still life bar
    bool onBar = sel == n;
    int by = SCR_H - 15;
    canvas.fillRoundRect(4, by, SCR_W - 8, 14, 4, onBar ? accent() : blend565(col::PANEL, col::BG, 0.92f));
    canvas.setFont(&fonts::Font0);
    canvas.setTextDatum(middle_center);
    canvas.setTextColor(onBar ? textOn(accent()) : col::TEXT);
    canvas.drawString(cfg.stillLife ? "S  Bring them back to life" : "S  Still life (freeze every wallpaper)", SCR_W / 2, by + 7);
    // hint, bottom right above the bar
    canvas.setTextDatum(bottom_right);
    canvas.setTextColor(col::DIM);
    canvas.drawString("ENTER pick  DEL back", SCR_W - 6, by - 3);
    pushFrame();
  }
}
