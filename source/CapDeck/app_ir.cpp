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

// ------------------------------------------------------------------ recent remotes (NVS)
static std::vector<String> recents() {
  Preferences p;
  p.begin("capdeck", true);
  String all = p.getString("irrec", "");
  p.end();
  std::vector<String> v;
  int from = 0;
  while (from < (int)all.length()) {
    int nl = all.indexOf('\n', from);
    if (nl < 0) nl = all.length();
    if (nl > from) v.push_back(all.substring(from, nl));
    from = nl + 1;
  }
  return v;
}
static void pushRecent(const String &path) {
  auto v = recents();
  for (size_t i = 0; i < v.size(); i++)
    if (v[i] == path) { v.erase(v.begin() + i); break; }
  v.insert(v.begin(), path);
  if (v.size() > 6) v.resize(6);
  String all;
  for (auto &x : v) all += x + "\n";
  Preferences p;
  p.begin("capdeck", false);
  p.putString("irrec", all);
  p.end();
}
static String baseName(const String &path) {
  String n = path.substring(path.lastIndexOf('/') + 1);
  if (n.endsWith(".ir")) n.remove(n.length() - 3);
  n.replace("_", " ");
  return n;
}

// Non-interactive: send one named signal (or the first, if name is empty) from a .ir file.
// Used by the Macros engine. Returns "" on success or an error string.
String irSendSignal(const String &path, const String &name) {
  File f = SD.open(path, FILE_READ);
  if (!f) return "IR file not found: " + path;
  uint32_t offset = 0;
  bool found = false;
  while (f.available()) {
    uint32_t pos = f.position();
    String l = readLine(f);
    if (l.startsWith("name:")) {
      String n = l.substring(5);
      n.trim();
      if (!name.length() || n.equalsIgnoreCase(name)) { offset = pos; found = true; break; }
    }
  }
  if (!found) { f.close(); return name.length() ? "No \"" + name + "\" in that remote" : "Empty .ir file"; }
  String err = sendAt(f, offset);
  f.close();
  return err;
}

void irOpenFile(const String &path) {
  File f = SD.open(path, FILE_READ);
  if (!f) { toast("Can't open " + path, col::BAD); return; }
  pushRecent(path);
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

// ------------------------------------------------------------------ search the SD library by name
static void walk(const String &dir, const std::vector<String> &words, std::vector<String> &out, int &seen, uint32_t &lastDraw) {
  File d = SD.open(dir);
  if (!d || !d.isDirectory()) return;
  std::vector<String> subdirs;
  for (File f = d.openNextFile(); f && out.size() < 150; f = d.openNextFile()) {
    String n = f.name();
    int sl = n.lastIndexOf('/');
    if (sl >= 0) n = n.substring(sl + 1);
    String full = (dir == "/" ? "" : dir) + "/" + n;
    if (f.isDirectory()) {
      if (!n.startsWith(".")) subdirs.push_back(full);
    } else if (n.endsWith(".ir")) {
      seen++;
      String hay = full;
      hay.toLowerCase();
      hay.replace("_", " ");
      bool ok = true;
      for (auto &w : words)
        if (hay.indexOf(w) < 0) { ok = false; break; }
      if (ok) out.push_back(full);
    }
    f.close();
    if (millis() - lastDraw > 250) {
      lastDraw = millis();
      drawHeader("Searching...", col::IR, ICON_IR);
      canvas.setFont(&fonts::Font2);
      canvas.setTextDatum(middle_center);
      canvas.setTextColor(col::TEXT);
      canvas.drawString(String(seen) + " remotes checked", SCR_W / 2, 66);
      canvas.setTextColor(col::IR);
      canvas.drawString(String(out.size()) + " matches", SCR_W / 2, 88);
      pushFrame();
      M5Cardputer.update();
    }
  }
  d.close();
  for (auto &sd : subdirs) walk(sd, words, out, seen, lastDraw);
}

static void searchRemotes() {
  static String q;
  if (!textInput("Brand or device (e.g. vizio tv)", q, col::IR, false, 40) || !q.length()) return;
  String ql = q;
  ql.toLowerCase();
  std::vector<String> words;
  int from = 0;
  while (from < (int)ql.length()) {
    int sp = ql.indexOf(' ', from);
    if (sp < 0) sp = ql.length();
    if (sp > from) words.push_back(ql.substring(from, sp));
    from = sp + 1;
  }
  std::vector<String> out;
  int seen = 0;
  uint32_t lastDraw = 0;
  walk("/capdeck/ir", words, out, seen, lastDraw);
  if (out.empty()) { toast("Nothing matched \"" + q + "\" in " + String(seen) + " remotes", col::WARN, 1800); return; }
  std::vector<ListItem> items;
  for (auto &p : out) {
    String parent = p.substring(0, p.lastIndexOf('/'));
    parent = parent.substring(parent.lastIndexOf('/') + 1);
    items.push_back({baseName(p), parent, 255, 0});
  }
  int sel = 0;
  while (true) {
    int r = listMenu((String(out.size()) + " matches").c_str(), col::IR, items, sel, ICON_IR);
    if (r < 0) return;
    irOpenFile(out[r]);
  }
}

static const char *TV_OFF = "/capdeck/ir/Flipper-IRDB/Universal_TV_Remotes/UNIVERSAL_POWER_OFF_DEVICES.ir";

static const char *IR_HELP =
    "The CapDeck SD kit ships the whole Flipper-IRDB library (about 9,000 remotes, CC0) in "
    "/capdeck/ir/Flipper-IRDB. SEARCH finds a remote by brand or device. Add your own .ir files "
    "anywhere under /capdeck/ir (from the Portal's Files tab too).\n\n"
    "Supported: raw signals plus NEC, NECext, Samsung32, SIRC/15/20, RC5/RC5X, RC6.\n\n"
    "Point the top edge of the Cardputer at the device. The built-in IR LED only "
    "transmits - the ADV has no IR receiver, so learning remotes needs an extra receiver module.\n\n"
    "Keys: ; . move   ENTER send   DEL back   1-9 jump";

void appIR() {
  int sel = 0;
  while (true) {
    auto rec = recents();
    std::vector<ListItem> items = {{"Search remotes", "brand / device", ICON_SCAN, col::IR},
                                   {"Browse library", "/capdeck/ir", ICON_FILES, col::FILES},
                                   {"TV power-off sweep", SD.exists(TV_OFF) ? "universal" : "needs SD kit", ICON_POWER, col::WARN}};
    for (auto &r : rec) items.push_back({baseName(r), "recent", ICON_IR, 0});
    items.push_back({"Browse whole SD", "/", ICON_FILES, col::FILES});
    items.push_back({"Test blink (NEC)", "", ICON_IR, 0});
    items.push_back({"Help", "", ICON_INFO, 0});
    int r = listMenu("Infrared", col::IR, items, sel, ICON_IR);
    if (r < 0) return;
    int nRec = rec.size();
    if (r == 0) searchRemotes();
    else if (r == 1) {
      String p = pickFile("Pick a remote", "/capdeck/ir", "ir", col::IR, ICON_IR);
      if (p.length()) irOpenFile(p);
    } else if (r == 2) {
      if (SD.exists(TV_OFF)) irOpenFile(TV_OFF);
      else toast("Copy the CapDeck SD kit to the card to get the remote library", col::WARN, 2000);
    } else if (r < 3 + nRec) {
      irOpenFile(rec[r - 3]);
    } else if (r == 3 + nRec) {
      String p = pickFile("Pick a .ir file", "/", "ir", col::IR, ICON_IR);
      if (p.length()) irOpenFile(p);
    } else if (r == 4 + nRec) {
      irBegin();
      for (int i = 0; i < 6; i++) drawBlast("NEC 0x00 / 0x00", i, col::IR);
      irsend.sendNEC(irsend.encodeNEC(0x00, 0x00), 32);
      toast("Sent. Look at the IR LED through a phone camera - it should flash.", col::IR, 2500);
    } else {
      textViewer("Infrared help", IR_HELP, col::IR);
    }
  }
}
