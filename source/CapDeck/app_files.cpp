// CapDeck - Files: SD card browser / manager, plus a reusable file picker
// The device's party trick: view any text or image file, and edit .json configs in place (with a
// validity check before it saves), so Home Assistant / CapDeck settings can be tuned on the go.
#include "core.h"
#include <ArduinoJson.h>
#include <Update.h>
#include <esp_ota_ops.h>
#include <algorithm>

struct Entry {
  String name;
  bool dir;
  uint64_t size;
};

static String joinPath(const String &dir, const String &name) {
  if (dir == "/") return "/" + name;
  return dir + "/" + name;
}

static String parentOf(const String &p) {
  int i = p.lastIndexOf('/');
  if (i <= 0) return "/";
  return p.substring(0, i);
}

static String extOf(const String &n) {
  int i = n.lastIndexOf('.');
  if (i < 0) return "";
  String e = n.substring(i + 1);
  e.toLowerCase();
  return e;
}

static uint8_t iconForFile(const String &name) {
  String e = extOf(name);
  if (e == "ir") return ICON_IR;
  if (e == "sub" || e == "rf") return ICON_RF;
  if (e == "gpx" || e == "nmea") return ICON_GPS;
  if (e == "bin") return ICON_POWER;
  if (e == "txt" || e == "json" || e == "md" || e == "csv" || e == "log") return ICON_INFO;
  return 255;
}

static bool listDir(const String &path, std::vector<Entry> &out, const char *extFilter = nullptr) {
  out.clear();
  File d = SD.open(path);
  if (!d || !d.isDirectory()) return false;
  while (true) {
    File f = d.openNextFile();
    if (!f) break;
    String n = f.name();
    int s = n.lastIndexOf('/');
    if (s >= 0) n = n.substring(s + 1);
    if (n.startsWith(".")) { f.close(); continue; }  // hide dot files
    bool isDir = f.isDirectory();
    if (!isDir && extFilter && extOf(n) != extFilter) { f.close(); continue; }
    out.push_back({n, isDir, isDir ? 0 : (uint64_t)f.size()});
    f.close();
    if (out.size() >= 300) break;
  }
  d.close();
  std::sort(out.begin(), out.end(), [](const Entry &a, const Entry &b) {
    if (a.dir != b.dir) return a.dir;
    String x = a.name, y = b.name;
    x.toLowerCase();
    y.toLowerCase();
    return x < y;
  });
  return true;
}

static bool removeRecursive(const String &path) {
  File f = SD.open(path);
  if (!f) return false;
  if (!f.isDirectory()) {
    f.close();
    return SD.remove(path);
  }
  std::vector<String> children;
  while (true) {
    File c = f.openNextFile();
    if (!c) break;
    String n = c.name();
    int s = n.lastIndexOf('/');
    if (s >= 0) n = n.substring(s + 1);
    children.push_back(joinPath(path, n));
    c.close();
  }
  f.close();
  for (auto &c : children) removeRecursive(c);
  return SD.rmdir(path);
}

static String shortPath(const String &p, int maxChars = 22) {
  if ((int)p.length() <= maxChars) return p;
  return "..." + p.substring(p.length() - maxChars + 3);
}

static void viewFile(const String &path) {
  File f = SD.open(path, FILE_READ);
  if (!f) { toast("Can't open file", col::BAD); return; }
  const size_t cap = 12 * 1024;
  String text;
  text.reserve(min((size_t)f.size(), cap) + 64);
  int nonText = 0;
  size_t n = 0;
  uint8_t head[512];
  size_t headLen = f.read(head, sizeof head);
  for (size_t i = 0; i < headLen; i++)
    if (head[i] < 9 || (head[i] > 13 && head[i] < 32)) nonText++;
  if (headLen && nonText > (int)headLen / 10) {
    // binary: hex dump of the first 512 bytes
    char line[64];
    for (size_t i = 0; i < headLen; i += 8) {
      int o = snprintf(line, sizeof line, "%04X ", (unsigned)i);
      for (size_t j = 0; j < 8; j++) o += snprintf(line + o, sizeof line - o, i + j < headLen ? "%02X " : "   ", head[i + j]);
      for (size_t j = 0; j < 8 && i + j < headLen; j++) line[o++] = isprint(head[i + j]) ? head[i + j] : '.';
      line[o] = 0;
      text += line;
      text += '\n';
    }
  } else {
    for (size_t i = 0; i < headLen; i++) text += (char)head[i];
    n = headLen;
    while (f.available() && n < cap) {
      text += (char)f.read();
      n++;
    }
    if (f.available()) text += "\n\n[... truncated at 12 KB ...]";
  }
  f.close();
  int s = path.lastIndexOf('/');
  textViewer(path.substring(s + 1).c_str(), text, col::FILES);
}

static bool isImageExt(const String &e) { return e == "png" || e == "jpg" || e == "jpeg" || e == "bmp" || e == "qoi"; }

// Full-screen image viewer: fits to the screen keeping aspect ratio. Any key leaves.
static void viewImage(const String &path) {
  auto &d = M5Cardputer.Display;
  d.fillScreen(TFT_BLACK);
  String e = extOf(path);
  bool ok;
  if (e == "png") ok = d.drawPngFile(SD, path.c_str(), 0, 0, SCR_W, SCR_H, 0, 0, 0.0f, 0.0f, datum_t::middle_center);
  else if (e == "bmp") ok = d.drawBmpFile(SD, path.c_str(), 0, 0, SCR_W, SCR_H, 0, 0, 0.0f, 0.0f, datum_t::middle_center);
  else if (e == "qoi") ok = d.drawQoiFile(SD, path.c_str(), 0, 0, SCR_W, SCR_H, 0, 0, 0.0f, 0.0f, datum_t::middle_center);
  else ok = d.drawJpgFile(SD, path.c_str(), 0, 0, SCR_W, SCR_H, 0, 0, 0.0f, 0.0f, datum_t::middle_center);
  if (!ok) { toast("Couldn't decode that image", col::WARN, 1400); return; }
  d.setFont(&fonts::Font0);
  d.setTextDatum(bottom_left);
  String nm = path.substring(path.lastIndexOf('/') + 1);
  int w = d.textWidth(nm) + 6;
  d.fillRect(0, SCR_H - 11, w, 11, TFT_BLACK);
  d.setTextColor(TFT_WHITE);
  d.drawString(nm, 3, SCR_H - 1);
  while (true) {
    Key k = readKey();
    if (k.any) break;
    delay(8);
  }
}

// Line-based JSON editor. Edit/add/delete lines, then it must parse as JSON before it saves -
// so a stray comma can't brick a config.
static void editJson(const String &path) {
  std::vector<String> lines;
  {
    File f = SD.open(path, FILE_READ);
    if (!f) { toast("Can't open file", col::BAD); return; }
    if (f.size() > 24 * 1024) { f.close(); toast("Too big to edit here (24 KB max)", col::WARN, 1600); return; }
    String cur;
    while (f.available()) {
      char c = f.read();
      if (c == '\r') continue;
      if (c == '\n') { lines.push_back(cur); cur = ""; } else cur += c;
    }
    if (cur.length() || lines.empty()) lines.push_back(cur);
    f.close();
  }
  bool dirty = false;
  int sel = 0;
  String name = path.substring(path.lastIndexOf('/') + 1);
  while (true) {
    std::vector<ListItem> items;
    for (size_t i = 0; i < lines.size(); i++) {
      String shown = lines[i].length() ? lines[i] : String("(empty)");
      shown.trim();
      items.push_back({String((int)i + 1) + "  " + shown, "", 255, lines[i].length() ? (uint16_t)0 : col::FAINT});
    }
    items.push_back({"+ add a line", "", ICON_CONF, col::OK});
    items.push_back({dirty ? "Save changes" : "Save (no changes)", "", ICON_FILES, dirty ? col::OK : col::DIM});
    int r = listMenu((name + (dirty ? " *" : "")).c_str(), col::FILES, items, sel, ICON_FILES);
    if (r < 0) {
      if (!dirty || confirmBox("Discard unsaved edits?", col::WARN)) return;
      continue;
    }
    if (r == (int)lines.size()) {  // add
      String nl;
      if (textInput("New line", nl, col::FILES, false, 200)) { lines.insert(lines.begin() + min((int)lines.size(), sel), nl); dirty = true; }
      continue;
    }
    if (r == (int)lines.size() + 1) {  // save
      if (!dirty) { toast("Nothing changed", col::DIM, 700); continue; }
      String joined;
      for (size_t i = 0; i < lines.size(); i++) { joined += lines[i]; if (i + 1 < lines.size()) joined += "\n"; }
      JsonDocument doc;
      DeserializationError err = deserializeJson(doc, joined);
      if (err) {
        toast(String("Not valid JSON: ") + err.c_str() + ". Fix it before saving.", col::BAD, 2600);
        continue;
      }
      File f = SD.open(path, FILE_WRITE);
      if (!f) { toast("Couldn't write the file", col::BAD, 1400); continue; }
      f.print(joined);
      f.close();
      toast("Saved. JSON checks out.", col::OK, 1200);
      dirty = false;
      continue;
    }
    // edit/delete one line
    std::vector<ListItem> la = {{"Edit text", "", ICON_CONF, 0}, {"Delete line", "", ICON_POWER, col::BAD}};
    int a2 = 0;
    int a = listMenu(("Line " + String(r + 1)).c_str(), col::FILES, la, a2, ICON_FILES);
    if (a == 0) {
      String v = lines[r];
      if (textInput("Edit line", v, col::FILES, false, 200) && v != lines[r]) { lines[r] = v; dirty = true; }
    } else if (a == 1 && lines.size() > 1 && confirmBox("Delete line " + String(r + 1) + "?", col::BAD)) {
      lines.erase(lines.begin() + r);
      dirty = true;
      if (sel >= (int)lines.size()) sel = lines.size() - 1;
    }
  }
}

// Launch another firmware (.bin) straight from the SD card: it's written into the spare app slot
// and booted. Needs a flash layout with two app slots (M5Launcher-style); a full-flash CapDeck has
// only one, so then we say how to do it from M5Launcher instead.
static void launchBin(const String &path, uint64_t size) {
  File f = SD.open(path, FILE_READ);
  if (!f) { toast("Can't open file", col::BAD); return; }
  int magic = f.read();
  if (magic != 0xE9) { f.close(); toast("That isn't an ESP32 app image (.bin from a firmware release)", col::WARN, 2200); return; }
  const esp_partition_t *slot = esp_ota_get_next_update_partition(nullptr);
  if (!slot) {
    f.close();
    textViewer("Launch firmware",
               "There's no spare app slot on this flash layout (CapDeck was flashed as a full image), so it can't "
               "start another firmware by itself.\n\nTo run it: Config > Back to M5Launcher, then install the .bin "
               "from the SD card there. If you flashed CapDeck at 0x0, install M5Launcher first.",
               col::FILES);
    return;
  }
  if (size > slot->size) { f.close(); toast("Too big for the app slot (" + humanSize(slot->size) + ")", col::WARN, 2000); return; }
  String nm = path.substring(path.lastIndexOf('/') + 1);
  if (!confirmBox("Start " + nm + "? CapDeck stays installed; get back via M5Launcher.", col::WARN)) { f.close(); return; }
  f.seek(0);
  if (!Update.begin(size, U_FLASH)) { f.close(); toast(String("Couldn't start: ") + Update.errorString(), col::BAD, 2000); return; }
  uint8_t buf[2048];
  size_t done = 0;
  uint32_t lastUi = 0;
  while (f.available()) {
    size_t n = f.read(buf, sizeof buf);
    if (!n || Update.write(buf, n) != n) break;
    done += n;
    if (millis() - lastUi > 150) {
      lastUi = millis();
      drawHeader("Launching", col::FILES, ICON_FILES);
      canvas.setFont(&fonts::Font2); canvas.setTextDatum(middle_center); canvas.setTextColor(col::TEXT);
      canvas.drawString(nm, SCR_W / 2, 60);
      int w = SCR_W - 40;
      canvas.drawRect(20, 80, w, 12, col::DIM);
      canvas.fillRect(22, 82, (int)((w - 4) * (float)done / size), 8, col::FILES);
      canvas.setFont(&fonts::Font0); canvas.setTextColor(col::DIM);
      canvas.drawString("writing " + humanSize(done) + " of " + humanSize(size) + "  -  don't power off", SCR_W / 2, 104);
      pushFrame();
    }
  }
  f.close();
  if (done != size || !Update.end(true)) {  // end(true) also makes it the boot app
    Update.abort();
    toast(String("Write failed: ") + Update.errorString() + ". CapDeck is untouched.", col::BAD, 2600);
    return;
  }
  toast("Starting " + nm, col::OK, 900);
  ESP.restart();
}

static void fileActions(const String &path, uint64_t size) {
  String name = path.substring(path.lastIndexOf('/') + 1);
  String e = extOf(name);
  std::vector<ListItem> acts;
  if (e == "ir") acts.push_back({"Open in Infrared", "", ICON_IR, col::IR});
  if (isImageExt(e)) acts.push_back({"View image", "", ICON_INFO, col::PORTAL});
  if (e == "json") acts.push_back({"Edit (JSON)", "validated", ICON_CONF, col::OK});
  if (e == "bin") acts.push_back({"Launch firmware", "", ICON_POWER, col::WARN});
  acts.push_back({"View", "", ICON_INFO, 0});
  acts.push_back({"Rename", "", ICON_CONF, 0});
  acts.push_back({"Delete", "", ICON_POWER, col::BAD});
  acts.push_back({"Info", humanSize(size), ICON_INFO, 0});
  int sel = 0;
  int r = listMenu(name.c_str(), col::FILES, acts, sel, ICON_FILES);
  if (r < 0) return;
  String a = acts[r].label;
  if (a == "Open in Infrared") irOpenFile(path);
  else if (a == "View image") viewImage(path);
  else if (a == "Edit (JSON)") editJson(path);
  else if (a == "Launch firmware") launchBin(path, size);
  else if (a == "View") viewFile(path);
  else if (a == "Rename") {
    String nn = name;
    if (textInput("Rename to", nn, col::FILES) && nn.length() && nn != name) {
      String np = joinPath(parentOf(path), nn);
      toast(SD.rename(path, np) ? "Renamed" : "Rename failed", SD.exists(np) ? col::OK : col::BAD, 900);
    }
  } else if (a == "Delete") {
    if (confirmBox("Delete " + name + "?", col::BAD)) toast(SD.remove(path) ? "Deleted" : "Delete failed", col::WARN, 900);
  } else if (a == "Info") {
    textViewer("File info", "Name: " + name + "\nPath: " + path + "\nSize: " + humanSize(size) + " (" + String((uint32_t)size) + " bytes)", col::FILES);
  }
}

static void folderActions(String &dir) {
  std::vector<ListItem> acts = {{"New folder here", "", ICON_FILES, 0},
                                {"Rename this folder", "", ICON_CONF, 0},
                                {"Delete this folder", "", ICON_POWER, col::BAD},
                                {"SD card usage", "", ICON_INFO, 0}};
  int sel = 0;
  int r = listMenu("Folder actions", col::FILES, acts, sel, ICON_FILES);
  if (r == 0) {
    String n;
    if (textInput("New folder name", n, col::FILES) && n.length()) toast(SD.mkdir(joinPath(dir, n)) ? "Created" : "Failed", col::OK, 800);
  } else if (r == 1 && dir != "/") {
    String name = dir.substring(dir.lastIndexOf('/') + 1), nn = name;
    if (textInput("Rename folder", nn, col::FILES) && nn.length() && nn != name) {
      String np = joinPath(parentOf(dir), nn);
      if (SD.rename(dir, np)) dir = np;
      else toast("Rename failed", col::BAD);
    }
  } else if (r == 2 && dir != "/") {
    if (confirmBox("Delete " + dir + " and everything inside?", col::BAD)) {
      if (removeRecursive(dir)) dir = parentOf(dir);
      else toast("Delete failed", col::BAD);
    }
  } else if (r == 3) {
    uint64_t total = SD.totalBytes(), used = SD.usedBytes();
    toast("Used " + humanSize(used) + " of " + humanSize(total) + "\n(" + String(total ? (int)(used * 100 / total) : 0) + "% full)", col::FILES, 2500);
  }
}

// Generic browser. If `pickExt` is set, works as a picker and returns the chosen file path.
static String browse(const char *startDir, const char *pickExt, const char *pickTitle, uint16_t color, uint8_t icon) {
  if (!sdOK) {
    toast("No microSD card found", col::BAD);
    return "";
  }
  String dir = startDir;
  if (!SD.exists(dir)) dir = "/";
  std::vector<int> selStack;
  int sel = 0;
  std::vector<Entry> entries;
  while (true) {
    if (!listDir(dir, entries, pickExt)) {
      toast("Can't read " + dir, col::BAD);
      dir = "/";
      continue;
    }
    std::vector<ListItem> items;
    bool root = dir == "/";
    if (!root) items.push_back({"..", "up", ICON_FILES, col::DIM});
    for (auto &e : entries) {
      ListItem it;
      it.label = e.name;
      if (e.dir) {
        it.icon = ICON_FILES;
        it.color = col::FILES;
        it.right = ">";
      } else {
        it.icon = iconForFile(e.name);
        it.right = humanSize(e.size);
      }
      items.push_back(it);
    }
    if (!pickExt) items.push_back({"Folder actions...", "", ICON_CONF, col::CONF});
    String title = pickExt ? String(pickTitle) : shortPath(dir);
    int r = listMenu(title.c_str(), color, items, sel, icon);
    if (r < 0) {
      if (root) return "";
      dir = parentOf(dir);
      sel = selStack.empty() ? 0 : selStack.back();
      if (!selStack.empty()) selStack.pop_back();
      continue;
    }
    int off = root ? 0 : 1;
    if (!root && r == 0) {
      dir = parentOf(dir);
      sel = selStack.empty() ? 0 : selStack.back();
      if (!selStack.empty()) selStack.pop_back();
      continue;
    }
    if (!pickExt && r == (int)items.size() - 1) {
      folderActions(dir);
      continue;
    }
    Entry &e = entries[r - off];
    String full = joinPath(dir, e.name);
    if (e.dir) {
      selStack.push_back(r);
      dir = full;
      sel = 0;
      continue;
    }
    if (pickExt) return full;
    fileActions(full, e.size);
  }
}

void appFiles(const char *startDir) { browse(startDir, nullptr, nullptr, col::FILES, ICON_FILES); }

String pickFile(const char *title, const char *startDir, const char *ext, uint16_t color, uint8_t icon) {
  return browse(startDir, ext, title, color, icon);
}
