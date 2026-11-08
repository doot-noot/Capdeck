// CapDeck - RF 433 MHz: decode (RCSwitch), logic scope, saved codes + optional replay
#include "core.h"
#include <RCSwitch.h>

static RCSwitch rc;
static bool rxOn = false;

struct RfCode {
  String name;
  uint32_t value;
  uint8_t bits, proto;
  uint16_t pulse;
};

static const char *CODES_PATH = "/capdeck/rf/codes.txt";

static void rxStart() {
  if (cfg.rfRxPin < 0) return;
  pinMode(cfg.rfRxPin, INPUT);
  rc.enableReceive(digitalPinToInterrupt(cfg.rfRxPin));
  rxOn = true;
}
static void rxStop() {
  if (rxOn) rc.disableReceive();
  rxOn = false;
}

static std::vector<RfCode> loadCodes() {
  std::vector<RfCode> v;
  if (!sdOK) return v;
  File f = SD.open(CODES_PATH, FILE_READ);
  if (!f) return v;
  while (f.available()) {
    String l = f.readStringUntil('\n');
    l.trim();
    if (!l.length() || l[0] == '#') continue;
    // name,value,bits,protocol,pulse
    int p[4], from = 0;
    bool ok = true;
    for (int i = 0; i < 4; i++) {
      p[i] = l.indexOf(',', from);
      if (p[i] < 0) { ok = false; break; }
      from = p[i] + 1;
    }
    if (!ok) continue;
    RfCode c;
    c.name = l.substring(0, p[0]);
    c.value = strtoul(l.substring(p[0] + 1, p[1]).c_str(), nullptr, 0);
    c.bits = l.substring(p[1] + 1, p[2]).toInt();
    c.proto = l.substring(p[2] + 1, p[3]).toInt();
    c.pulse = l.substring(p[3] + 1).toInt();
    v.push_back(c);
  }
  f.close();
  return v;
}

static bool saveCode(const RfCode &c) {
  if (!sdOK) return false;
  bool fresh = !SD.exists(CODES_PATH);
  File f = SD.open(CODES_PATH, FILE_APPEND);
  if (!f) return false;
  if (fresh) f.println("# name,value,bits,protocol,pulse_us");
  f.printf("%s,0x%lX,%u,%u,%u\n", c.name.c_str(), (unsigned long)c.value, c.bits, c.proto, c.pulse);
  f.close();
  return true;
}

static void forwardToHA(const RfCode &c) {
  if (!haReady() || netState() != NetState::UP) return;
  char body[120];
  snprintf(body, sizeof body, "{\"code\":\"0x%lX\",\"bits\":%u,\"protocol\":%u,\"pulse\":%u}", (unsigned long)c.value, c.bits,
           c.proto, c.pulse);
  haPost("/api/webhook/capdeck_rf", body, nullptr, false);
}

static String codeStr(const RfCode &c) {
  char b[48];
  snprintf(b, sizeof b, "0x%lX  %ub  P%u  %uus", (unsigned long)c.value, c.bits, c.proto, c.pulse);
  return b;
}

static void receiveScreen() {
  if (cfg.rfRxPin < 0) { toast("Set an RX pin in Pin setup first", col::WARN); return; }
  rxStart();
  std::vector<std::pair<RfCode, int>> log;  // code + repeat count
  uint32_t lastFwd = 0, lastBlink = 0;
  bool dirty = true;
  while (true) {
    Key k = readKey();
    if (k.back) break;
    if (rc.available()) {
      RfCode c{"", (uint32_t)rc.getReceivedValue(), (uint8_t)rc.getReceivedBitlength(), (uint8_t)rc.getReceivedProtocol(),
               (uint16_t)rc.getReceivedDelay()};
      rc.resetAvailable();
      if (c.value) {
        if (!log.empty() && log.back().first.value == c.value) log.back().second++;
        else {
          log.push_back({c, 1});
          if (log.size() > 30) log.erase(log.begin());
          beep(2800, 20);
        }
        lastBlink = millis();
        if (cfg.rfToHA && millis() - lastFwd > 1000) { forwardToHA(c); lastFwd = millis(); }
        dirty = true;
      }
    }
    if (k.enter && !log.empty()) {
      RfCode c = log.back().first;
      String n = "remote_" + String(log.size());
      if (textInput("Name this code", n, col::RF, false, 24)) {
        n.replace(",", " ");
        c.name = n;
        toast(saveCode(c) ? "Saved to codes.txt" : "Save failed (SD?)", col::OK, 900);
      }
      dirty = true;
    }
    if (k.ch == 's' && !log.empty()) {  // replay the last code through the transmitter
      if (cfg.rfTxPin < 0) toast("Set a transmitter pin in Pin setup", col::WARN, 1200);
      else {
        RfCode c = log.back().first;
        rxStop();
        rc.enableTransmit(cfg.rfTxPin);
        rc.setProtocol(c.proto ? c.proto : 1);
        if (c.pulse) rc.setPulseLength(c.pulse);
        rc.setRepeatTransmit(8);
        rc.send(c.value, c.bits);
        rc.disableTransmit();
        rxStart();
        beep(3000, 20);
        toast("Replayed " + codeStr(c), col::OK, 700);
      }
      dirty = true;
    }
    if (k.ch == 'h' && !log.empty()) {
      if (!haReady()) toast("Home Assistant isn't set up in config.json", col::WARN);
      else if (wifiWait(12000, "Wi-Fi")) {
        forwardToHA(log.back().first);
        toast("Sent to webhook capdeck_rf", col::HA, 900);
      }
      dirty = true;
    }
    static uint32_t lastDraw = 0;
    if (!dirty && millis() - lastDraw < 250) { delay(5); continue; }
    dirty = false;
    lastDraw = millis();
    drawHeader("RF 433 receive", col::RF, ICON_RF);
    bool live = millis() - lastBlink < 400;
    canvas.fillCircle(SCR_W - 12, BAR_H + 9, 4, live ? col::OK : col::FAINT);
    canvas.setFont(&fonts::Font0);
    canvas.setTextDatum(middle_right);
    canvas.setTextColor(col::DIM);
    canvas.drawString("G" + String(cfg.rfRxPin), SCR_W - 20, BAR_H + 9);
    if (log.empty()) {
      drawIcon(canvas, ICON_RF, SCR_W / 2, 70, 44, col::RF);
      canvas.setFont(&fonts::Font2);
      canvas.setTextDatum(middle_center);
      canvas.setTextColor(col::TEXT);
      canvas.drawString("Listening... press a remote", SCR_W / 2, 106);
      canvas.setFont(&fonts::Font0);
      canvas.setTextColor(col::DIM);
      canvas.drawString(cfg.rfToHA ? "forwarding to Home Assistant" : "ENTER save  S replay  H to HA  DEL back", SCR_W / 2, 124);
    } else {
      auto &last = log.back();
      canvas.fillRoundRect(4, 36, SCR_W - 8, 36, 6, col::PANEL);
      canvas.drawRoundRect(4, 36, SCR_W - 8, 36, 6, col::RF);
      canvas.setFont(&fonts::FreeSansBold12pt7b);
      canvas.setTextDatum(middle_left);
      canvas.setTextColor(col::RF);
      char big[16];
      snprintf(big, sizeof big, "0x%lX", (unsigned long)last.first.value);
      canvas.drawString(big, 12, 50);
      canvas.setFont(&fonts::Font0);
      canvas.setTextColor(col::DIM);
      char det[48];
      snprintf(det, sizeof det, "%u bits  protocol %u  %u us  x%d", last.first.bits, last.first.proto, last.first.pulse, last.second);
      canvas.drawString(det, 12, 65);
      for (int i = 0; i < 5 && i + 1 < (int)log.size(); i++) {
        auto &e = log[log.size() - 2 - i];
        canvas.setTextColor(i == 0 ? col::TEXT : col::DIM);
        canvas.drawString(codeStr(e.first) + (e.second > 1 ? "  x" + String(e.second) : ""), 8, 80 + i * 10);
      }
      canvas.setTextDatum(bottom_right);
      canvas.setTextColor(col::FAINT);
      canvas.drawString("ENTER save  S replay  H to HA", SCR_W - 4, SCR_H - 1);
    }
    pushFrame();
  }
  rxStop();
}

// Raw logic view of the receiver output - handy for checking wiring/antenna
static void scopeScreen() {
  if (cfg.rfRxPin < 0) { toast("Set an RX pin in Pin setup first", col::WARN); return; }
  rxStop();
  pinMode(cfg.rfRxPin, INPUT);
  int usPerCol = 100;  // zoom
  const int traces = 4, th = 22;
  static uint8_t buf[traces][SCR_W];
  int t = 0;
  bool hold = false;
  while (true) {
    Key k = readKey();
    if (k.back) break;
    if (k.left) usPerCol = max(20, usPerCol / 2);
    if (k.right) usPerCol = min(2000, usPerCol * 2);
    if (k.space || k.enter) hold = !hold;
    if (!hold) {
      // sample one trace (240 columns), each column = high if any sample high
      for (int x = 0; x < SCR_W; x++) {
        uint32_t end = micros() + usPerCol;
        uint8_t v = 0;
        while ((int32_t)(micros() - end) < 0) v |= digitalRead(cfg.rfRxPin);
        buf[t % traces][x] = v;
      }
      t++;
    }
    drawHeader("RF scope", col::RF, ICON_RF);
    canvas.setFont(&fonts::Font0);
    canvas.setTextDatum(middle_right);
    canvas.setTextColor(hold ? col::WARN : col::DIM);
    char z[32];
    snprintf(z, sizeof z, "%s%d ms/screen", hold ? "HOLD " : "", usPerCol * SCR_W / 1000);
    canvas.drawString(z, SCR_W - 6, BAR_H + 9);
    for (int r = 0; r < traces; r++) {
      int idx = ((t - 1 - r) % traces + traces) % traces;
      int y0 = 36 + r * (th + 3);
      uint16_t c = r == 0 ? col::RF : col::DIM;
      canvas.drawFastHLine(0, y0 + th, SCR_W, col::FAINT);
      for (int x = 0; x < SCR_W; x++) {
        int y = buf[idx][x] ? y0 + 2 : y0 + th - 1;
        canvas.drawPixel(x, y, c);
        if (x && buf[idx][x] != buf[idx][x - 1]) canvas.drawFastVLine(x, y0 + 2, th - 3, c);
      }
    }
    pushFrame();
    if (hold) delay(30);
  }
}

static void savedScreen() {
  int sel = 0;
  while (true) {
    auto codes = loadCodes();
    std::vector<ListItem> items;
    for (auto &c : codes) {
      char b[16];
      snprintf(b, sizeof b, "0x%lX", (unsigned long)c.value);
      items.push_back({c.name, b, ICON_RF, 0});
    }
    String title = cfg.rfTxPin >= 0 ? "Saved - ENTER replays" : "Saved codes";
    int r = listMenu(title.c_str(), col::RF, items, sel, ICON_RF);
    if (r < 0) return;
    RfCode &c = codes[r];
    if (cfg.rfTxPin < 0) {
      textViewer(c.name.c_str(), codeStr(c) + "\n\nTo replay codes, wire a 433 MHz transmitter (e.g. FS1000A) and pick its pin in Pin setup.\n\nCodes live in /capdeck/rf/codes.txt - edit or delete them in Files.", col::RF);
      continue;
    }
    rxStop();
    rc.enableTransmit(cfg.rfTxPin);
    rc.setProtocol(c.proto ? c.proto : 1);
    if (c.pulse) rc.setPulseLength(c.pulse);
    rc.setRepeatTransmit(8);
    drawHeader("Transmitting", col::RF, ICON_RF);
    drawIcon(canvas, ICON_RF, SCR_W / 2, 75, 50, col::RF);
    pushFrame();
    rc.send(c.value, c.bits);
    rc.disableTransmit();
    beep(3000, 20);
    toast("Sent " + c.name, col::OK, 700);
  }
}

// Non-interactive: transmit a saved code by name. Used by the Macros engine.
String rfSendSaved(const String &name) {
  if (cfg.rfTxPin < 0) return "No 433 MHz transmitter pin set (RF 433 > Pin setup)";
  auto codes = loadCodes();
  for (auto &c : codes) {
    if (!c.name.equalsIgnoreCase(name)) continue;
    rxStop();
    rc.enableTransmit(cfg.rfTxPin);
    rc.setProtocol(c.proto ? c.proto : 1);
    if (c.pulse) rc.setPulseLength(c.pulse);
    rc.setRepeatTransmit(8);
    rc.send(c.value, c.bits);
    rc.disableTransmit();
    return "";
  }
  return "No saved RF code named \"" + name + "\"";
}

static String pinName(int8_t p) {
  if (p < 0) return "none";
  if (p == PIN_GROVE_YELLOW) return "G2 (Grove yellow)";
  if (p == PIN_GROVE_WHITE) return "G1 (Grove white)";
  return "G" + String(p);
}

static void pinSetup() {
  const int8_t opts[] = {PIN_GROVE_WHITE, PIN_GROVE_YELLOW, -1};
  int sel = 0;
  while (true) {
    std::vector<ListItem> items = {{"Receiver data pin", pinName(cfg.rfRxPin), ICON_RF, 0},
                                   {"Transmitter data pin", pinName(cfg.rfTxPin), ICON_RF, 0}};
    int r = listMenu("RF pin setup", col::RF, items, sel, ICON_CONF);
    if (r < 0) { saveSettings(); return; }
    int8_t &p = r == 0 ? cfg.rfRxPin : cfg.rfTxPin;
    int i = 0;
    while (i < 3 && opts[i] != p) i++;
    p = opts[(i + 1) % 3];
    if (cfg.rfRxPin >= 0 && cfg.rfRxPin == cfg.rfTxPin) {
      if (r == 0) cfg.rfTxPin = -1;
      else cfg.rfRxPin = -1;
    }
  }
}

static const char *RF_HELP =
    "WIRING (both modules share the Grove port)\n"
    "  GND  (both)     -> black\n"
    "  VCC  (both)     -> red (5V)\n"
    "  Receiver DATA   -> white (G1) through a divider:\n"
    "          DATA -10k-+-20k- GND, G1 at the +\n"
    "  Transmitter DATA -> yellow (G2), direct\n"
    "Antennas: the coils work; a straight 17.3 cm wire on each works better.\n\n"
    "The Grove port gives 5 V. Powered from 5 V the receiver's DATA line swings "
    "to 5 V, and the ESP32-S3 pins are 3.3 V only - so use the divider (or power "
    "the receiver from 3.3 V if your module allows).\n\n"
    "RECEIVE decodes common fixed-code remotes (doorbells, outlets, PT2262/EV1527 "
    "fobs). ENTER saves a code, H sends it to Home Assistant webhook 'capdeck_rf'.\n\n"
    "SCOPE shows the raw data line. , and / zoom, SPACE holds.\n\n"
    "REPLAY: press S on the receive screen to resend the last code, or pick one in Saved codes. Only replay "
    "your own devices. Rolling-code remotes (cars, garage doors) won't work.";

void appRF() {
  int sel = 0;
  while (true) {
    std::vector<ListItem> items = {{"Receive & decode", "", ICON_RF, 0},
                                   {"Scope", "", ICON_DISPLAY, 0},
                                   {"Saved codes", "", ICON_FILES, col::FILES},
                                   {"Forward to Home Assistant", cfg.rfToHA ? "ON" : "off", ICON_HA, col::HA},
                                   {"Pin setup", pinName(cfg.rfRxPin), ICON_CONF, 0},
                                   {"Wiring & help", "", ICON_INFO, 0}};
    int r = listMenu("RF 433 MHz", col::RF, items, sel, ICON_RF);
    if (r < 0) return;
    switch (r) {
      case 0: receiveScreen(); break;
      case 1: scopeScreen(); break;
      case 2: savedScreen(); break;
      case 3:
        cfg.rfToHA = !cfg.rfToHA;
        saveSettings();
        if (cfg.rfToHA) toast("Each decoded code is POSTed to webhook 'capdeck_rf' (Wi-Fi must be on)", col::HA, 2200);
        break;
      case 4: pinSetup(); break;
      case 5: textViewer("RF help", RF_HELP, col::RF); break;
    }
  }
}
