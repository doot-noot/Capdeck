// CapDeck - GPS (Cap LoRa-1262 ATGM336H): live fix, radar view, waypoint logging
#include "core.h"
#include <TinyGPSPlus.h>

TinyGPSPlus &gpsRef();

static bool logWaypoint(const String &label) {
  TinyGPSPlus &g = gpsRef();
  if (!sdOK || !gpsFix()) return false;
  bool fresh = !SD.exists("/capdeck/gps/waypoints.csv");
  File f = SD.open("/capdeck/gps/waypoints.csv", FILE_APPEND);
  if (!f) return false;
  if (fresh) f.println("utc,lat,lon,alt_m,sats,label");
  char line[160];
  snprintf(line, sizeof line, "%04d-%02d-%02dT%02d:%02d:%02dZ,%.6f,%.6f,%.1f,%d,%s", g.date.year(), g.date.month(), g.date.day(),
           g.time.hour(), g.time.minute(), g.time.second(), g.location.lat(), g.location.lng(), g.altitude.meters(),
           gpsSats(), label.c_str());
  f.println(line);
  f.close();
  return true;
}

void appGps() {
  TinyGPSPlus &g = gpsRef();
  int frame = 0;
  uint32_t lastDraw = 0, opened = millis();
  bool imperial = true;
  while (true) {
    Key k = readKey();
    if (k.back) return;
    if (k.ch == 'u') imperial = !imperial;
    if (k.enter) {
      String label = "wp";
      if (!gpsFix()) toast("No fix yet - go outside / near a window", col::WARN, 1500);
      else if (textInput("Waypoint label", label, col::GPS, false, 24)) {
        label.replace(",", " ");
        toast(logWaypoint(label) ? "Saved to /capdeck/gps/waypoints.csv" : "Save failed", col::OK, 1100);
      }
    }
    if (millis() - lastDraw < 120) { delay(5); continue; }
    lastDraw = millis();
    frame++;
    drawHeader("GPS", col::GPS, ICON_GPS);
    // radar
    int cx = 52, cy = 82, R = 44;
    canvas.fillCircle(cx, cy, R, 0x0120);
    for (int r = R; r > 0; r -= 15) canvas.drawCircle(cx, cy, r, 0x02A4);
    canvas.drawFastHLine(cx - R, cy, R * 2, 0x02A4);
    canvas.drawFastVLine(cx, cy - R, R * 2, 0x02A4);
    float a = (frame * 9 % 360) * DEG_TO_RAD;
    for (int t = 0; t < 14; t++) {
      float b = a - t * 0.05f;
      canvas.drawLine(cx, cy, cx + cosf(b) * R, cy + sinf(b) * R, t == 0 ? col::GPS : (uint16_t)(0x0200 + (14 - t) * 0x40));
    }
    // pseudo satellite blips: one per satellite in view, deterministic positions
    int sats = gpsSats();
    for (int i = 0; i < sats && i < 16; i++) {
      float sa = i * 2.39996f;
      float sr = R * (0.35f + 0.6f * ((i * 37) % 10) / 10.0f);
      canvas.fillCircle(cx + cosf(sa) * sr, cy + sinf(sa) * sr, 2, gpsFix() ? col::GPS : col::WARN);
    }
    // readout
    canvas.setFont(&fonts::Font2);
    canvas.setTextDatum(top_left);
    int x = 104, y = 34;
    auto row = [&](const char *k, const String &v, uint16_t c = col::TEXT) {
      canvas.setFont(&fonts::Font0);
      canvas.setTextColor(col::DIM);
      canvas.drawString(k, x, y + 4);
      canvas.setFont(&fonts::Font2);
      canvas.setTextColor(c);
      canvas.drawString(v, x + 30, y);
      y += 15;
    };
    if (!gpsSeen) {
      row("GPS", millis() - opened < 3000 ? "starting..." : "no data", col::WARN);
      canvas.setFont(&fonts::Font0);
      canvas.setTextColor(col::DIM);
      canvas.drawString("Is the LoRa cap on?", x, y + 4);
    } else {
      bool fix = gpsFix();
      row("FIX", fix ? "3D fix" : "searching", fix ? col::OK : col::WARN);
      row("LAT", fix ? String(g.location.lat(), 6) : "--");
      row("LON", fix ? String(g.location.lng(), 6) : "--");
      String alt = !g.altitude.isValid() ? "--" : imperial ? String((int)g.altitude.feet()) + " ft" : String((int)g.altitude.meters()) + " m";
      row("ALT", alt);
      String spd = !g.speed.isValid() ? "--" : imperial ? String(g.speed.mph(), 1) + " mph" : String(g.speed.kmph(), 1) + " km/h";
      row("SPD", spd);
      row("SAT", String(sats) + (g.hdop.isValid() ? "  hdop " + String(g.hdop.hdop(), 1) : ""));
    }
    canvas.setFont(&fonts::Font0);
    canvas.setTextDatum(bottom_right);
    canvas.setTextColor(col::FAINT);
    canvas.drawString("ENTER waypoint  U units", SCR_W - 3, SCR_H - 1);
    pushFrame();
  }
}
