// CapDeck - first-run setup: theme, Wi-Fi, Meshtastic listening. Everything else is in Portal.
#include "core.h"

void wifiScanConnect();  // app_settings.cpp

static void card(const char *title, const char *line1, const char *line2, uint8_t icon, uint16_t c, int step, int steps) {
  canvas.fillScreen(col::BG);
  drawStatusBar("SETUP", accent());
  drawIcon(canvas, icon, 46, 74, 60, c);
  canvas.setFont(&fonts::FreeSansBold12pt7b);
  canvas.setTextDatum(top_left);
  canvas.setTextColor(col::TEXT);
  canvas.drawString(title, 92, 34);
  canvas.setFont(&fonts::Font2);
  canvas.setTextColor(col::DIM);
  std::vector<String> lines;  // wrap instead of running off the right edge
  wrapText(String(line1) + " " + line2, SCR_W - 92 - 4, lines);
  for (size_t i = 0; i < lines.size() && i < 3; i++) canvas.drawString(lines[i], 92, 60 + i * 15);
  canvas.setFont(&fonts::Font0);
  canvas.setTextColor(c);
  canvas.drawString("ENTER yes   DEL skip", 92, 110);
  for (int i = 0; i < steps; i++) canvas.fillRoundRect(92 + i * 14, 124, 10, 4, 2, i <= step ? c : col::FAINT);
  pushFrame();
}

static bool ask() {
  while (true) {
    Key k = readKey();
    if (k.enter) return true;
    if (k.back) return false;
    delay(10);
  }
}

void firstRunWizard() {
  const int steps = 4;
  card("Hi!", "CapDeck quick setup.", "Takes about a minute.", ICON_CONF, accent(), 0, steps);
  if (!ask()) goto done;

  {  // theme
    std::vector<ListItem> items;
    for (int i = 0; i < THEME_COUNT; i++) items.push_back({THEMES[i].name, "", ICON_DISPLAY, THEMES[i].accent});
    int sel = cfg.theme;
    int r = listMenu("Pick a theme", accent(), items, sel, ICON_DISPLAY);
    if (r >= 0) {
      cfg.theme = r;
      applyTheme(r);
      saveSettings();
    }
  }

  card("Wi-Fi", "Join your home Wi-Fi?", "Needed for HA & Local AI.", ICON_WIFI, accent(), 1, steps);
  if (ask()) wifiScanConnect();

  if (loraPresent) {
    card("Mycelium", "Listen for Meshtastic", "nodes nearby? (LongFast)", ICON_MESH, col::MESH, 2, steps);
    if (ask()) {
      cfg.meshListen = true;
      cfg.meshPreset = 0;  // LongFast - what Michigan Mesh uses
      saveSettings();
      meshBuildChannels();
      loraSetMode(RM_MESH);
    }
  }

  card("Done!", "Open PORTAL any time to", "set up HA + AI from your phone", ICON_PORTAL, col::PORTAL, 3, steps);
  ask();

done:
  Preferences p;
  p.begin("capdeck", false);
  p.putBool("setup12", true);
  p.end();
}

bool needsFirstRun() {
  Preferences p;
  p.begin("capdeck", true);
  bool done = p.getBool("setup12", false);
  p.end();
  return !done;
}
