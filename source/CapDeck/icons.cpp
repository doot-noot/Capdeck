// CapDeck - vector icons, drawn from primitives so they scale to any size.
// Every icon is designed on a 64x64 grid centred on (0,0).
#include "core.h"

namespace {
struct Pen {
  LovyanGFX &g;
  int cx, cy;
  float f;
  uint16_t c, bg;
  int X(float v) const { return cx + (int)lroundf(v * f); }
  int Y(float v) const { return cy + (int)lroundf(v * f); }
  int R(float v) const { return max(1, (int)lroundf(v * f)); }
  float W(float v) const { return max(0.6f, v * f); }  // half-width for wide lines
  void line(float x0, float y0, float x1, float y1, float w, uint16_t col) const {
    g.drawWideLine(X(x0), Y(y0), X(x1), Y(y1), W(w), col);
  }
  void line(float x0, float y0, float x1, float y1, float w = 2.5f) const { line(x0, y0, x1, y1, w, c); }
  void disc(float x, float y, float r, uint16_t col) const { g.fillCircle(X(x), Y(y), R(r), col); }
  void disc(float x, float y, float r) const { disc(x, y, r, c); }
  void ring(float x, float y, float r, float t, float a0 = 0, float a1 = 360) const {
    g.fillArc(X(x), Y(y), R(r), max(0, R(r) - R(t)), a0, a1, c);
  }
  void rrect(float x0, float y0, float x1, float y1, float r, uint16_t col) const {
    g.fillRoundRect(X(x0), Y(y0), max(1, X(x1) - X(x0)), max(1, Y(y1) - Y(y0)), R(r), col);
  }
  void rrect(float x0, float y0, float x1, float y1, float r = 4) const { rrect(x0, y0, x1, y1, r, c); }
  void tri(float x0, float y0, float x1, float y1, float x2, float y2, uint16_t col) const {
    g.fillTriangle(X(x0), Y(y0), X(x1), Y(y1), X(x2), Y(y2), col);
  }
  void tri(float x0, float y0, float x1, float y1, float x2, float y2) const { tri(x0, y0, x1, y1, x2, y2, c); }
};

// Smart home: house with a little connected-node graph inside
void iconHA(const Pen &p) {
  p.line(-28, 0, 0, -26, 3.5f);
  p.line(0, -26, 28, 0, 3.5f);
  p.line(17, -16, 17, -24, 3.0f);  // chimney
  p.rrect(-20, -4, 20, 28, 3);
  p.rrect(-15, 1, 15, 23, 2, p.bg);
  p.line(0, 6, -8, 17, 1.6f);
  p.line(0, 6, 9, 13, 1.6f);
  p.line(0, 6, 0, -4, 1.6f);
  p.disc(0, 6, 4.5f);
  p.disc(-8, 17, 3);
  p.disc(9, 13, 3);
}

// LoRa: antenna mast radiating waves
void iconLoRa(const Pen &p) {
  p.line(0, -8, 0, 26, 3.0f);
  p.line(0, 26, -12, 28, 2.5f);
  p.line(0, 26, 12, 28, 2.5f);
  p.line(0, 8, -9, 26, 2.0f);
  p.line(0, 8, 9, 26, 2.0f);
  p.disc(0, -12, 5);
  for (int i = 0; i < 3; i++) {
    float r = 13 + i * 8;
    p.ring(0, -12, r, 3.2f, 315, 360);
    p.ring(0, -12, r, 3.2f, 0, 45);
    p.ring(0, -12, r, 3.2f, 135, 225);
  }
}

// GPS: map pin over an orbit ellipse
void iconGPS(const Pen &p) {
  p.g.drawEllipse(p.X(0), p.Y(24), p.R(26), p.R(7), p.c);
  p.g.drawEllipse(p.X(0), p.Y(24), p.R(25), p.R(6), p.c);
  p.disc(0, -10, 18);
  p.tri(-15.5f, -1, 15.5f, -1, 0, 26);
  p.disc(0, -10, 7, p.bg);
  p.disc(22, -24, 3.5f);  // satellite dot
  p.line(15, -28, 29, -20, 1.4f);
}

// IR: remote control firing a beam
void iconIR(const Pen &p) {
  p.rrect(-12, -4, 12, 30, 5);
  p.rrect(-8, 0, 8, 26, 3, p.bg);
  p.disc(0, 6, 3.5f);
  for (int r = 0; r < 2; r++)
    for (int c = 0; c < 3; c++) p.disc(-5 + c * 5, 14 + r * 6, 1.6f);
  p.rrect(-3, -9, 3, -4, 1);  // LED
  for (int i = 0; i < 3; i++) p.ring(0, -6, 10 + i * 8, 3.0f, 240, 300);
}

// RF 433: key fob + wave + "433"
void iconRF(const Pen &p) {
  // sine wave
  float px = -28, py = -14;
  for (int i = 1; i <= 28; i++) {
    float x = -28 + i * 2;
    float y = -14 + 9 * sinf(i * 0.45f);
    p.line(px, py, x, y, 2.0f);
    px = x;
    py = y;
  }
  if (p.f >= 0.45f) {
    p.g.setFont(&fonts::Font0);
    p.g.setTextSize(max(1.0f, p.f * 2.4f));
    p.g.setTextDatum(middle_center);
    p.g.setTextColor(p.c);
    p.g.drawString("433", p.X(0), p.Y(14));
    p.g.setTextSize(1);
  } else {
    p.rrect(-10, 6, 10, 26, 4);
  }
}

// Files: folder with documents peeking out
void iconFiles(const Pen &p) {
  p.rrect(-28, -22, -2, -8, 4);   // tab
  p.rrect(-28, -15, 28, 22, 5);   // body
  p.line(-28, -8, 28, -8, 1.6f, p.bg);  // lip
  p.rrect(-10, 3, 10, 7, 2, p.bg);      // label slot
}

// Settings: gear
void iconGear(const Pen &p) {
  for (int i = 0; i < 8; i++) {
    float a = i * PI / 4;
    float ca = cosf(a), sa = sinf(a);
    p.line(ca * 14, sa * 14, ca * 27, sa * 27, 5.5f);
  }
  p.disc(0, 0, 21);
  p.disc(0, 0, 8, p.bg);
}

void iconClock(const Pen &p) {
  p.ring(0, 0, 27, 4.5f);
  for (int i = 0; i < 12; i++) {
    float a = i * PI / 6;
    p.line(cosf(a) * 18, sinf(a) * 18, cosf(a) * 20, sinf(a) * 20, 1.2f);
  }
  p.line(0, 0, 0, -15, 2.5f);
  p.line(0, 0, 10, 6, 2.5f);
  p.disc(0, 0, 3);
}

void iconBattery(const Pen &p) {
  p.rrect(-26, -14, 22, 14, 4);
  p.rrect(-22, -10, 18, 10, 2, p.bg);
  p.rrect(22, -6, 28, 6, 2);
  p.rrect(-19, -7, 4, 7, 1);
  p.tri(2, -12, -6, 2, 0, 2);  // bolt
  p.tri(0, -2, 6, -2, -2, 12);
}

void iconWifi(const Pen &p) {
  p.disc(0, 20, 5);
  for (int i = 0; i < 3; i++) p.ring(0, 20, 16 + i * 10, 4.5f, 225, 315);
}

void iconDisplay(const Pen &p) {
  p.disc(0, 0, 11);
  for (int i = 0; i < 8; i++) {
    float a = i * PI / 4;
    p.line(cosf(a) * 17, sinf(a) * 17, cosf(a) * 26, sinf(a) * 26, 2.8f);
  }
}

void iconSound(const Pen &p) {
  p.rrect(-24, -8, -12, 8, 1);
  p.tri(-14, -8, 2, -22, 2, 0);
  p.tri(-14, 8, 2, 22, 2, 0);
  p.tri(-14, -8, -14, 8, 2, 0);
  p.ring(4, 0, 12, 3.5f, 300, 360);
  p.ring(4, 0, 12, 3.5f, 0, 60);
  p.ring(4, 0, 22, 3.5f, 300, 360);
  p.ring(4, 0, 22, 3.5f, 0, 60);
}

void iconInfo(const Pen &p) {
  p.ring(0, 0, 27, 4.0f);
  p.disc(0, -12, 4);
  p.line(0, -2, 0, 16, 3.5f);
}

void iconPower(const Pen &p) {
  p.ring(0, 2, 24, 5.0f, 300, 360);
  p.ring(0, 2, 24, 5.0f, 0, 240);
  p.line(0, -26, 0, 0, 4.0f);
}
// Local AI: chip with pins and a spark
void iconAI(const Pen &p) {
  for (int i = 0; i < 4; i++) {
    float o = -12 + i * 8;
    p.line(o, -26, o, -18, 2.2f);
    p.line(o, 18, o, 26, 2.2f);
    p.line(-26, o, -18, o, 2.2f);
    p.line(18, o, 26, o, 2.2f);
  }
  p.rrect(-19, -19, 19, 19, 5);
  p.rrect(-14, -14, 14, 14, 3, p.bg);
  // four-point spark
  p.tri(0, -11, -3, 0, 3, 0);
  p.tri(0, 11, -3, 0, 3, 0);
  p.tri(-11, 0, 0, -3, 0, 3);
  p.tri(11, 0, 0, -3, 0, 3);
}

// Mesh: connected nodes
void iconMesh(const Pen &p) {
  const float pts[5][2] = {{0, -22}, {-22, -4}, {22, -6}, {-12, 22}, {14, 20}};
  const int links[8][2] = {{0, 1}, {0, 2}, {1, 3}, {2, 4}, {3, 4}, {1, 2}, {0, 4}, {1, 4}};
  for (auto &l : links) p.line(pts[l[0]][0], pts[l[0]][1], pts[l[1]][0], pts[l[1]][1], 1.6f);
  for (int i = 0; i < 5; i++) {
    p.disc(pts[i][0], pts[i][1], i == 0 ? 7 : 5.5f);
    p.disc(pts[i][0], pts[i][1], i == 0 ? 3.5f : 2.5f, p.bg);
  }
}

// Portal: phone with a hotspot above it
void iconPortal(const Pen &p) {
  p.rrect(-13, -6, 13, 30, 4);
  p.rrect(-10, -2, 10, 22, 2, p.bg);
  p.disc(0, 26, 1.8f, p.bg);
  for (int i = 0; i < 3; i++) p.ring(0, -6, 10 + i * 7, 3.0f, 225, 315);
  p.disc(0, -10, 2.5f);
}

// Scanner: spectrum bars under a sweep line
void iconScan(const Pen &p) {
  const float h[] = {8, 16, 28, 14, 22, 10, 18};
  for (int i = 0; i < 7; i++) p.rrect(-27 + i * 8, 24 - h[i], -22 + i * 8, 24, 1);
  p.line(-28, 28, 28, 28, 1.6f);
  p.ring(0, 28, 34, 2.5f, 215, 325);
}

// Macro: lightning bolt (one tap, many actions)
void iconMacro(const Pen &p) {
  // bold zig-zag bolt
  const float bolt[7][2] = {{6, -26}, {-12, 4}, {-1, 4}, {-6, 26}, {14, -6}, {2, -6}, {8, -26}};
  for (int i = 0; i < 6; i++) p.line(bolt[i][0], bolt[i][1], bolt[i + 1][0], bolt[i + 1][1], 3.0f);
  // fill the body with two triangles so it reads as solid at small sizes
  p.tri(6, -26, -12, 4, 2, -6);
  p.tri(-6, 26, 14, -6, -1, 4);
}

// Map: folded paper map with a pin
void iconMap(const Pen &p) {
  p.tri(-28, -16, -10, -22, -10, 22);
  p.tri(-28, -16, -28, 28, -10, 22);
  p.tri(-8, -22, 10, -16, 10, 28);
  p.tri(-8, -22, -8, 22, 10, 28);
  p.tri(12, -16, 28, -22, 28, 22);
  p.tri(12, -16, 12, 28, 28, 22);
  p.disc(1, -6, 9, p.bg);
  p.disc(1, -6, 6.5f);
  p.tri(-5, -3, 7, -3, 1, 8);
  p.disc(1, -6, 2.5f, p.bg);
}

// Mycorrhiza: a cap above ground, the hyphae trading with the world below (downloads & lookups)
void iconMyco(const Pen &p) {
  p.ring(0, -5, 21, 21, 180, 360);  // cap
  p.rrect(-23, -7, 23, -3, 2);      // rim
  p.disc(-9, -13, 2.6f, p.bg);
  p.disc(5, -18, 2.2f, p.bg);
  p.disc(13, -10, 2.0f, p.bg);
  p.rrect(-4, -3, 4, 10, 2);        // stem
  p.line(-29, 10, 29, 10, 1.3f);    // the ground
  p.line(0, 10, -17, 24, 1.7f);
  p.line(-17, 24, -27, 21, 1.2f);
  p.line(-11, 19, -10, 29, 1.2f);
  p.line(0, 10, 16, 25, 1.7f);
  p.line(16, 25, 27, 23, 1.2f);
  p.line(9, 18, 6, 29, 1.2f);
  p.disc(-27, 21, 2.2f);
  p.disc(27, 23, 2.2f);
  p.disc(-10, 29, 1.7f);
  p.disc(6, 29, 1.7f);
}

// Forage: a basket with a mushroom peeking out
void iconForage(const Pen &p) {
  p.ring(0, 4, 23, 3.4f, 180, 360);  // handle
  p.ring(-8, 2, 12, 12, 180, 360);   // the find
  p.rrect(-11, 1, -5, 7, 1);
  p.disc(-12, -4, 1.9f, p.bg);
  p.disc(-4, -6, 1.6f, p.bg);
  p.tri(-27, 4, 27, 4, 19, 29);      // basket
  p.tri(-27, 4, 19, 29, -19, 29);
  p.line(-23, 12, 23, 12, 1.3f, p.bg);
  p.line(-21, 20, 21, 20, 1.3f, p.bg);
  p.line(-6, 5, -4, 28, 1.1f, p.bg);
  p.line(7, 5, 6, 28, 1.1f, p.bg);
}

// Rhizomorph: root-like cords carrying a pulse between two nodes
void iconRhizo(const Pen &p) {
  p.disc(-22, -16, 6);                 // node A
  p.disc(22, 18, 6);                   // node B
  p.line(-18, -12, -6, -2, 2.6f);      // the main cord, braided
  p.line(-6, -2, 6, 2, 2.6f);
  p.line(6, 2, 18, 14, 2.6f);
  p.line(-16, -18, 0, -10, 1.4f);      // side strands
  p.line(0, -10, 16, 10, 1.4f);
  p.line(-20, -8, -4, 6, 1.4f);
  p.line(-4, 6, 14, 20, 1.4f);
  p.line(-4, 6, -14, 22, 1.2f);        // a root reaching off
  p.line(6, 2, 24, -8, 1.2f);
  p.disc(0, 0, 3.4f, p.bg);            // the pulse travelling along
  p.disc(0, 0, 2.2f);
}

// Nightcap: a crescent moon in a sleeping cap, one eye open on the sky
void iconNightcap(const Pen &p) {
  p.disc(-2, 4, 24);
  p.disc(9, -3, 20, p.bg);           // carve the crescent
  p.tri(-21, -10, -2, -21, 18, -30); // the cap
  p.disc(19, -30, 4);                // pompom
  p.disc(-14, 6, 5.2f, p.bg);        // the open eye
  p.disc(-13, 6, 2.6f);
  p.line(-20, 0, -8, 1, 1.0f);       // lid
  p.line(22, 6, 22, 16, 1.2f);       // a star
  p.line(17, 11, 27, 11, 1.2f);
  p.disc(12, 24, 1.6f);              // and a smaller one
}
}  // namespace

void drawIcon(LovyanGFX &g, uint8_t icon, int cx, int cy, int size, uint16_t color, uint16_t bg) {
  Pen p{g, cx, cy, size / 64.0f, color, bg};
  switch (icon) {
    case ICON_HA: iconHA(p); break;
    case ICON_LORA: iconLoRa(p); break;
    case ICON_GPS: iconGPS(p); break;
    case ICON_IR: iconIR(p); break;
    case ICON_RF: iconRF(p); break;
    case ICON_FILES: iconFiles(p); break;
    case ICON_CONF: iconGear(p); break;
    case ICON_CLOCK: iconClock(p); break;
    case ICON_BATTERY: iconBattery(p); break;
    case ICON_WIFI: iconWifi(p); break;
    case ICON_DISPLAY: iconDisplay(p); break;
    case ICON_SOUND: iconSound(p); break;
    case ICON_INFO: iconInfo(p); break;
    case ICON_POWER: iconPower(p); break;
    case ICON_AI: iconAI(p); break;
    case ICON_MESH: iconMesh(p); break;
    case ICON_PORTAL: iconPortal(p); break;
    case ICON_SCAN: iconScan(p); break;
    case ICON_MAP: iconMap(p); break;
    case ICON_MACRO: iconMacro(p); break;
    case ICON_MYCO: iconMyco(p); break;
    case ICON_FORAGE: iconForage(p); break;
    case ICON_RHIZO: iconRhizo(p); break;
    case ICON_NIGHTCAP: iconNightcap(p); break;
  }
}
