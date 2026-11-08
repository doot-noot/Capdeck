// CapDeck - Config: clock, battery, Wi-Fi, display, sound, system, launcher
#include "core.h"
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <esp_sleep.h>
#include <esp_system.h>  // esp_random (spore easter egg)
#include <sys/time.h>

static const char *DAYS[] = {"SUNDAY", "MONDAY", "TUESDAY", "WEDNESDAY", "THURSDAY", "FRIDAY", "SATURDAY"};
static const char *MONTHS[] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN", "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};

// ------------------------------------------------------------------ clock
static void setClockManually() {
  String v = timeValid() ? timeStr() : "12:00";
  if (!textInput("Time (24h HH:MM)", v, col::CONF, false, 5)) return;
  int c = v.indexOf(':');
  if (c < 0) return;
  int h = v.substring(0, c).toInt(), m = v.substring(c + 1).toInt();
  time_t now = time(nullptr);
  struct tm t;
  localtime_r(&now, &t);
  if (!timeValid()) { t.tm_year = 2026 - 1900; t.tm_mon = 0; t.tm_mday = 1; }
  t.tm_hour = constrain(h, 0, 23);
  t.tm_min = constrain(m, 0, 59);
  t.tm_sec = 0;
  t.tm_isdst = -1;
  struct timeval tv = {mktime(&t), 0};
  settimeofday(&tv, nullptr);
}

static void clockScreen() {
  uint32_t lastDraw = 0;
  while (true) {
    Key k = readKey();
    if (k.back) return;
    if (k.ch == 's') {
      if (wifiWait(15000, "Time sync")) {
        configTzTime(cfg.tz.c_str(), "pool.ntp.org", "time.nist.gov");
        uint32_t t = millis();
        while (!timeValid() && millis() - t < 5000) { readKey(); delay(50); }
      }
    }
    if (k.ch == 'm' || k.enter) setClockManually();
    if (millis() - lastDraw < 200) { delay(10); continue; }
    lastDraw = millis();
    canvas.fillScreen(col::BG);
    drawStatusBar("CLOCK", col::CONF);
    uint16_t a = accent();
    if (!timeValid()) {
      canvas.setFont(&fonts::Font7);
      canvas.setTextDatum(middle_center);
      canvas.setTextColor(col::FAINT);
      canvas.drawString("88:88", SCR_W / 2, 66);
      canvas.setFont(&fonts::Font2);
      canvas.setTextColor(col::TEXT);
      canvas.drawString("Clock not set", SCR_W / 2, 106);
      canvas.setFont(&fonts::Font0);
      canvas.setTextColor(col::DIM);
      canvas.drawString("S sync   M set by hand   GPS too", SCR_W / 2, 124);
      pushFrame();
      continue;
    }
    time_t now = time(nullptr);
    struct tm t;
    localtime_r(&now, &t);
    char hm[8];
    strftime(hm, sizeof hm, "%H:%M", &t);
    canvas.setFont(&fonts::Font7);
    canvas.setTextDatum(middle_center);
    canvas.setTextColor(0x18C3);
    canvas.drawString("88:88", 106, 62);  // ghost segments
    canvas.setTextColor(a);
    canvas.drawString(hm, 106, 62);
    // seconds ring
    canvas.fillArc(212, 62, 22, 18, 0, 360, col::FAINT);
    canvas.fillArc(212, 62, 22, 18, 270, 270 + t.tm_sec * 6, a);
    canvas.setFont(&fonts::FreeSansBold9pt7b);
    canvas.setTextColor(col::TEXT);
    char sec[4];
    snprintf(sec, sizeof sec, "%02d", t.tm_sec);
    canvas.drawString(sec, 212, 63);
    char date[32];
    snprintf(date, sizeof date, "%s  %s %d  %d", DAYS[t.tm_wday], MONTHS[t.tm_mon], t.tm_mday, t.tm_year + 1900);
    canvas.setFont(&fonts::Font2);
    canvas.setTextColor(col::TEXT);
    canvas.drawString(date, SCR_W / 2, 104);
    canvas.setFont(&fonts::Font0);
    canvas.setTextColor(col::DIM);
    canvas.drawString("S sync   M set   DEL back", SCR_W / 2, 124);
    pushFrame();
  }
}

// ------------------------------------------------------------------ battery
static void batteryScreen() {
  static std::vector<int16_t> hist;  // millivolts, one sample per 2 s
  uint32_t lastSample = 0, lastDraw = 0;
  while (true) {
    Key k = readKey();
    if (k.back) return;
    if (millis() - lastSample > 2000 || hist.empty()) {
      hist.push_back(M5Cardputer.Power.getBatteryVoltage());
      if (hist.size() > 110) hist.erase(hist.begin());
      lastSample = millis();
    }
    if (millis() - lastDraw < 500) { delay(10); continue; }
    lastDraw = millis();
    int lvl = M5Cardputer.Power.getBatteryLevel();
    int mv = hist.back();
    uint16_t c = lvl > 50 ? col::OK : lvl > 20 ? col::WARN : col::BAD;
    drawHeader("Battery", col::CONF, ICON_BATTERY);
    // big battery
    int bx = 8, by = 40, bw = 96, bh = 46;
    canvas.drawRoundRect(bx, by, bw, bh, 7, col::TEXT);
    canvas.drawRoundRect(bx + 1, by + 1, bw - 2, bh - 2, 6, col::TEXT);
    canvas.fillRoundRect(bx + bw, by + 14, 6, 18, 2, col::TEXT);
    int cells = (lvl + 19) / 20;
    for (int i = 0; i < 5; i++)
      canvas.fillRoundRect(bx + 5 + i * 18, by + 5, 15, bh - 10, 3, i < cells ? c : col::PANEL);
    canvas.setFont(&fonts::FreeSansBold18pt7b);
    canvas.setTextDatum(middle_left);
    canvas.setTextColor(col::TEXT);
    canvas.drawString(String(lvl) + "%", 116, 56);
    canvas.setFont(&fonts::Font2);
    canvas.setTextColor(col::DIM);
    canvas.drawString(String(mv / 1000.0f, 2) + " V", 118, 80);
    bool rising = hist.size() > 15 && hist.back() - hist[hist.size() - 15] > 15;
    if (mv > 4150 || rising) {
      canvas.setTextColor(col::WARN);
      canvas.drawString(mv > 4150 ? "full / on USB" : "charging", 170, 80);
    }
    // history graph
    int gx = 8, gy = 96, gw = SCR_W - 16, gh = 34;
    canvas.drawRect(gx, gy, gw, gh, col::FAINT);
    if (hist.size() > 1) {
      int lo = 3300, hi = 4250;
      for (size_t i = 1; i < hist.size(); i++) {
        int x0 = gx + (i - 1) * gw / 110, x1 = gx + i * gw / 110;
        int y0 = gy + gh - 1 - constrain(hist[i - 1] - lo, 0, hi - lo) * (gh - 2) / (hi - lo);
        int y1 = gy + gh - 1 - constrain(hist[i] - lo, 0, hi - lo) * (gh - 2) / (hi - lo);
        canvas.drawLine(x0, y0, x1, y1, c);
      }
    }
    canvas.setFont(&fonts::Font0);
    canvas.setTextDatum(top_left);
    canvas.setTextColor(col::DIM);
    canvas.drawString("voltage, last ~3 min", gx + 3, gy + 2);
    pushFrame();
  }
}

// ------------------------------------------------------------------ wifi
void wifiScanConnect() {
  drawHeader("Scanning...", col::CONF, ICON_WIFI);
  drawIcon(canvas, ICON_WIFI, SCR_W / 2, 80, 50, accent());
  pushFrame();
  bool wasOff = WiFi.getMode() == WIFI_OFF;
  if (wasOff) WiFi.mode(WIFI_STA);
  int n = WiFi.scanNetworks();
  if (n <= 0) { toast("No networks found", col::WARN); return; }
  std::vector<ListItem> items;
  std::vector<String> ssids;
  std::vector<bool> open;
  for (int i = 0; i < n && i < 30; i++) {
    String s = WiFi.SSID(i);
    if (!s.length()) continue;
    bool dup = false;
    for (auto &e : ssids) dup |= e == s;
    if (dup) continue;
    ssids.push_back(s);
    open.push_back(WiFi.encryptionType(i) == WIFI_AUTH_OPEN);
    items.push_back({s, String(WiFi.RSSI(i)) + (open.back() ? " open" : ""), ICON_WIFI, 0});
  }
  WiFi.scanDelete();
  int sel = 0;
  int r = listMenu("Pick a network", col::CONF, items, sel, ICON_WIFI);
  if (r < 0) return;
  String pass = (ssids[r] == cfg.wifiSsid) ? cfg.wifiPass : "";
  if (!open[r] && !textInput("Password", pass, col::CONF, true, 63)) return;
  cfg.wifiSsid = ssids[r];
  cfg.wifiPass = pass;
  saveSettings();
  wifiStop();
  if (wifiWait(15000, "Connecting")) toast("Connected  " + WiFi.localIP().toString(), col::OK, 1500);
  else toast("Couldn't connect - wrong password?", col::BAD, 1800);
}

static void wifiMenu() {
  int sel = 0;
  while (true) {
    String st = netState() == NetState::UP ? WiFi.SSID() : netState() == NetState::CONNECTING ? "connecting" : "off";
    std::vector<ListItem> items = {{"Status", st, ICON_WIFI, 0},
                                   {"Scan & connect", "", ICON_WIFI, 0},
                                   {"Connect now", "", ICON_POWER, 0},
                                   {"Turn Wi-Fi off", "", ICON_POWER, 0},
                                   {"Auto-connect at boot", cfg.autoWifi ? "ON" : "off", ICON_CONF, 0},
                                   {"Forget saved network", cfg.wifiSsid, ICON_FILES, col::BAD}};
    int r = listMenu("Wi-Fi", col::CONF, items, sel, ICON_WIFI);
    if (r < 0) return;
    switch (r) {
      case 0:
        if (netState() == NetState::UP)
          textViewer("Wi-Fi status", "SSID: " + WiFi.SSID() + "\nIP: " + WiFi.localIP().toString() + "\nGateway: " +
                                         WiFi.gatewayIP().toString() + "\nRSSI: " + String(WiFi.RSSI()) + " dBm\nMAC: " + WiFi.macAddress(),
                     col::CONF);
        else toast("Not connected", col::WARN, 900);
        break;
      case 1: wifiScanConnect(); break;
      case 2: if (wifiWait(15000, "Connecting")) toast("Connected  " + WiFi.localIP().toString(), col::OK, 1200); else toast("No luck", col::BAD); break;
      case 3: wifiStop(); toast("Wi-Fi off", col::DIM, 700); break;
      case 4: cfg.autoWifi = !cfg.autoWifi; saveSettings(); break;
      case 5:
        if (confirmBox("Forget " + cfg.wifiSsid + "? (networks in config.json stay)", col::BAD)) {
          cfg.wifiSsid = cfg.wifiPass = "";
          saveSettings();
        }
        break;
    }
  }
}

// ------------------------------------------------------------------ display / sound
static void sliderScreen(const char *title, uint8_t icon, uint8_t &value, uint8_t minV, std::function<void()> apply) {
  bool dirty = true;
  while (true) {
    Key k = readKey();
    if (k.back || k.enter) { saveSettings(); return; }
    if (k.left || k.down) { value = max<int>(minV, value - 15); apply(); dirty = true; }
    if (k.right || k.up) { value = min(255, value + 15); apply(); dirty = true; }
    if (!dirty) { delay(10); continue; }
    dirty = false;
    drawHeader(title, col::CONF, icon);
    drawIcon(canvas, icon, SCR_W / 2, 66, 46, accent());
    int pct = value * 100 / 255;
    canvas.fillRoundRect(20, 100, SCR_W - 40, 10, 5, col::PANEL);
    canvas.fillRoundRect(20, 100, (SCR_W - 40) * value / 255, 10, 5, accent());
    canvas.setFont(&fonts::Font0);
    canvas.setTextDatum(middle_center);
    canvas.setTextColor(col::DIM);
    canvas.drawString(String(pct) + "%    , / to adjust", SCR_W / 2, 122);
    pushFrame();
  }
}

static void displayMenu() {
  int sel = 0;
  const uint8_t dims[] = {15, 30, 60, 120, 0};
  while (true) {
    std::vector<ListItem> items = {{"Theme", THEMES[cfg.theme % THEME_COUNT].name, ICON_DISPLAY, accent()},
                                   {"Brightness", String(cfg.brightness * 100 / 255) + "%", ICON_DISPLAY, 0},
                                   {"Accent colour", cfg.accentIdx ? "custom" : "theme", ICON_DISPLAY, accent()},
                                   {"LED", LED_MODE_NAMES[cfg.ledMode % LED_MODE_COUNT], ICON_DISPLAY, 0},
                                   {"LED brightness", String(cfg.ledBright * 100 / 255) + "%", ICON_DISPLAY, 0},
                                   {"Dim after", cfg.dimSecs ? String(cfg.dimSecs) + " s" : "never", ICON_CLOCK, 0}};
    int r = listMenu("Display", col::CONF, items, sel, ICON_DISPLAY);
    if (r < 0) return;
    if (r == 0) { cfg.theme = (cfg.theme + 1) % THEME_COUNT; applyTheme(cfg.theme); saveSettings(); }
    if (r == 1) sliderScreen("Brightness", ICON_DISPLAY, cfg.brightness, 15, [] { applyBrightness(); });
    if (r == 2) { cfg.accentIdx = (cfg.accentIdx + 1) % 7; saveSettings(); }
    if (r == 3) { cfg.ledMode = (cfg.ledMode + 1) % LED_MODE_COUNT; ledApply(); saveSettings(); if (cfg.ledMode == 5) ledFlash(20, 230, 40, 500); }
    if (r == 4) sliderScreen("LED brightness", ICON_DISPLAY, cfg.ledBright, 0, [] { ledApply(); });
    if (r == 5) {
      int i = 0;
      while (i < 5 && dims[i] != cfg.dimSecs) i++;
      cfg.dimSecs = dims[(i + 1) % 5];
      saveSettings();
    }
  }
}

static void soundMenu() {
  int sel = 0;
  while (true) {
    std::vector<ListItem> items = {{"Sound", cfg.sound ? "ON" : "off", ICON_SOUND, 0},
                                   {"Volume", String(cfg.volume * 100 / 255) + "%", ICON_SOUND, 0},
                                   {"Test beep", "", ICON_SOUND, 0}};
    int r = listMenu("Sound", col::CONF, items, sel, ICON_SOUND);
    if (r < 0) return;
    if (r == 0) { cfg.sound = !cfg.sound; saveSettings(); }
    if (r == 1) sliderScreen("Volume", ICON_SOUND, cfg.volume, 0, [] { M5Cardputer.Speaker.setVolume(cfg.volume); beep(2000, 30); });
    if (r == 2) { beep(1500, 60); delay(80); beep(2200, 60); delay(80); beep(3000, 90); }
  }
}

// ------------------------------------------------------------------ easter egg: spores released
// Fires when you tap the build number enough times (see systemInfo). On-brand Deathcaps/Amanita moment.
static void sporeBurst() {
  struct P { float x, y, vx, vy; uint16_t c; };
  const uint16_t greens[] = {0x2FE6, 0x4FEA, 0x67F0, 0x9FF3};
  std::vector<P> ps;
  for (int i = 0; i < 48; i++) {
    float a = (float)i / 48 * 6.2832f;
    float sp = 0.7f + (esp_random() % 100) / 55.0f;
    ps.push_back({SCR_W / 2.0f, SCR_H / 2.0f + 6, cosf(a) * sp, sinf(a) * sp - 0.5f, greens[i & 3]});
  }
  for (int f = 0; f < 44; f++) {
    canvas.fillScreen(col::BG);
    int cy = SCR_H / 2 + 8 - min(f, 14);  // a little Amanita rising from the middle
    canvas.fillRect(SCR_W / 2 - 3, cy, 6, 15, col::DIM);  // stem
    canvas.fillCircle(SCR_W / 2, cy, 12, col::OK);         // cap
    for (int d = 0; d < 4; d++) canvas.fillCircle(SCR_W / 2 - 8 + d * 5, cy - 3, 1, col::TEXT);  // spots
    for (auto &p : ps) {
      p.x += p.vx * 3.0f; p.y += p.vy * 3.0f; p.vy += 0.10f;  // gravity
      if (f > 3) canvas.fillCircle((int)p.x, (int)p.y, f < 26 ? 2 : 1, p.c);
    }
    canvas.setTextDatum(middle_center);
    if (f > 8) {
      canvas.setFont(&fonts::FreeSansBold9pt7b);
      canvas.setTextColor(0x67F0);
      canvas.drawString("SPORES RELEASED", SCR_W / 2, 24);
    }
    if (f > 20) {
      canvas.setFont(&fonts::Font0);
      canvas.setTextColor(col::TEXT);
      canvas.drawString("developer mode unlocked", SCR_W / 2, SCR_H - 12);
    }
    pushFrame();
    delay(26);
  }
  beep(2600, 40); delay(40); beep(3200, 60);
  ledFlash(20, 230, 40, 600);
}

// ------------------------------------------------------------------ system info (tap the build number to unlock dev mode)
static void systemInfo() {
  uint32_t up = millis() / 1000;
  String s;
  s += "Chip: " + String(ESP.getChipModel()) + " rev " + String(ESP.getChipRevision()) + " @ " + String(ESP.getCpuFreqMHz()) + " MHz\n";
  s += "Flash: " + humanSize(ESP.getFlashChipSize()) + "   App: " + humanSize(ESP.getSketchSize()) + "\n";
  s += "Free heap: " + humanSize(ESP.getFreeHeap()) + " (min " + humanSize(ESP.getMinFreeHeap()) + ")\n";
  s += "Uptime: " + String(up / 3600) + "h " + String((up / 60) % 60) + "m " + String(up % 60) + "s\n";
  s += "SD: " + (sdOK ? humanSize(SD.usedBytes()) + " used of " + humanSize(SD.totalBytes()) : String("none")) + "\n";
  s += "LoRa cap: " + String(loraPresent ? "yes" : "no") + "   GPS data: " + String(gpsSeen ? "yes" : "no") + "\n";
  s += "Battery: " + String(M5Cardputer.Power.getBatteryVoltage()) + " mV\n";
  s += "Wi-Fi MAC: " + WiFi.macAddress() + "\n";
  const esp_partition_t *run = esp_ota_get_running_partition();
  if (run) s += "Running from: " + String(run->label) + "\n";
  s += "\nKeys: ; . up/down   , / left/right   ENTER taps build   DEL back";
  canvas.setFont(&fonts::Font0);
  std::vector<String> lines;
  wrapText(s, SCR_W - 12, lines);
  int scroll = 0, taps = 0;
  while (true) {
    Key k = readKey();
    if (k.back) return;
    if ((k.up || k.ch == ';') && scroll > 0) scroll--;
    if ((k.down || k.ch == '.') && scroll < (int)lines.size() - 1) scroll++;
    if (k.enter) {
      if (devMode()) toast("You're already a developer", col::MESH, 1100);
      else if (++taps >= 7) { setDevMode(true); sporeBurst(); toast("you really shouldn't have done that", col::OK, 1700); taps = 0; }
      else if (taps >= 4) { int left = 7 - taps; toast(String(left) + (left == 1 ? " tap to go..." : " taps to go..."), col::DIM, 700); }
    }
    canvas.fillScreen(col::BG);
    drawStatusBar("SYSTEM INFO", col::CONF);
    canvas.setTextDatum(top_left);
    canvas.setFont(&fonts::FreeSansBold9pt7b);
    canvas.setTextColor(accent());
    canvas.drawString("CapDeck " CAPDECK_VERSION, 6, BAR_H + 4);
    canvas.setFont(&fonts::Font0);
    canvas.setTextColor(col::DIM);
    canvas.drawString("build " CAPDECK_BUILD, 6, BAR_H + 24);
    int y = BAR_H + 38;
    for (int i = scroll; i < (int)lines.size() && y < SCR_H - 2; i++, y += 11) {
      canvas.setTextColor(col::TEXT);
      canvas.drawString(lines[i], 6, y);
    }
    pushFrame();
  }
}

static void backToLauncher() {
  // M5Launcher lives in its own app partition; boot it again.
  const esp_partition_t *running = esp_ota_get_running_partition();
  const esp_partition_t *target = esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_TEST, nullptr);
  if (!target) target = esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_FACTORY, nullptr);
  if (!target || target == running) {
    toast("Launcher partition not found. Power off, hold the launcher key and power on.", col::WARN, 2500);
    return;
  }
  if (!confirmBox("Restart into M5Launcher?", col::CONF)) return;
  esp_ota_set_boot_partition(target);
  ESP.restart();
}

static void sleepNow() {
  if (!confirmBox("Deep sleep? Press G0 to wake.", col::CONF)) return;
  M5Cardputer.Display.setBrightness(0);
  M5Cardputer.Display.sleep();
  wifiStop();
  esp_sleep_enable_ext0_wakeup(GPIO_NUM_0, 0);
  esp_deep_sleep_start();
}

// ------------------------------------------------------------------ developer tools
static void liveMemory() {
  static std::vector<uint16_t> hist;  // free KB over time
  uint32_t last = 0;
  uint32_t startMin = ESP.getMinFreeHeap();
  while (true) {
    Key k = readKey();
    if (k.back) return;
    if (k.ch == 'r') { hist.clear(); }  // can't reset the chip's min, but clears the graph
    if (millis() - last < 400) { delay(10); continue; }
    last = millis();
    uint32_t freeNow = ESP.getFreeHeap();
    uint32_t minEver = ESP.getMinFreeHeap();
    uint32_t biggest = ESP.getMaxAllocHeap();
    hist.push_back(freeNow / 1024);
    if (hist.size() > 220) hist.erase(hist.begin());
    drawHeader("Live memory", col::CONF, ICON_INFO);
    canvas.setFont(&fonts::Font0);
    canvas.setTextDatum(top_left);
    int y = BAR_H + 20;
    auto row = [&](const char *a, const String &b, uint16_t c) {
      canvas.setTextColor(col::DIM);
      canvas.drawString(a, 6, y);
      canvas.setTextColor(c);
      canvas.drawString(b, 96, y);
      y += 11;
    };
    // fragmentation = how much smaller the biggest single block is than total free
    int frag = freeNow ? 100 - (int)(biggest * 100 / freeNow) : 0;
    row("Free now", humanSize(freeNow), col::TEXT);
    row("Lowest ever", humanSize(minEver), minEver < 40000 ? col::BAD : minEver < 80000 ? col::WARN : col::OK);
    row("Biggest block", humanSize(biggest), col::TEXT);
    row("Fragmentation", String(frag) + "%", frag > 40 ? col::WARN : col::OK);
    // graph
    int gx = 6, gy = BAR_H + 66, gw = SCR_W - 12, gh = SCR_H - gy - 12;
    canvas.drawRect(gx, gy, gw, gh, col::FAINT);
    if (hist.size() > 1) {
      uint16_t lo = 9999, hi = 0;
      for (auto v : hist) { lo = min(lo, v); hi = max(hi, v); }
      if (hi - lo < 10) hi = lo + 10;
      for (size_t i = 1; i < hist.size(); i++) {
        int x0 = gx + 1 + (i - 1) * (gw - 2) / 220, x1 = gx + 1 + i * (gw - 2) / 220;
        int y0 = gy + gh - 1 - (hist[i - 1] - lo) * (gh - 2) / (hi - lo);
        int y1 = gy + gh - 1 - (hist[i] - lo) * (gh - 2) / (hi - lo);
        canvas.drawLine(x0, y0, x1, y1, col::CONF);
      }
    }
    canvas.setTextDatum(bottom_right);
    canvas.setTextColor(col::DIM);
    canvas.drawString("watch 'Lowest'   R clears graph", SCR_W - 4, SCR_H - 1);
    pushFrame();
  }
}

static void viewTextFile(const char *title, const char *path) {
  if (!sdOK || !SD.exists(path)) { toast("Not found: " + String(path), col::WARN, 1200); return; }
  File f = SD.open(path, FILE_READ);
  String s;
  while (f.available() && s.length() < 8000) s += (char)f.read();
  if (f.available()) s += "\n[... truncated ...]";
  f.close();
  textViewer(title, s, col::CONF);
}

static void devScreen() {
  int sel = 0;
  while (true) {
    std::vector<ListItem> items = {{"Developer mode", devMode() ? "ON" : "off", ICON_CONF, devMode() ? col::OK : col::DIM},
                                   {"Live memory", humanSize(ESP.getFreeHeap()) + " free", ICON_INFO, 0},
                                   {"View config.json", "", ICON_FILES, col::FILES},
                                   {"View macros.json", "", ICON_FILES, col::FILES},
                                   {"Save diagnostics to SD", "capdeck_diag.txt", ICON_FILES, col::FILES}};
    int r = listMenu("DO_NOT_TOUCH.json", col::CONF, items, sel, ICON_CONF);
    if (r < 0) return;
    switch (r) {
      case 0:
        setDevMode(!devMode());
        toast(devMode() ? "Dev mode on - heap shows in the status bar" : "Dev mode off - re-unlock in System info", col::CONF, 1500);
        break;
      case 1: liveMemory(); break;
      case 2: viewTextFile("config.json", "/capdeck/config.json"); break;
      case 3: viewTextFile("macros.json", "/capdeck/macros.json"); break;
      case 4: {
        if (!sdOK) { toast("No SD card", col::BAD, 1000); break; }
        File f = SD.open("/capdeck/capdeck_diag.txt", FILE_WRITE);
        if (!f) { toast("Couldn't write to SD", col::BAD, 1000); break; }
        f.println("CapDeck v" CAPDECK_VERSION " diagnostics");
        f.printf("chip %s rev %d, %d MHz, flash %u\n", ESP.getChipModel(), ESP.getChipRevision(), ESP.getCpuFreqMHz(),
                 (unsigned)ESP.getFlashChipSize());
        f.printf("heap free %u, min %u, biggest %u\n", (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMinFreeHeap(),
                 (unsigned)ESP.getMaxAllocHeap());
        f.printf("app size %u\n", (unsigned)ESP.getSketchSize());
        f.printf("lora present %d, mode %d, tcxo %.1f\n", loraPresent, (int)radioMode, loraTcxoUsed);
        f.print("lora init log:\n");
        f.print(loraInitLog);
        f.printf("mesh nodes %u, packets %u\n", (unsigned)meshNodes.size(), (unsigned)meshPackets);
        f.printf("wifi %s, mac %s\n", WiFi.SSID().c_str(), WiFi.macAddress().c_str());
        f.close();
        toast("Saved to /capdeck/capdeck_diag.txt - grab it in Portal > Files", col::OK, 2200);
        break;
      }
    }
  }
}

void appSettings() {
  // Action IDs instead of raw row indices, so hiding a row (e.g. Developer) never shifts the wrong case.
  enum Act { A_THEME, A_CLOCK, A_BATT, A_WIFI, A_DISP, A_SOUND, A_RELOAD, A_SYS, A_DEV, A_SETUP, A_LAUNCH, A_SLEEP, A_REBOOT };
  int sel = 0;
  while (true) {
    String clk = timeValid() ? timeStr() : "not set";
    String bat = String(M5Cardputer.Power.getBatteryLevel()) + "%";
    String wf = netState() == NetState::UP ? WiFi.SSID() : "off";
    std::vector<ListItem> items;
    std::vector<Act> acts;
    auto add = [&](ListItem it, Act a) { items.push_back(it); acts.push_back(a); };
    add({"Theme", THEMES[cfg.theme % THEME_COUNT].name, ICON_DISPLAY, accent()}, A_THEME);
    add({"Clock", clk, ICON_CLOCK, 0}, A_CLOCK);
    add({"Battery", bat, ICON_BATTERY, 0}, A_BATT);
    add({"Wi-Fi", wf, ICON_WIFI, 0}, A_WIFI);
    add({"Display", "", ICON_DISPLAY, 0}, A_DISP);
    add({"Sound", cfg.sound ? "on" : "off", ICON_SOUND, 0}, A_SOUND);
    add({"Reload config.json", "", ICON_FILES, col::FILES}, A_RELOAD);
    add({"System info", "", ICON_INFO, 0}, A_SYS);
    if (devMode()) add({"DO_NOT_TOUCH.json", "cheats", ICON_CONF, col::OK}, A_DEV);  // hidden until unlocked in System info
    add({"Run setup again", "", ICON_CONF, 0}, A_SETUP);
    add({"Back to M5Launcher", "", ICON_POWER, col::WARN}, A_LAUNCH);
    add({"Sleep", "", ICON_POWER, 0}, A_SLEEP);
    add({"Reboot", "", ICON_POWER, col::BAD}, A_REBOOT);
    int r = listMenu("Config", col::CONF, items, sel, ICON_CONF);
    if (r < 0) return;
    switch (acts[r]) {
      case A_THEME: cfg.theme = (cfg.theme + 1) % THEME_COUNT; applyTheme(cfg.theme); saveSettings(); break;
      case A_CLOCK: clockScreen(); break;
      case A_BATT: batteryScreen(); break;
      case A_WIFI: wifiMenu(); break;
      case A_DISP: displayMenu(); break;
      case A_SOUND: soundMenu(); break;
      case A_RELOAD:
        if (loadConfigFile()) { meshBuildChannels(); loraApplyParams(); toast("Config reloaded", col::OK, 900); }
        else toast(sdOK ? "config.json has a JSON error" : "No SD card", col::BAD, 1500);
        break;
      case A_SYS: systemInfo(); break;
      case A_DEV: devScreen(); break;
      case A_SETUP: { void firstRunWizard(); firstRunWizard(); break; }
      case A_LAUNCH: backToLauncher(); break;
      case A_SLEEP: sleepNow(); break;
      case A_REBOOT: if (confirmBox("Reboot now?", col::BAD)) ESP.restart(); break;
    }
  }
}
