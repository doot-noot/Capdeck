// CapDeck - first-run setup: theme, tips, Wi-Fi, Meshtastic listening. Everything else is in Portal.
#include "core.h"

void wifiScanConnect();  // app_settings.cpp

// A setup card. The body text wraps to the space beside the icon (it used to run off the screen).
static void card(const char *title, const char *body, uint8_t icon, uint16_t c, int step, int steps) {
  const int tx = 92, tw = SCR_W - tx - 6;
  canvas.fillScreen(col::BG);
  drawStatusBar("SETUP", accent());
  drawIcon(canvas, icon, 46, 72, 58, c);
  canvas.setFont(&fonts::FreeSansBold12pt7b);
  canvas.setTextDatum(top_left);
  canvas.setTextColor(col::TEXT);
  canvas.drawString(title, tx, 26);
  canvas.setFont(&fonts::Font2);
  canvas.setTextColor(col::DIM);
  std::vector<String> lines;
  wrapText(body, tw, lines);
  for (size_t i = 0; i < lines.size() && i < 4; i++) canvas.drawString(lines[i], tx, 54 + i * 15);
  canvas.setFont(&fonts::Font0);
  canvas.setTextColor(c);
  canvas.drawString("ENTER yes    DEL no", tx, 116);
  for (int i = 0; i < steps; i++) canvas.fillRoundRect(tx + i * 14, 128, 10, 4, 2, i <= step ? c : col::FAINT);
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
  const int steps = 6;
  card("Hi!", "CapDeck quick setup. Takes about a minute.", ICON_CONF, accent(), 0, steps);
  if (!ask()) goto done;

  card("Theme", "Every theme is alive. Pick one you like (you can change it any time in Config).", ICON_DISPLAY, accent(), 1, steps);
  if (ask()) themePicker();

  card("Tips", "Show longer hints while you learn? They shrink on their own once you know an app.", ICON_INFO, accent(), 2, steps);
  cfg.tips = ask();
  saveSettings();

  card("Wi-Fi", "Join your home Wi-Fi? Needed for Home Assistant, Local AI and lookups.", ICON_WIFI, accent(), 3, steps);
  if (ask()) wifiScanConnect();

  if (loraPresent) {
    card("Mycelium", "Listen for Meshtastic nodes nearby? (LongFast, the usual public channel)", ICON_MESH, col::MESH, 4, steps);
    if (ask()) {
      cfg.meshListen = true;
      cfg.meshPreset = 0;  // LongFast - what most public meshes use
      saveSettings();
      meshBuildChannels();
      loraSetMode(RM_MESH);
    }
  }

  card("Done!", "Open Portal any time to set up Home Assistant and AI from your phone.", ICON_PORTAL, col::PORTAL, 5, steps);
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

// ------------------------------------------------------------------ tips
// Longer hints for an app's first 3 opens; after that, the short ones. Opens are counted per app in NVS.
static uint8_t curOpens = 255;

void tipsOpened(const char *app) {
  uint32_t h = 2166136261u;
  for (const char *c = app; *c; c++) h = (h ^ (uint8_t)*c) * 16777619u;
  char key[12];
  snprintf(key, sizeof key, "o%08lx", (unsigned long)h);
  Preferences p;
  p.begin("tips", false);
  curOpens = p.getUChar(key, 0);
  if (curOpens < 250) p.putUChar(key, curOpens + 1);
  p.end();
}

bool tipsActive() { return cfg.tips && curOpens < 3; }

const char *tip(const char *longText, const char *shortText) { return tipsActive() ? longText : shortText; }

void tipsReset() {
  Preferences p;
  p.begin("tips", false);
  p.clear();
  p.end();
  curOpens = 0;
}
