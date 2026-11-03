// CapDeck - LoRa scanner: RSSI spectrum + waterfall, and a CAD "hunter" for known LoRa setups
#include "core.h"
#include <RadioLib.h>

static const float F0 = 902.0f, F1 = 928.0f;
static const int BINS = 104;  // 250 kHz bins
static float binFreq(int i) { return F0 + 0.125f + i * 0.25f; }

static uint16_t heat(int v) {  // 0..255 -> dark blue / theme accent / white
  uint16_t a = col::LORA;
  if (v < 40) return col::BG;
  if (v < 110) return col::FAINT;
  if (v < 170) return col::DIM;
  if (v < 220) return a;
  return col::TEXT;
}

static void spectrum() {
  SX1262 *r = loraRadio();
  loraSetMode(RM_SCAN);
  r->standby();
  r->setBandwidth(125.0);
  r->calibrateImage(915.0);
  static float cur[BINS], peak[BINS];
  static uint8_t fall[44][BINS];
  for (int i = 0; i < BINS; i++) { cur[i] = -140; peak[i] = -140; }
  memset(fall, 0, sizeof fall);
  int cursor = 19;  // 906.875 LongFast
  bool paused = false;
  int sweeps = 0;
  const int gx = 28, gy = BAR_H + 3, gh = 52, wy = gy + gh + 2, wh = 44;
  while (true) {
    Key k = readKey();
    if (k.back) break;
    if (k.left) cursor = max(0, cursor - (k.repeat ? 2 : 1));
    if (k.right) cursor = min(BINS - 1, cursor + (k.repeat ? 2 : 1));
    if (k.space || k.enter) paused = !paused;
    if (k.ch == 'r') for (int i = 0; i < BINS; i++) peak[i] = -140;
    if (!paused) {
      for (int i = 0; i < BINS; i++) {
        r->standby();
        r->setFrequency(binFreq(i), true);
        r->startReceive();
        delayMicroseconds(700);
        float v = r->getRSSI(false);
        float v2 = r->getRSSI(false);
        cur[i] = max(v, v2);
        if (cur[i] > peak[i]) peak[i] = cur[i];
        else peak[i] -= 0.4f;
        if ((i & 15) == 0) sysTick();
      }
      memmove(fall[1], fall[0], sizeof(fall) - sizeof(fall[0]));
      for (int i = 0; i < BINS; i++) fall[0][i] = (uint8_t)constrain((int)((cur[i] + 125) * 255 / 60), 0, 255);
      sweeps++;
    }
    canvas.fillScreen(col::BG);
    drawStatusBar(paused ? "SCAN - PAUSED" : "SPECTRUM 902-928", col::LORA);
    // axes
    canvas.setFont(&fonts::Font0);
    canvas.setTextColor(col::DIM);
    canvas.setTextDatum(middle_right);
    for (int db = -60; db >= -120; db -= 30) {
      int y = gy + (int)((-60 - db) * gh / 70.0f);
      canvas.drawFastHLine(gx, y, BINS * 2, col::PANEL);
      canvas.drawString(String(db), gx - 2, y);
    }
    // LongFast marker
    canvas.drawFastVLine(gx + 19 * 2, gy, gh, col::FAINT);
    for (int i = 0; i < BINS; i++) {
      int x = gx + i * 2;
      int h = constrain((int)((cur[i] + 130) * gh / 70.0f), 0, gh);
      int hp = constrain((int)((peak[i] + 130) * gh / 70.0f), 0, gh);
      canvas.fillRect(x, gy + gh - h, 2, h, i == cursor ? col::TEXT : col::LORA);
      canvas.drawFastHLine(x, gy + gh - hp, 2, col::WARN);
      for (int row = 0; row < wh; row++) canvas.fillRect(x, wy + row, 2, 1, heat(fall[row][i]));
    }
    canvas.drawFastVLine(gx + cursor * 2, wy, wh, col::TEXT);
    canvas.setTextDatum(top_left);
    canvas.setTextColor(col::TEXT);
    char rd[40];
    snprintf(rd, sizeof rd, "%.3f", binFreq(cursor));
    canvas.drawString(rd, 2, wy + 2);
    canvas.drawString("MHz", 2, wy + 11);
    snprintf(rd, sizeof rd, "%d", (int)cur[cursor]);
    canvas.setTextColor(col::LORA);
    canvas.drawString(rd, 2, wy + 24);
    snprintf(rd, sizeof rd, "%d", (int)peak[cursor]);
    canvas.setTextColor(col::WARN);
    canvas.drawString(rd, 2, wy + 33);
    canvas.setTextColor(col::DIM);
    canvas.setTextDatum(bottom_left);
    canvas.drawString("902", gx, SCR_H);
    canvas.setTextDatum(bottom_center);
    canvas.drawString(", / tune  SPACE hold  R reset", gx + BINS, SCR_H);
    canvas.setTextDatum(bottom_right);
    canvas.drawString("928", gx + BINS * 2, SCR_H);
    pushFrame();
    if (paused) delay(30);
  }
  loraSetMode(cfg.meshListen ? RM_MESH : RM_CAPDECK);
}

// ------------------------------------------------------------------ CAD hunter
struct Target {
  String name;
  float freqs[8];
  uint8_t nf;
  float bw;
  uint8_t sf;
  uint32_t hits = 0, lastHit = 0;
  float lastFreq = 0;
};

static void hunter() {
  SX1262 *r = loraRadio();
  std::vector<Target> t;
  for (int i = 0; i < MESH_PRESET_COUNT; i++) {
    Target x;
    x.name = String("Mesh ") + MESH_PRESETS[i].name;
    x.freqs[0] = MESH_PRESETS[i].freq;
    x.nf = 1;
    x.bw = MESH_PRESETS[i].bw;
    x.sf = MESH_PRESETS[i].sf;
    t.push_back(x);
  }
  for (int sf = 7; sf <= 10; sf++) {  // LoRaWAN US915 sub-band 2 (TTN / Helium uplinks)
    Target x;
    x.name = "LoRaWAN SF" + String(sf);
    x.nf = 8;
    for (int c = 0; c < 8; c++) x.freqs[c] = 903.9f + c * 0.2f;
    x.bw = 125;
    x.sf = sf;
    t.push_back(x);
  }
  {
    Target x;
    x.name = "CapDeck chat";
    x.freqs[0] = cfg.loraFreq;
    x.nf = 1;
    x.bw = cfg.loraBw;
    x.sf = cfg.loraSf;
    t.push_back(x);
  }
  loraSetMode(RM_SCAN);
  r->standby();
  r->calibrateImage(915.0);
  int scroll = 0, sel = 0;
  uint32_t rounds = 0, lastDraw = 0;
  size_t ti = 0;
  uint8_t fi = 0;
  float lastBw = 0;
  uint8_t lastSf = 0;
  while (true) {
    Key k = readKey();
    if (k.back) break;
    if (k.up) sel = max(0, sel - 1);
    if (k.down) sel = min((int)t.size() - 1, sel + 1);
    if (k.ch == 'r') for (auto &x : t) { x.hits = 0; x.lastHit = 0; }
    // one CAD per loop so the UI stays responsive
    Target &cur = t[ti];
    r->standby();
    if (cur.bw != lastBw) { r->setBandwidth(cur.bw); lastBw = cur.bw; }
    if (cur.sf != lastSf) { r->setSpreadingFactor(cur.sf); lastSf = cur.sf; }
    r->setFrequency(cur.freqs[fi], true);
    int st = r->scanChannel();
    if (st == RADIOLIB_LORA_DETECTED || st == RADIOLIB_PREAMBLE_DETECTED) {
      cur.hits++;
      cur.lastHit = millis();
      cur.lastFreq = cur.freqs[fi];
      beep(2900, 8);
    }
    if (++fi >= cur.nf) { fi = 0; ti = (ti + 1) % t.size(); if (!ti) rounds++; }
    if (millis() - lastDraw < 120) continue;
    lastDraw = millis();
    drawHeader("LoRa hunter", col::LORA, ICON_SCAN);
    canvas.setFont(&fonts::Font0);
    canvas.setTextDatum(middle_right);
    canvas.setTextColor(col::DIM);
    canvas.drawString("round " + String(rounds), SCR_W - 6, BAR_H + 9);
    const int top = BAR_H + 20, rh = 11, rows = (SCR_H - top - 9) / rh;
    if (sel < scroll) scroll = sel;
    if (sel >= scroll + rows) scroll = sel - rows + 1;
    for (int i = 0; i < rows && scroll + i < (int)t.size(); i++) {
      Target &x = t[scroll + i];
      int y = top + i * rh;
      bool hot = x.lastHit && millis() - x.lastHit < 1500;
      if (scroll + i == sel) canvas.fillRect(0, y - 1, SCR_W, rh, col::PANEL);
      canvas.fillCircle(6, y + 4, 3, hot ? col::OK : x.hits ? col::LORA : col::FAINT);
      canvas.setTextDatum(top_left);
      canvas.setTextColor(x.hits ? col::TEXT : col::DIM);
      canvas.drawString(x.name, 14, y);
      canvas.setTextColor(col::DIM);
      canvas.drawString(String(x.nf == 1 ? x.freqs[0] : x.freqs[0], 3) + (x.nf > 1 ? "+" : ""), 112, y);
      canvas.setTextDatum(top_right);
      canvas.setTextColor(hot ? col::OK : col::TEXT);
      String h = x.hits ? String(x.hits) + "  " + fmtAge(millis() - x.lastHit) : String("-");
      canvas.drawString(h, SCR_W - 4, y);
      if (&x == &t[ti]) canvas.drawFastHLine(14, y + 8, 40, col::LORA);
    }
    canvas.setTextDatum(bottom_center);
    canvas.setTextColor(col::FAINT);
    canvas.drawString("channel activity detection  ; . select  R reset", SCR_W / 2, SCR_H);
    pushFrame();
  }
  loraSetMode(cfg.meshListen ? RM_MESH : RM_CAPDECK);
}

static const char *SCAN_HELP =
    "SPECTRUM sweeps 902-928 MHz in 250 kHz steps and shows signal strength (bars), peak hold "
    "(yellow ticks) and a waterfall of the last ~40 sweeps. The faint vertical line is Meshtastic LongFast "
    "(906.875). Bursts that show up only in the waterfall are short packets.\n\n"
    "HUNTER cycles channel-activity-detection over common LoRa setups (Meshtastic presets on their US "
    "default slots, LoRaWAN US915 sub-band 2 used by TTN/Helium, and your CapDeck chat settings). A hit "
    "means a LoRa transmission with that bandwidth/spreading factor was on air right then.\n\n"
    "Both are receive-only. Scanning pauses Meshtastic listening and LoRa chat until you leave.";

void appScanner() {
  if (!loraPresent && !loraInit()) {
    toast("Cap LoRa-1262 not detected", col::BAD, 1500);
    return;
  }
  int sel = 0;
  while (true) {
    std::vector<ListItem> items = {{"Spectrum + waterfall", "902-928", ICON_SCAN, 0},
                                   {"LoRa hunter", "CAD", ICON_LORA, 0},
                                   {"Help", "", ICON_INFO, 0}};
    int r = listMenu("LoRa scanner", col::LORA, items, sel, ICON_SCAN);
    if (r < 0) return;
    if (r == 0) spectrum();
    else if (r == 1) hunter();
    else textViewer("Scanner help", SCAN_HELP, col::LORA);
  }
}
