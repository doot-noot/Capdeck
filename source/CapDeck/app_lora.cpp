// CapDeck - LoRa (Cap LoRa-1262): chat, packet monitor, ping, radio settings
#include "core.h"
#include <ArduinoJson.h>

static bool needCap() {
  if (!loraPresent && !loraInit()) {
    toast("Cap LoRa-1262 not detected. Attach it and try again.", col::BAD, 2000);
    return false;
  }
  if (radioMode == RM_MESH) {
    if (!confirmBox("Radio is listening to Meshtastic. Switch back to CapDeck chat?", col::LORA)) return false;
    cfg.meshListen = false;
    saveSettings();
    loraSetMode(RM_CAPDECK);
  }
  return true;
}

static void drawSignal(int x, int y, float rssi, uint16_t c) {
  int bars = rssi > -70 ? 4 : rssi > -90 ? 3 : rssi > -105 ? 2 : rssi > -118 ? 1 : 0;
  for (int i = 0; i < 4; i++) canvas.fillRect(x + i * 4, y + 8 - (i + 1) * 2, 3, (i + 1) * 2, i < bars ? c : col::FAINT);
}

static void chat() {
  if (!needCap()) return;
  String input;
  int scrollUp = 0;
  bool dirty = true;
  size_t seen = loraLog.size();
  uint32_t blink = 0;
  while (true) {
    Key k = readKey();
    loraUnread = 0;
    if (loraLog.size() != seen) { seen = loraLog.size(); dirty = true; scrollUp = 0; }
    if (k.any) dirty = true;
    if (k.esc || (k.del && !input.length())) return;
    if (k.fn && k.up) scrollUp++;
    if (k.fn && k.down) scrollUp = max(0, scrollUp - 1);
    if (k.del && input.length()) input.remove(input.length() - 1);
    else if (k.enter && input.length()) {
      if (!loraSend(cfg.nick + ": " + input)) toast("Send failed", col::BAD, 900);
      else beep(2600, 15);
      input = "";
      scrollUp = 0;
    } else if (k.space) input += ' ';
    else if (k.ch && !k.fn && input.length() < 180) input += k.ch;
    if (millis() - blink > 450) { blink = millis(); dirty = true; }
    if (!dirty) { delay(8); continue; }
    dirty = false;

    drawHeader("LoRa chat", col::LORA, ICON_LORA);
    canvas.setFont(&fonts::Font0);
    canvas.setTextDatum(middle_right);
    canvas.setTextColor(col::DIM);
    canvas.drawString(String(cfg.loraFreq, 1) + "MHz SF" + String(cfg.loraSf), SCR_W - 6, BAR_H + 9);
    canvas.setFont(&fonts::Font2);
    struct L { String s; bool me; float rssi; bool first; };
    std::vector<L> lines;
    for (auto &m : loraLog) {
      std::vector<String> w;
      wrapText(m.text, SCR_W - 44, w);
      for (size_t i = 0; i < w.size(); i++) lines.push_back({w[i], m.mine, m.rssi, i == 0});
      lines.push_back({"", m.mine, 0, false});
    }
    int lh = 15, y = SCR_H - 22 - lh;
    for (int i = (int)lines.size() - 1 - scrollUp; i >= 0 && y >= BAR_H + 18; i--, y -= lh) {
      auto &l = lines[i];
      if (!l.s.length()) { y += lh / 2; continue; }
      int w = canvas.textWidth(l.s);
      canvas.setTextDatum(top_left);
      if (l.me) {
        canvas.fillRoundRect(SCR_W - w - 14, y, w + 10, lh, 4, col::LORA);
        canvas.setTextColor(col::TEXT);
        canvas.drawString(l.s, SCR_W - w - 9, y);
      } else {
        canvas.fillRoundRect(4, y, w + 10, lh, 4, col::PANEL2);
        canvas.setTextColor(col::TEXT);
        canvas.drawString(l.s, 9, y);
        if (l.first) drawSignal(w + 18, y + 3, l.rssi, col::LORA);
      }
    }
    if (loraLog.empty()) {
      canvas.setTextDatum(middle_center);
      canvas.setTextColor(col::DIM);
      canvas.drawString("No messages yet", SCR_W / 2, 70);
    }
    canvas.fillRect(0, SCR_H - 20, SCR_W, 20, col::PANEL);
    canvas.drawFastHLine(0, SCR_H - 20, SCR_W, col::LORA);
    canvas.setTextDatum(middle_left);
    canvas.setTextColor(input.length() ? col::TEXT : col::DIM);
    String shown = input.length() ? input : String("message, ENTER sends");
    while (canvas.textWidth(shown) > SCR_W - 14) shown.remove(0, 1);
    canvas.drawString(shown, 5, SCR_H - 10);
    if (input.length() && (millis() / 450) % 2) canvas.fillRect(6 + canvas.textWidth(shown), SCR_H - 17, 2, 14, col::LORA);
    pushFrame();
  }
}

static void monitor() {
  if (!needCap()) return;
  size_t seen = loraLog.size();
  uint32_t lastPkt = 0;
  float lastRssi = -140, lastSnr = 0;
  int count = 0;
  bool dirty = true;
  uint32_t lastDraw = 0;
  while (true) {
    Key k = readKey();
    loraUnread = 0;
    if (k.back) return;
    if (loraLog.size() != seen) {
      seen = loraLog.size();
      auto &m = loraLog.back();
      if (!m.mine) { lastPkt = millis(); lastRssi = m.rssi; lastSnr = m.snr; count++; }
      dirty = true;
    }
    if (!dirty && millis() - lastDraw < 500) { delay(8); continue; }
    dirty = false;
    lastDraw = millis();
    drawHeader("LoRa monitor", col::LORA, ICON_LORA);
    // signal meter
    canvas.fillRoundRect(4, 36, 92, 94, 6, col::PANEL);
    int pct = constrain((int)((lastRssi + 130) * 100 / 90), 0, 100);
    canvas.fillArc(50, 82, 34, 28, 135, 405, col::FAINT);
    if (count) canvas.fillArc(50, 82, 34, 28, 135, 135 + pct * 270 / 100, col::LORA);
    canvas.setFont(&fonts::FreeSansBold9pt7b);
    canvas.setTextDatum(middle_center);
    canvas.setTextColor(col::TEXT);
    canvas.drawString(count ? String((int)lastRssi) : String("--"), 50, 80);
    canvas.setFont(&fonts::Font0);
    canvas.setTextColor(col::DIM);
    canvas.drawString("dBm", 50, 95);
    canvas.drawString(count ? "SNR " + String(lastSnr, 1) : String("waiting"), 50, 120);
    // packet list
    canvas.setTextDatum(top_left);
    int y = 37;
    int shown = 0;
    for (int i = (int)loraLog.size() - 1; i >= 0 && shown < 9; i--) {
      auto &m = loraLog[i];
      if (m.mine) continue;
      String t = m.text;
      while (canvas.textWidth(t) > 130) t.remove(t.length() - 1);
      canvas.setTextColor(shown == 0 ? col::TEXT : col::DIM);
      canvas.drawString(t, 102, y);
      y += 10;
      shown++;
    }
    canvas.setTextDatum(bottom_right);
    canvas.setTextColor(col::FAINT);
    canvas.drawString(String(count) + " pkts" + (lastPkt ? "  " + String((millis() - lastPkt) / 1000) + "s ago" : ""), SCR_W - 4, SCR_H - 2);
    pushFrame();
  }
}

static void ping() {
  if (!needCap()) return;
  static int n = 0;
  String msg = "PING " + cfg.nick + " #" + String(++n);
  size_t before = loraLog.size();
  bool ok = loraSend(msg);
  if (!ok) { toast("Transmit failed", col::BAD); return; }
  uint32_t t = millis();
  int frame = 0;
  while (millis() - t < 6000) {
    Key k = readKey();
    if (k.back) return;
    for (size_t i = before; i < loraLog.size(); i++) {
      if (!loraLog[i].mine) {
        toast("Reply: " + loraLog[i].text + "\nRSSI " + String((int)loraLog[i].rssi) + " dBm  SNR " + String(loraLog[i].snr, 1), col::OK, 3000);
        return;
      }
    }
    drawHeader("Ping", col::LORA, ICON_LORA);
    drawIcon(canvas, ICON_LORA, SCR_W / 2, 72, 50, col::LORA);
    int r = 10 + (frame * 4) % 60;
    canvas.drawCircle(SCR_W / 2, 60, r, col::LORA);
    canvas.setFont(&fonts::Font2);
    canvas.setTextDatum(middle_center);
    canvas.setTextColor(col::TEXT);
    canvas.drawString("Sent \"" + msg + "\" - waiting", SCR_W / 2, 118);
    pushFrame();
    frame++;
    delay(40);
  }
  toast("No reply within 6 s", col::WARN, 1200);
}

static bool saveLoraToConfig() {
  if (!sdOK) return false;
  File f = SD.open("/capdeck/config.json", FILE_READ);
  if (!f) return false;
  JsonDocument doc;
  if (deserializeJson(doc, f)) { f.close(); return false; }
  f.close();
  JsonObject lo = doc["lora"].to<JsonObject>();
  lo["freq_mhz"] = cfg.loraFreq;
  lo["bw_khz"] = cfg.loraBw;
  lo["sf"] = cfg.loraSf;
  lo["cr"] = cfg.loraCr;
  lo["sync_word"] = cfg.loraSync;
  lo["power_dbm"] = cfg.loraPower;
  f = SD.open("/capdeck/config.json", FILE_WRITE);
  if (!f) return false;
  serializeJsonPretty(doc, f);
  f.close();
  return true;
}

static void radioSettings() {
  const float freqs[] = {915.0, 906.875, 903.9, 923.0};
  const float bws[] = {62.5, 125.0, 250.0, 500.0};
  int sel = 0;
  while (true) {
    std::vector<ListItem> items = {
        {"Frequency", String(cfg.loraFreq, 3) + " MHz", 255, 0},
        {"Bandwidth", String(cfg.loraBw, 1) + " kHz", 255, 0},
        {"Spreading factor", "SF" + String(cfg.loraSf), 255, 0},
        {"Coding rate", "4/" + String(cfg.loraCr), 255, 0},
        {"Sync word", "0x" + String(cfg.loraSync, HEX), 255, 0},
        {"TX power", String(cfg.loraPower) + " dBm", 255, 0},
        {"Nickname", cfg.nick, 255, 0},
        {"Save to config.json", "", ICON_FILES, col::FILES},
    };
    int r = listMenu("Radio settings", col::LORA, items, sel, ICON_CONF);
    if (r < 0) return;
    switch (r) {
      case 0: {
        int i = 0;
        while (i < 4 && fabsf(freqs[i] - cfg.loraFreq) > 0.01f) i++;
        if (i >= 3) {
          String v = String(cfg.loraFreq, 3);
          if (textInput("Frequency MHz (868-923)", v, col::LORA) && v.toFloat() >= 868 && v.toFloat() <= 923) cfg.loraFreq = v.toFloat();
          else cfg.loraFreq = freqs[0];
        } else cfg.loraFreq = freqs[i + 1];
        break;
      }
      case 1: {
        int i = 0;
        while (i < 4 && bws[i] != cfg.loraBw) i++;
        cfg.loraBw = bws[(i + 1) % 4];
        break;
      }
      case 2: cfg.loraSf = cfg.loraSf >= 12 ? 7 : cfg.loraSf + 1; break;
      case 3: cfg.loraCr = cfg.loraCr >= 8 ? 5 : cfg.loraCr + 1; break;
      case 4: cfg.loraSync = cfg.loraSync == 0x12 ? 0x34 : 0x12; break;
      case 5: cfg.loraPower = cfg.loraPower >= 22 ? 2 : cfg.loraPower + 3; break;
      case 6: textInput("Nickname", cfg.nick, col::LORA, false, 16); break;
      case 7: toast(saveLoraToConfig() ? "Saved" : "Couldn't write config.json", col::OK, 900); break;
    }
    loraApplyParams();
  }
}

static const char *LORA_HELP =
    "Plain point-to-point LoRa on the Cap LoRa-1262 (SX1262, 868-923 MHz). In the US "
    "stay in 902-928 MHz - 915.0 is a good default.\n\n"
    "Every CapDeck with the same frequency, bandwidth, SF, coding rate and sync word can "
    "chat. Messages arriving anywhere in CapDeck beep and show an envelope in the status bar. "
    "They are also logged to /capdeck/lora/log.txt.\n\n"
    "This is NOT Meshtastic - Meshtastic packets are encrypted and use their own framing.\n\n"
    "DX-LR32 / UART modules: they only talk to this if they're set to the same raw LoRa "
    "parameters and pass data through unchanged (transparent mode). Check the module's AT settings.\n\n"
    "Chat keys: type + ENTER, Fn+; / Fn+. scroll, DEL on empty line goes back.";

void appLora() {
  int sel = 0;
  while (true) {
    std::vector<ListItem> items = {
        {"Chat", loraUnread ? String(loraUnread) + " new" : "", ICON_LORA, 0},
        {"Monitor", "", ICON_DISPLAY, 0},
        {"Ping", "", ICON_WIFI, 0},
        {"Scanner", "spectrum + hunter", ICON_SCAN, 0},
        {"Radio settings", String(cfg.loraFreq, 1), ICON_CONF, 0},
        {"Help", !loraPresent ? "no cap" : radioMode == RM_MESH ? "mesh mode" : "cap ok", ICON_INFO, 0},
    };
    int r = listMenu("LoRa", col::LORA, items, sel, ICON_LORA);
    if (r < 0) return;
    switch (r) {
      case 0: chat(); break;
      case 1: monitor(); break;
      case 2: ping(); break;
      case 3: appScanner(); break;
      case 4: if (needCap()) radioSettings(); break;
      case 5: textViewer("LoRa help", LORA_HELP, col::LORA); break;
    }
  }
}
