// CapDeck - Mycorrhiza: the fungus-root network that trades with the outside world.
// Downloads & lookups in one place:
//   Ask         DuckDuckGo Instant Answers, Wikipedia as the fallback (facts and summaries - not a web-results list)
//   Map regions a city's major roads from OpenStreetMap, simplified and saved as a small file for the Map app
//   IR remotes  any remote from Flipper-IRDB, downloaded on demand (no 21 MB kit needed)
//   Sky data    the ISS orbit for Nightcap's pass alerts
// No accounts, no API keys. Big downloads are streamed straight to the SD card (the Cardputer has 512 KB of RAM).
#include "core.h"
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

static const char *UA = "CapDeck/" CAPDECK_VERSION " (+https://github.com/doot-noot/Capdeck)";
static const char *IRDB_RAW = "https://raw.githubusercontent.com/Lucaslhm/Flipper-IRDB/main/";
static const char *IRDB_TREE = "https://api.github.com/repos/Lucaslhm/Flipper-IRDB/git/trees/main?recursive=1";
static const char *IRDB_DIR = "/capdeck/ir/Flipper-IRDB/";
static const char *IRDB_INDEX = "/capdeck/ir/irdb_index.txt";
static const char *REGIONS = "/capdeck/map/regions.json";
static const char *ISS_TLE = "/capdeck/sky/iss.tle";

void skyReloadTle();  // nightcap.cpp

// ------------------------------------------------------------------ plumbing
String urlEncode(const String &s, bool keepSlash) {
  static const char *hex = "0123456789ABCDEF";
  String o;
  o.reserve(s.length() * 3);
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    bool safe = (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '-' || c == '_' ||
                c == '.' || c == '~' || (keepSlash && c == '/');
    if (safe) o += c;
    else { o += '%'; o += hex[(c >> 4) & 0xF]; o += hex[c & 0xF]; }
  }
  return o;
}

// A Stream that hands every byte to a callback. HTTPClient de-chunks into it, so we can parse or save on the fly.
class SinkStream : public Stream {
 public:
  std::function<bool(const uint8_t *, size_t)> sink;
  size_t total = 0;
  size_t write(uint8_t c) override { return write(&c, 1); }
  size_t write(const uint8_t *b, size_t n) override {
    total += n;
    return sink(b, n) ? n : 0;
  }
  int available() override { return 0; }
  int read() override { return -1; }
  int peek() override { return -1; }
  void flush() override {}
};

static bool openGet(HTTPClient &http, WiFiClientSecure &tls, const String &url, String &err, int timeoutMs = 25000) {
  if (netState() != NetState::UP && !wifiWait(15000, "Mycorrhiza needs Wi-Fi")) {
    err = "No Wi-Fi";
    return false;
  }
  tls.setInsecure();  // no certificate store on the device; everything fetched here is public and read-only
  http.setTimeout(timeoutMs);
  http.setConnectTimeout(12000);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.setUserAgent(UA);
  if (!http.begin(tls, url)) {
    err = "Couldn't open the connection";
    return false;
  }
  int code = http.GET();
  if (code != 200) {
    err = code > 0 ? "Server said HTTP " + String(code) : "No reply (" + HTTPClient::errorToString(code) + ")";
    http.end();
    return false;
  }
  return true;
}

static bool streamGet(const String &url, String &err, std::function<bool(const uint8_t *, size_t)> sink, int timeoutMs = 25000) {
  WiFiClientSecure tls;
  HTTPClient http;
  if (!openGet(http, tls, url, err, timeoutMs)) return false;
  SinkStream s;
  s.sink = sink;
  int r = http.writeToStream(&s);
  http.end();
  if (r < 0) {
    err = "Download stopped (" + HTTPClient::errorToString(r) + ")";
    return false;
  }
  return true;
}

bool httpGetToString(const String &url, String &body, String &err, size_t maxBytes) {
  body = "";
  bool truncated = false;
  bool ok = streamGet(url, err, [&](const uint8_t *b, size_t n) {
    if (body.length() + n > maxBytes) { truncated = true; return false; }
    body.concat((const char *)b, n);
    return true;
  });
  return ok || truncated;  // a capped answer is still an answer
}

static bool mkdirs(const String &path) {  // mkdir -p for the parent folders of `path`
  for (int i = 1; i < (int)path.length(); i++)
    if (path[i] == '/') {
      String d = path.substring(0, i);
      if (!SD.exists(d)) SD.mkdir(d);
    }
  return true;
}

bool httpGetToFile(const String &url, const char *path, String &err, std::function<void(size_t)> progress) {
  if (!sdOK) { err = "No SD card"; return false; }
  mkdirs(path);
  String part = String(path) + ".part";
  File f = SD.open(part, FILE_WRITE);
  if (!f) { err = "Couldn't write to the SD card"; return false; }
  uint32_t lastP = 0;
  size_t got = 0;
  bool ok = streamGet(url, err, [&](const uint8_t *b, size_t n) {
    if (f.write(b, n) != n) return false;
    got += n;
    if (progress && millis() - lastP > 300) { lastP = millis(); progress(got); }
    return true;
  }, 60000);
  f.close();
  if (!ok || !got) { SD.remove(part); if (ok) err = "The download was empty"; return false; }
  if (SD.exists(path)) SD.remove(path);
  SD.rename(part, path);
  return true;
}

static void busy(const char *title, const String &line1, const String &line2 = "") {
  drawHeader(title, col::AI, ICON_MYCO);
  drawIcon(canvas, ICON_MYCO, SCR_W / 2, 64, 40, col::AI);
  canvas.setFont(&fonts::Font2);
  canvas.setTextDatum(middle_center);
  canvas.setTextColor(col::TEXT);
  canvas.drawString(line1, SCR_W / 2, 98);
  canvas.setFont(&fonts::Font0);
  canvas.setTextColor(col::DIM);
  canvas.drawString(line2, SCR_W / 2, 116);
  pushFrame();
}

static String slug(const String &s) {
  String o;
  for (size_t i = 0; i < s.length() && o.length() < 24; i++) {
    char c = tolower(s[i]);
    if (isalnum((unsigned char)c)) o += c;
    else if (o.length() && o[o.length() - 1] != '_') o += '_';
  }
  return o.length() ? o : String("region");
}

// ================================================================== Ask (DuckDuckGo + Wikipedia)
static String ddgLookup(const String &q, String &src, String &err) {
  String body;
  if (!httpGetToString("https://api.duckduckgo.com/?q=" + urlEncode(q) + "&format=json&no_html=1&skip_disambig=1", body, err)) return "";
  JsonDocument d;
  if (deserializeJson(d, body)) { err = "Couldn't read DuckDuckGo's reply"; return ""; }
  String out = d["AbstractText"] | "";
  if (!out.length()) out = d["Answer"] | "";
  if (!out.length()) out = d["Definition"] | "";
  if (!out.length())
    for (JsonObject t : d["RelatedTopics"].as<JsonArray>()) {
      String s = t["Text"] | "";
      if (s.length()) { out = s; break; }
    }
  if (out.length()) {
    String s = d["AbstractSource"] | "";
    String u = d["AbstractURL"] | "";
    src = s.length() ? s : String("DuckDuckGo");
    if (u.length()) src += "\n" + u;
  }
  return out;
}

static String wikiLookup(const String &q, String &src, String &err) {
  String body;
  if (!httpGetToString("https://en.wikipedia.org/w/api.php?action=query&list=search&srlimit=1&format=json&srsearch=" + urlEncode(q), body, err)) return "";
  JsonDocument d;
  if (deserializeJson(d, body)) { err = "Couldn't read Wikipedia's reply"; return ""; }
  String title = d["query"]["search"][0]["title"] | "";
  if (!title.length()) return "";
  String body2;
  if (!httpGetToString("https://en.wikipedia.org/api/rest_v1/page/summary/" + urlEncode(title), body2, err)) return "";
  JsonDocument d2;
  if (deserializeJson(d2, body2)) { err = "Couldn't read Wikipedia's reply"; return ""; }
  String ex = d2["extract"] | "";
  if (ex.length()) src = "Wikipedia - " + title;
  return ex;
}

static void askScreen() {
  String q;
  while (true) {
    if (!textInput(tip("Ask: a thing, place or word", "Ask"), q, col::AI, false, 100)) return;
    q.trim();
    if (!q.length()) continue;
    busy("Ask", "looking up " + q, "DuckDuckGo, then Wikipedia");
    String src, err;
    String ans = ddgLookup(q, src, err);
    if (!ans.length()) {
      String err2;
      ans = wikiLookup(q, src, err2);
      if (!ans.length() && err2.length()) err = err2;
    }
    if (!ans.length()) {
      toast(err.length() ? err : "Nothing found. Try a thing or a name rather than a question.", col::WARN, 2200);
      continue;
    }
    String title = q.length() > 22 ? q.substring(0, 22) + ".." : q;
    textViewer(title.c_str(), ans + "\n\n-- " + src, col::AI);
  }
}

// ================================================================== Map regions (OpenStreetMap roads)
// Road file format (/capdeck/map/roads/<slug>.rd), little-endian:
//   header  "CDR1", u32 ways, f32 south, west, north, east
//   way     u8 class (0 motorway, 1 trunk, 2 primary), u8 0, u16 n, i32 minLat, minLon, maxLat, maxLon (deg*1e6),
//           then n x (i32 lat, i32 lon)
struct RoadWriter {
  File f;
  uint32_t ways = 0, points = 0;
  uint8_t cls = 0;
  std::vector<int32_t> pts;
  int32_t lastLa = 0, lastLo = 0, seenLa = 0, seenLo = 0;
  bool inWay = false;
  void beginWay() { pts.clear(); inWay = true; }
  void point(double lat, double lon) {
    int32_t la = lat * 1e6, lo = lon * 1e6;
    seenLa = la; seenLo = lo;
    if (!pts.empty()) {  // keep a point only every ~40 m: plenty for "where the highways go"
      double dla = (la - lastLa) * 0.111, dlo = (lo - lastLo) * 0.111 * cos(lat * DEG_TO_RAD);
      if (dla * dla + dlo * dlo < 40.0 * 40.0) return;
    }
    if (pts.size() < 4000) { pts.push_back(la); pts.push_back(lo); lastLa = la; lastLo = lo; }
  }
  void endWay() {
    if (!inWay) return;
    inWay = false;
    if (!pts.empty() && (seenLa != lastLa || seenLo != lastLo)) { pts.push_back(seenLa); pts.push_back(seenLo); }
    uint16_t n = pts.size() / 2;
    if (n < 2) return;
    int32_t mnLa = INT32_MAX, mnLo = INT32_MAX, mxLa = INT32_MIN, mxLo = INT32_MIN;
    for (size_t i = 0; i < pts.size(); i += 2) {
      mnLa = min(mnLa, pts[i]); mxLa = max(mxLa, pts[i]);
      mnLo = min(mnLo, pts[i + 1]); mxLo = max(mxLo, pts[i + 1]);
    }
    uint8_t h[4] = {cls, 0, (uint8_t)(n & 0xFF), (uint8_t)(n >> 8)};
    f.write(h, 4);
    int32_t bb[4] = {mnLa, mnLo, mxLa, mxLo};
    f.write((uint8_t *)bb, sizeof bb);
    f.write((uint8_t *)pts.data(), pts.size() * 4);
    ways++;
    points += n;
  }
};

// Streams Overpass JSON: picks out each way and its "lat"/"lon" pairs without ever holding the whole reply.
struct OverpassParser {
  RoadWriter &w;
  char win[12] = {0};
  bool inNum = false;
  char num[24];
  int numLen = 0, numKind = 0;  // 1 lat, 2 lon
  double lat = 0;
  bool haveLat = false;
  explicit OverpassParser(RoadWriter &rw) : w(rw) {}
  bool ends(const char *pat) {
    int n = strlen(pat);
    return !strncmp(win + sizeof win - n, pat, n);
  }
  void feed(char c) {
    if (inNum) {
      if ((c >= '0' && c <= '9') || c == '-' || c == '.' || c == 'e' || c == 'E' || c == '+') {
        if (numLen < 23) num[numLen++] = c;
        return;
      }
      num[numLen] = 0;
      inNum = false;
      double v = atof(num);
      if (numKind == 1) { lat = v; haveLat = true; }
      else if (haveLat) { w.point(lat, v); haveLat = false; }
    }
    memmove(win, win + 1, sizeof win - 1);
    win[sizeof win - 1] = c;
    if (c == ':' && ends("\"lat\":")) { inNum = true; numLen = 0; numKind = 1; }
    else if (c == ':' && ends("\"lon\":")) { inNum = true; numLen = 0; numKind = 2; }
    else if (c == '"' && ends("\"type\":\"way\"")) { w.endWay(); w.beginWay(); }
  }
};

static bool loadRegions(JsonDocument &doc) {
  doc.clear();
  File f = sdOK ? SD.open(REGIONS) : File();
  if (f) {
    DeserializationError e = deserializeJson(doc, f);
    f.close();
    if (!e && doc.is<JsonArray>()) return true;
  }
  doc.to<JsonArray>();
  return false;
}

static bool saveRegions(JsonDocument &doc) {
  mkdirs(REGIONS);
  File f = SD.open(REGIONS, FILE_WRITE);
  if (!f) return false;
  serializeJsonPretty(doc, f);
  f.close();
  return true;
}

static void addRegion() {
  if (!sdOK) { toast("No SD card", col::BAD, 1200); return; }
  String place;
  if (!textInput("City or town", place, col::AI, false, 60)) return;
  place.trim();
  if (!place.length()) return;
  std::vector<ListItem> dl = {{"Highways only", "smallest, quickest", ICON_MAP, 0}, {"Highways + main roads", "more detail", ICON_MAP, 0}};
  int dsel = 0;
  int detail = listMenu("How much?", col::AI, dl, dsel, ICON_MAP);
  if (detail < 0) return;
  busy("Map regions", "finding " + place, "OpenStreetMap / Nominatim");
  String body, err;
  if (!httpGetToString("https://nominatim.openstreetmap.org/search?format=json&limit=1&q=" + urlEncode(place), body, err, 20000)) {
    toast(err, col::BAD, 1800);
    return;
  }
  JsonDocument d;
  if (deserializeJson(d, body) || !d[0]["boundingbox"]) { toast("Couldn't find that place", col::WARN, 1600); return; }
  double s = atof(d[0]["boundingbox"][0] | "0"), n = atof(d[0]["boundingbox"][1] | "0");
  double w = atof(d[0]["boundingbox"][2] | "0"), e = atof(d[0]["boundingbox"][3] | "0");
  double cla = (s + n) / 2, clo = (w + e) / 2;
  // keep it a city-sized box (~55 km): the device draws it, the SD stores it
  if (n - s > 0.5) { s = cla - 0.25; n = cla + 0.25; }
  if (e - w > 0.7) { w = clo - 0.35; e = clo + 0.35; }
  if (n - s < 0.15) { s = cla - 0.075; n = cla + 0.075; }
  if (e - w < 0.2) { w = clo - 0.1; e = clo + 0.1; }
  String name = place;
  String file = "/capdeck/map/roads/" + slug(place) + ".rd";
  mkdirs(file);
  RoadWriter rw;
  rw.f = SD.open(file, FILE_WRITE);
  if (!rw.f) { toast("Couldn't write to the SD card", col::BAD, 1400); return; }
  uint8_t hdr[24] = {'C', 'D', 'R', '1'};
  rw.f.write(hdr, 24);  // filled in at the end
  const char *classes[] = {"motorway", "trunk", "primary"};
  int nClasses = detail == 0 ? 2 : 3;
  char bbox[96];
  snprintf(bbox, sizeof bbox, "(%.5f,%.5f,%.5f,%.5f)", s, w, n, e);
  bool ok = true;
  for (int c = 0; c < nClasses && ok; c++) {
    rw.cls = c;
    String q = String("[out:json][timeout:90];way[\"highway\"=\"") + classes[c] + "\"]" + bbox + ";out skel geom;";
    OverpassParser op(rw);
    uint32_t lastUi = 0;
    size_t bytes = 0;
    busy("Map regions", String("growing ") + classes[c] + "s", "OpenStreetMap / Overpass");
    ok = streamGet("https://overpass-api.de/api/interpreter?data=" + urlEncode(q), err, [&](const uint8_t *b, size_t len) {
      for (size_t i = 0; i < len; i++) op.feed((char)b[i]);
      bytes += len;
      if (millis() - lastUi > 400) {
        lastUi = millis();
        busy("Map regions", String("growing ") + classes[c] + "s  " + humanSize(bytes),
             String(rw.ways) + " roads so far  (big cities take a minute)");
        stayAwake();
      }
      return true;
    }, 120000);
    rw.endWay();
  }
  // header: magic, ways, bbox
  rw.f.seek(4);
  rw.f.write((uint8_t *)&rw.ways, 4);
  float bb[4] = {(float)s, (float)w, (float)n, (float)e};
  rw.f.write((uint8_t *)bb, 16);
  rw.f.close();
  if (!ok || !rw.ways) {
    SD.remove(file);
    toast(!ok ? err : String("No roads found there"), col::BAD, 2000);
    return;
  }
  JsonDocument regs;
  loadRegions(regs);
  JsonArray arr = regs.as<JsonArray>();
  for (size_t i = 0; i < arr.size(); i++)
    if (String((const char *)(arr[i]["file"] | "")) == file) { arr.remove(i); break; }
  JsonObject r = arr.add<JsonObject>();
  r["name"] = name;
  r["file"] = file;
  r["south"] = s; r["west"] = w; r["north"] = n; r["east"] = e;
  r["ways"] = rw.ways;
  r["detail"] = detail == 0 ? "highways" : "highways+main";
  saveRegions(regs);
  toast(String(rw.ways) + " roads saved for " + name + ". Open Map to see them.", col::OK, 2200);
}

static void regionsScreen() {
  int sel = 0;
  while (true) {
    JsonDocument regs;
    loadRegions(regs);
    JsonArray arr = regs.as<JsonArray>();
    std::vector<ListItem> items = {{"Add a place", "download roads", ICON_MYCO, col::OK}};
    for (JsonObject r : arr) items.push_back({String((const char *)(r["name"] | "?")), String((int)(r["ways"] | 0)) + " roads", ICON_MAP, 0});
    int i = listMenu("Map regions", col::AI, items, sel, ICON_MAP);
    if (i < 0) return;
    if (i == 0) { addRegion(); continue; }
    JsonObject r = arr[i - 1];
    String nm = (const char *)(r["name"] | "?");
    std::vector<ListItem> acts = {{"Open on the map", "", ICON_MAP, 0}, {"Delete", "", ICON_FILES, col::BAD}};
    int a2 = 0;
    int a = listMenu(nm.c_str(), col::AI, acts, a2, ICON_MAP);
    if (a == 0) {
      extern double mapStartLat, mapStartLon;
      mapStartLat = ((double)(r["south"] | 0.0) + (double)(r["north"] | 0.0)) / 2;
      mapStartLon = ((double)(r["west"] | 0.0) + (double)(r["east"] | 0.0)) / 2;
      appMap(0);
    } else if (a == 1 && confirmBox("Delete " + nm + "?", col::BAD)) {
      SD.remove((const char *)(r["file"] | ""));
      arr.remove(i - 1);
      saveRegions(regs);
    }
  }
}

// ================================================================== IR remotes on demand (Flipper-IRDB)
bool irFetchRemote(const String &relPath, String &localPath, String &err) {
  localPath = String(IRDB_DIR) + relPath;
  if (SD.exists(localPath)) return true;
  busy("IR remotes", "downloading", relPath.substring(relPath.lastIndexOf('/') + 1));
  return httpGetToFile(String(IRDB_RAW) + urlEncode(relPath, true), localPath.c_str(), err);
}

// Builds /capdeck/ir/irdb_index.txt from the repo's file list (one GitHub API call, streamed).
static bool buildIrIndex(String &err) {
  String part = String(IRDB_INDEX) + ".part";
  mkdirs(part);
  File f = SD.open(part, FILE_WRITE);
  if (!f) { err = "Couldn't write to the SD card"; return false; }
  char win[9] = {0};
  bool inPath = false;
  String path;
  uint32_t count = 0, lastUi = 0;
  bool ok = streamGet(IRDB_TREE, err, [&](const uint8_t *b, size_t n) {
    for (size_t i = 0; i < n; i++) {
      char c = b[i];
      if (inPath) {
        if (c == '"') {
          inPath = false;
          if (path.endsWith(".ir")) { f.println(path); count++; }
        } else if (path.length() < 200) path += c;
        continue;
      }
      memmove(win, win + 1, sizeof win - 1);
      win[sizeof win - 1] = c;
      if (!strncmp(win, "\"path\":\"", 8)) { inPath = true; path = ""; }
    }
    if (millis() - lastUi > 400) { lastUi = millis(); busy("IR remotes", "indexing the library", String(count) + " remotes"); }
    return true;
  }, 90000);
  f.close();
  if (!ok || count < 100) { SD.remove(part); if (ok) err = "The list came back short"; return false; }
  if (SD.exists(IRDB_INDEX)) SD.remove(IRDB_INDEX);
  SD.rename(part, IRDB_INDEX);
  return true;
}

void irLibrarySearch() {
  if (!sdOK) { toast("No SD card", col::BAD, 1200); return; }
  if (!SD.exists(IRDB_INDEX)) {
    if (!confirmBox("Download the remote list? (one small file, about 9,000 remotes)", col::IR)) return;
    String err;
    if (!buildIrIndex(err)) { toast(err, col::BAD, 1800); return; }
  }
  String q;
  while (textInput(tip("Brand or device (e.g. samsung tv)", "Search remotes"), q, col::IR, false, 40)) {
    q.trim();
    q.toLowerCase();
    if (!q.length()) continue;
    std::vector<String> words;
    for (int a = 0; a < (int)q.length();) {
      int b = q.indexOf(' ', a);
      if (b < 0) b = q.length();
      if (b > a) words.push_back(q.substring(a, b));
      a = b + 1;
    }
    std::vector<String> hits;
    File f = SD.open(IRDB_INDEX);
    while (f && f.available() && hits.size() < 80) {
      String line = f.readStringUntil('\n');
      line.trim();
      String low = line;
      low.toLowerCase();
      low.replace('_', ' ');
      bool all = true;
      for (auto &w : words) all &= low.indexOf(w) >= 0;
      if (all) hits.push_back(line);
    }
    if (f) f.close();
    if (hits.empty()) { toast("No remotes match. Try just the brand.", col::WARN, 1500); continue; }
    std::vector<ListItem> items;
    for (auto &h : hits) {
      int s1 = h.lastIndexOf('/');
      String nm = h.substring(s1 + 1);
      nm.replace(".ir", "");
      nm.replace('_', ' ');
      bool local = SD.exists(String(IRDB_DIR) + h);
      String cat = h.substring(0, h.indexOf('/'));
      cat.replace('_', ' ');
      items.push_back({nm, local ? cat : String("get"), ICON_IR, local ? (uint16_t)0 : col::DIM});
    }
    int sel = 0;
    int r = listMenu((String(hits.size()) + (hits.size() >= 80 ? "+ found" : " found")).c_str(), col::IR, items, sel, ICON_IR);
    if (r < 0) continue;
    String local, err;
    if (!irFetchRemote(hits[r], local, err)) { toast(err, col::BAD, 1800); continue; }
    irOpenFile(local);
  }
}

// ================================================================== Sky data (ISS orbit for Nightcap)
bool skyFetchIss(String &err) {
  busy("Sky data", "fetching the ISS orbit", "CelesTrak");
  if (!httpGetToFile("https://celestrak.org/NORAD/elements/gp.php?CATNR=25544&FORMAT=TLE", ISS_TLE, err)) return false;
  File f = SD.open(ISS_TLE);
  String l1 = f ? f.readStringUntil('\n') : String("");
  if (f) f.close();
  if (l1.indexOf("ISS") < 0 && !l1.startsWith("1 ")) { err = "That didn't look like orbit data"; return false; }
  skyReloadTle();
  return true;
}

static String fileAge(const char *path) {
  if (!sdOK || !SD.exists(path)) return "none yet";
  File f = SD.open(path);
  time_t t = f ? f.getLastWrite() : 0;
  if (f) f.close();
  if (!t || !timeValid()) return "saved";
  long days = (time(nullptr) - t) / 86400;
  return days <= 0 ? String("today") : String(days) + (days == 1 ? " day old" : " days old");
}

// ================================================================== the app
void appMycorrhiza() {
  int sel = 0;
  while (true) {
    int regions = 0;
    {
      JsonDocument regs;
      loadRegions(regs);
      regions = regs.as<JsonArray>().size();
    }
    std::vector<ListItem> items = {
        {"Ask anything", "DuckDuckGo + Wikipedia", ICON_MYCO, 0},
        {"Map regions", String(regions) + " saved", ICON_MAP, col::MESH},
        {"IR remotes", "search the library", ICON_IR, col::IR},
        {"Sky data", "ISS orbit: " + fileAge(ISS_TLE), ICON_NIGHTCAP, col::PORTAL},
        {"About", "", ICON_INFO, 0},
    };
    int r = listMenu("Mycorrhiza", col::AI, items, sel, ICON_MYCO);
    if (r < 0) return;
    switch (r) {
      case 0: askScreen(); break;
      case 1: regionsScreen(); break;
      case 2: irLibrarySearch(); break;
      case 3: {
        String err;
        bool ok = skyFetchIss(err);
        toast(ok ? "ISS orbit saved. Nightcap can now warn you before visible passes." : err, ok ? col::OK : col::BAD, 2000);
        break;
      }
      case 4:
        textViewer("Mycorrhiza",
                   "In the forest, mycorrhiza is the fungal network that trades with the world around it. Here it's "
                   "how CapDeck fetches things - no accounts, no API keys.\n\n"
                   "ASK: DuckDuckGo Instant Answers (facts, definitions, summaries), with Wikipedia as the fallback. "
                   "It is not a full web search.\n\n"
                   "MAP REGIONS: type a city, pick how much detail, and the major roads download from OpenStreetMap "
                   "(Nominatim + Overpass), simplified to a small file in /capdeck/map/roads. The list is "
                   "/capdeck/map/regions.json - edit it by hand if you like. Map data (c) OpenStreetMap contributors.\n\n"
                   "IR REMOTES: search Flipper-IRDB by brand or device; a remote downloads from GitHub the first time "
                   "you open it, then lives on your SD card.\n\n"
                   "SKY DATA: the ISS orbit from CelesTrak, for Nightcap's pass alerts. It goes stale in a week or two "
                   "(the ISS boosts its orbit), so refresh it now and then.",
                   col::AI);
        break;
    }
  }
}
