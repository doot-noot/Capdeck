// CapDeck - Files: SD card browser / manager, plus a reusable file picker
#include "core.h"
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

static void fileActions(const String &path, uint64_t size) {
  String name = path.substring(path.lastIndexOf('/') + 1);
  std::vector<ListItem> acts;
  bool isIr = extOf(name) == "ir";
  if (isIr) acts.push_back({"Open in Infrared", "", ICON_IR, col::IR});
  acts.push_back({"View", "", ICON_INFO, 0});
  acts.push_back({"Rename", "", ICON_CONF, 0});
  acts.push_back({"Delete", "", ICON_POWER, col::BAD});
  acts.push_back({"Info", humanSize(size), ICON_INFO, 0});
  int sel = 0;
  int r = listMenu(name.c_str(), col::FILES, acts, sel, ICON_FILES);
  if (r < 0) return;
  String a = acts[r].label;
  if (a == "Open in Infrared") irOpenFile(path);
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
