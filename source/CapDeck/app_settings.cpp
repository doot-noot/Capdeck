// CapDeck - Config: clock, battery, Wi-Fi, display, sound, system, launcher
#include "core.h"
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <esp_sleep.h>
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
      canvas.drawString("S sync via Wi-Fi   M set by hand   (GPS sets it too)", SCR_W / 2, 124);
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
static void wifiScan() {
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
      case 1: wifiScan(); break;
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
                                   {"Dim after", cfg.dimSecs ? String(cfg.dimSecs) + " s" : "never", ICON_CLOCK, 0}};
    int r = listMenu("Display", col::CONF, items, sel, ICON_DISPLAY);
    if (r < 0) return;
    if (r == 0) { cfg.theme = (cfg.theme + 1) % THEME_COUNT; applyTheme(cfg.theme); saveSettings(); }
    if (r == 1) sliderScreen("Brightness", ICON_DISPLAY, cfg.brightness, 15, [] { applyBrightness(); });
    if (r == 2) { cfg.accentIdx = (cfg.accentIdx + 1) % 7; saveSettings(); }
    if (r == 3) {
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

// ------------------------------------------------------------------ system
static void systemInfo() {
  uint32_t up = millis() / 1000;
  String s;
  s += "CapDeck v" CAPDECK_VERSION "\n";
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
  s += "\nKeys everywhere:\n ; . up/down   , / left/right\n ENTER or G0 select   DEL back\n 1-9 jump in lists";
  textViewer("System info", s, col::CONF);
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

void appSettings() {
  int sel = 0;
  while (true) {
    String clk = timeValid() ? timeStr() : "not set";
    String bat = String(M5Cardputer.Power.getBatteryLevel()) + "%";
    String wf = netState() == NetState::UP ? WiFi.SSID() : "off";
    std::vector<ListItem> items = {{"Theme", THEMES[cfg.theme % THEME_COUNT].name, ICON_DISPLAY, accent()},
                                   {"Clock", clk, ICON_CLOCK, 0},
                                   {"Battery", bat, ICON_BATTERY, 0},
                                   {"Wi-Fi", wf, ICON_WIFI, 0},
                                   {"Display", "", ICON_DISPLAY, 0},
                                   {"Sound", cfg.sound ? "on" : "off", ICON_SOUND, 0},
                                   {"Reload config.json", "", ICON_FILES, col::FILES},
                                   {"System info", "", ICON_INFO, 0},
                                   {"Back to M5Launcher", "", ICON_POWER, col::WARN},
                                   {"Sleep", "", ICON_POWER, 0},
                                   {"Reboot", "", ICON_POWER, col::BAD}};
    int r = listMenu("Config", col::CONF, items, sel, ICON_CONF);
    if (r < 0) return;
    switch (r) {
      case 0: cfg.theme = (cfg.theme + 1) % THEME_COUNT; applyTheme(cfg.theme); saveSettings(); break;
      case 1: clockScreen(); break;
      case 2: batteryScreen(); break;
      case 3: wifiMenu(); break;
      case 4: displayMenu(); break;
      case 5: soundMenu(); break;
      case 6:
        if (loadConfigFile()) { loraApplyParams(); toast("Config reloaded", col::OK, 900); }
        else toast(sdOK ? "config.json has a JSON error" : "No SD card", col::BAD, 1500);
        break;
      case 7: systemInfo(); break;
      case 8: backToLauncher(); break;
      case 9: sleepNow(); break;
      case 10: if (confirmBox("Reboot now?", col::BAD)) ESP.restart(); break;
    }
  }
}
