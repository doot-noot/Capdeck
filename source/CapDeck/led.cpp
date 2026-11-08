// CapDeck - on-board RGB LED. Follows the theme's colour; events get a soft pulse, never a strobe.
// (A busy mesh used to fire a hard flash per packet. Pulses are smooth and rate-limited now.)
#include "core.h"

const char *LED_MODE_NAMES[] = {"Off", "Theme colour", "Breathe", "Brand green", "Rainbow", "Alerts only"};

static const uint32_t BRAND = 0x14E628;  // Deathcaps toxic green
static uint32_t pulseStart = 0, lastPulse = 0;
static uint32_t pulseRGB = 0;
static uint32_t lastWrite = 0;
static const uint32_t PULSE_MS = 900, PULSE_GAP = 1800;

bool screenIsOff();  // power.cpp

static void raw(uint32_t rgb, float level = 1.0f) {
  float s = cfg.ledBright / 255.0f * level;
  rgbLedWrite(PIN_LED, (uint8_t)(((rgb >> 16) & 0xFF) * s), (uint8_t)(((rgb >> 8) & 0xFF) * s), (uint8_t)((rgb & 0xFF) * s));
}

static uint32_t themeRGB() { return THEMES[cfg.theme % THEME_COUNT].led; }

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

void ledPulse(uint32_t rgb) {
  if (cfg.ledMode == 0) return;
  uint32_t now = millis();
  if (now - lastPulse < PULSE_GAP) return;  // a busy mesh can't turn this into a strobe
  lastPulse = pulseStart = now;
  pulseRGB = rgb ? rgb : themeRGB();
}

void ledFlash(uint8_t r, uint8_t g, uint8_t b, uint32_t) { ledPulse(((uint32_t)r << 16) | ((uint32_t)g << 8) | b); }

void ledApply() {
  lastWrite = 0;  // force a redraw on next tick
  ledTick();
}

void ledTick() {
  uint32_t now = millis();
  if (now - lastWrite < 40) return;  // ~25 fps is plenty
  lastWrite = now;
  // a pulse: smooth rise and fall, overriding the idle look
  if (pulseStart && now - pulseStart < PULSE_MS) {
    float ph = (now - pulseStart) / (float)PULSE_MS;
    raw(pulseRGB, sinf(ph * PI));
    return;
  }
  if (screenIsOff()) { raw(0); return; }  // screen off: dark, except for pulses
  switch (cfg.ledMode) {
    case 0:
    case 5: raw(0); return;  // off / alerts only
    case 1: raw(themeRGB()); return;
    case 2: raw(themeRGB(), 0.15f + 0.85f * (sinf(now / 1600.0f * PI) + 1) / 2); return;  // slow breathe
    case 3: raw(BRAND); return;
    case 4: {
      uint8_t r, g, b;
      hsv((now / 30) % 360, r, g, b);
      raw(((uint32_t)r << 16) | ((uint32_t)g << 8) | b, 0.8f);
      return;
    }
  }
}
