// CapDeck - Infrared: send signals from Flipper Zero .ir files on the SD card
#include "core.h"
#include <IRremoteESP8266.h>
#include <IRsend.h>
#include <IRutils.h>

static IRsend irsend(PIN_IR_TX);
static bool irReady = false;

struct IrIndex {
  String name;
  uint32_t offset;  // file offset of the "name:" line
};

static void irBegin() {
  if (!irReady) {
    irsend.begin();
    irReady = true;
  }
}

static String readLine(File &f) {
  String s = f.readStringUntil('\n');
  s.trim();
  return s;
}

static uint32_t hexBytesLE(const String &v, uint8_t *bytes) {  // "07 F8 00 00" -> bytes[]
  int bi = 0;
  for (int i = 0; i < (int)v.length() && bi < 4;) {
    while (i < (int)v.length() && v[i] == ' ') i++;
    if (i >= (int)v.length()) break;
    bytes[bi++] = strtoul(v.substring(i, i + 2).c_str(), nullptr, 16);
    i += 2;
  }
  return bytes[0] | (bytes[1] << 8) | (bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
}

static uint64_t packLsbFrame(const uint8_t *frame, int n) {
  uint64_t d = 0;
  for (int i = 0; i < n; i++) d = (d << 8) | reverseBits(frame[i], 8);
  return d;
}

// Sends the signal whose "name:" line starts at `offset`. Returns "" or an error.
static String sendAt(File &f, uint32_t offset) {
  f.seek(offset);
  String name = readLine(f);
  String type, protocol, addr, cmd, data;
  uint32_t freq = 38000;
  std::vector<uint16_t> raw;
  while (f.available()) {
    uint32_t pos = f.position();
    String l = readLine(f);
    if (l.startsWith("name:") || l.startsWith("#")) { f.seek(pos); break; }
    int c = l.indexOf(':');
    if (c < 0) continue;
    String k = l.substring(0, c), v = l.substring(c + 1);
    v.trim();
    if (k == "type") type = v;
    else if (k == "protocol") protocol = v;
    else if (k == "address") addr = v;
    else if (k == "command") cmd = v;
    else if (k == "frequency") freq = v.toInt();
    else if (k == "data") data = v;
  }
  irBegin();
  if (type == "raw") {
    raw.reserve(256);
    const char *p = data.c_str();
    while (*p && raw.size() < 1024) {
      while (*p == ' ') p++;
      if (!*p) break;
      long val = strtol(p, (char **)&p, 10);
      raw.push_back((uint16_t)constrain(val, 1, 65535));
    }
    if (raw.empty()) return "empty raw data";
    irsend.sendRaw(raw.data(), raw.size(), freq / 1000);
    return "";
  }
  uint8_t a[4] = {0}, c[4] = {0};
  uint32_t A = hexBytesLE(addr, a), C = hexBytesLE(cmd, c);
  if (protocol == "NEC") {
    uint8_t fr[4] = {a[0], (uint8_t)~a[0], c[0], (uint8_t)~c[0]};
    irsend.sendNEC(packLsbFrame(fr, 4), 32);
  } else if (protocol == "NECext") {
    uint8_t fr[4] = {a[0], a[1], c[0], c[1]};
    irsend.sendNEC(packLsbFrame(fr, 4), 32);
  } else if (protocol == "Samsung32") {
    irsend.sendSAMSUNG(irsend.encodeSAMSUNG(a[0], c[0]), 32);
  } else if (protocol == "SIRC") {
    irsend.sendSony(irsend.encodeSony(12, C & 0x7F, A & 0x1F), 12, 2);
  } else if (protocol == "SIRC15") {
    irsend.sendSony(irsend.encodeSony(15, C & 0x7F, A & 0xFF), 15, 2);
  } else if (protocol == "SIRC20") {
    irsend.sendSony(irsend.encodeSony(20, C & 0x7F, A & 0x1F, (A >> 5) & 0xFF), 20, 2);
  } else if (protocol == "RC5") {
    irsend.sendRC5(irsend.encodeRC5(A & 0x1F, C & 0x3F), 13);
  } else if (protocol == "RC5X") {
    irsend.sendRC5(irsend.encodeRC5X(A & 0x1F, C & 0x7F), 13);
  } else if (protocol == "RC6") {
    irsend.sendRC6(irsend.encodeRC6(A & 0xFF, C & 0xFF), 20);
  } else {
    return "protocol '" + protocol + "' not supported";
  }
  return "";
}

static void drawBlast(const String &label, int frame, uint16_t c) {
  drawHeader("Infrared", col::IR, ICON_IR);
  drawIcon(canvas, ICON_IR, SCR_W / 2, 86, 56, col::IR);
  for (int i = 0; i < 3; i++) {
    int r = 14 + ((frame * 6 + i * 12) % 36);
    canvas.drawArc(SCR_W / 2, 66, r, r - 2, 235, 305, c);
  }
  canvas.setFont(&fonts::Font2);
  canvas.setTextDatum(middle_center);
  canvas.setTextColor(col::TEXT);
  canvas.drawString(label, SCR_W / 2, 124);
  pushFrame();
}

void irOpenFile(const String &path) {
  File f = SD.open(path, FILE_READ);
  if (!f) { toast("Can't open " + path, col::BAD); return; }
  std::vector<IrIndex> idx;
  drawHeader("Reading remote...", col::IR, ICON_IR);
  pushFrame();
  while (f.available() && idx.size() < 600) {
    uint32_t pos = f.position();
    String l = readLine(f);
    if (l.startsWith("name:")) {
      String n = l.substring(5);
      n.trim();
      idx.push_back({n, pos});
    }
  }
  if (idx.empty()) {
    f.close();
    toast("No signals in this file", col::WARN);
    return;
  }
  std::vector<ListItem> items;
  items.push_back({"Send ALL (sweep)", String(idx.size()), ICON_IR, col::WARN});
  for (auto &s : idx) items.push_back({s.name, "", 255, 0});
  String title = path.substring(path.lastIndexOf('/') + 1);
  title.replace(".ir", "");
  int sel = 1;
  while (true) {
    int r = listMenu(title.c_str(), col::IR, items, sel, ICON_IR);
    if (r < 0) break;
    if (r == 0) {
      // sweep through every signal; DEL stops
      for (size_t i = 0; i < idx.size(); i++) {
        Key k = readKey();
        if (k.back) break;
        drawBlast(String(i + 1) + "/" + String(idx.size()) + "  " + idx[i].name, i, col::IR);
        sendAt(f, idx[i].offset);
        delay(140);
      }
      toast("Sweep done", col::OK, 700);
      continue;
    }
    for (int fr = 0; fr < 3; fr++) drawBlast(idx[r - 1].name, fr, col::IR);
    String err = sendAt(f, idx[r - 1].offset);
    if (err.length()) toast(err, col::BAD, 1500);
    else {
      beep(3200, 15);
      for (int fr = 3; fr < 6; fr++) { drawBlast(idx[r - 1].name, fr, col::IR); delay(25); }
    }
  }
  f.close();
}

static const char *IR_HELP =
    "Put Flipper Zero .ir files on the SD card in /capdeck/ir (sub-folders are fine). "
    "The Flipper-IRDB collection works as-is.\n\n"
    "Supported: raw signals plus NEC, NECext, Samsung32, SIRC/15/20, RC5/RC5X, RC6.\n\n"
    "Point the top edge of the Cardputer at the device. The built-in IR LED only "
    "transmits - the ADV has no IR receiver, so learning remotes needs an extra receiver module.\n\n"
    "Keys: ; . move   ENTER send   DEL back   1-9 jump";

void appIR() {
  std::vector<ListItem> items = {{"Browse remotes", "/capdeck/ir", ICON_FILES, col::FILES},
                                 {"Browse whole SD", "/", ICON_FILES, col::FILES},
                                 {"Test blink (NEC)", "", ICON_IR, 0},
                                 {"Help", "", ICON_INFO, 0}};
  int sel = 0;
  while (true) {
    int r = listMenu("Infrared", col::IR, items, sel, ICON_IR);
    if (r < 0) return;
    if (r == 0 || r == 1) {
      String p = pickFile("Pick a .ir file", r == 0 ? "/capdeck/ir" : "/", "ir", col::IR, ICON_IR);
      if (p.length()) irOpenFile(p);
    } else if (r == 2) {
      irBegin();
      for (int i = 0; i < 6; i++) drawBlast("NEC 0x00 / 0x00", i, col::IR);
      irsend.sendNEC(irsend.encodeNEC(0x00, 0x00), 32);
      toast("Sent. Look at the IR LED through a phone camera - it should flash.", col::IR, 2500);
    } else {
      textViewer("Infrared help", IR_HELP, col::IR);
    }
  }
}
