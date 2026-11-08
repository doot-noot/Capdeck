// CapDeck - on-board RGB LED. Pure eye-candy, and a handy "you got mail" blink.
// Deathcaps brand colour is toxic green; that's the stock look.
#include "core.h"

const char *LED_MODE_NAMES[] = {"Off", "Brand green", "Theme colour", "Breathe", "Rainbow", "Alerts only"};

// brand green (Amanita / Deathcaps)
static const uint8_t kR = 20, kG = 230, kB = 40;  // brand green

static uint32_t flashUntil = 0;
static uint8_t fr, fg, fb;
static uint32_t lastWrite = 0;

static void raw(uint8_t r, uint8_t g, uint8_t b) {
  float s = cfg.ledBright / 255.0f;
  rgbLedWrite(PIN_LED, (uint8_t)(r * s), (uint8_t)(g * s), (uint8_t)(b * s));
}

// pull the theme accent (RGB565) into 8-bit RGB
static void accentRGB(uint8_t &r, uint8_t &g, uint8_t &b) {
  uint16_t a = accent();
  r = ((a >> 11) & 0x1F) << 3;
  g = ((a >> 5) & 0x3F) << 2;
  b = (a & 0x1F) << 3;
}

static void hsv(uint16_t h, uint8_t &r, uint8_t &g, uint8_t &b) {  // h 0..359, full sat/val
  uint8_t sector = h / 60, f = (h % 60) * 255 / 60;
  uint8_t q = 255 - f, t = f;
  switch (sector) {
    case 0: r = 255; g = t; b = 0; break;
    case 1: r = q; g = 255; b = 0; break;
    case 2: r = 0; g = 255; b = t; break;
    case 3: r = 0; g = q; b = 255; break;
    case 4: r = t; g = 0; b = 255; break;
    default: r = 255; g = 0; b = q; break;
  }
}

void ledFlash(uint8_t r, uint8_t g, uint8_t b, uint32_t ms) {
  fr = r;
  fg = g;
  fb = b;
  flashUntil = millis() + ms;
}

void ledApply() {
  lastWrite = 0;  // force a redraw on next tick
  ledTick();
}

void ledTick() {
  uint32_t now = millis();
  // a flash overrides everything briefly
  if (now < flashUntil) {
    if (now - lastWrite < 30) return;
    lastWrite = now;
    raw(fr, fg, fb);
    return;
  }
  if (now - lastWrite < 40) return;  // ~25 fps is plenty
  lastWrite = now;
  uint8_t r = 0, g = 0, b = 0;
  switch (cfg.ledMode) {
    case 0:
    case 5:  // alerts only: idle dark (flashes handled above)
      raw(0, 0, 0);
      return;
    case 1:
      r = kR; g = kG; b = kB;
      break;
    case 2:
      accentRGB(r, g, b);
      break;
    case 3: {  // breathe the brand green
      float ph = (sinf(now / 1400.0f * PI) + 1) / 2;  // 0..1
      r = kR * ph; g = kG * ph; b = kB * ph;
      break;
    }
    case 4:
      hsv((now / 18) % 360, r, g, b);
      break;
  }
  raw(r, g, b);
}
