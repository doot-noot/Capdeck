// CapDeck - Map: one map, toggleable layers. Lines AND dots.
//   Roads   major roads as mycelial threads (from OpenStreetMap, downloaded in Mycorrhiza)
//   Tiles   optional raster map tiles if you've put any on the SD card
//   Finds   your forage finds, each a little mushroom symbol
//   Nodes   Meshtastic nodes heard nearby
//   Me      where you are, with range rings
// Pan with Fn + arrows, zoom with - and =, TAB steps through things on screen, ENTER opens one.
#include "core.h"
#include <TinyGPSPlus.h>
#include <ArduinoJson.h>
#include <math.h>

int mapFocusNode = -1;     // set by the mesh app to open centred on a node
double mapStartLat = 0, mapStartLon = 0;  // set by Mycorrhiza to open on a saved region

// ------------------------------------------------------------------ projection (Web Mercator)
double lonToPx(double lon, int z) { return (lon + 180.0) / 360.0 * 256.0 * (double)(1 << z); }
double latToPx(double lat, int z) {
  double s = sin(lat * DEG_TO_RAD);
  s = fmax(-0.9999, fmin(0.9999, s));
  return (0.5 - log((1 + s) / (1 - s)) / (4 * M_PI)) * 256.0 * (double)(1 << z);
}
double metersPerPx(double lat, int z) { return 156543.03392 * cos(lat * DEG_TO_RAD) / (double)(1 << z); }

// ------------------------------------------------------------------ layers
enum { L_ROADS, L_TILES, L_FINDS, L_NODES, L_ME, L_COUNT };
static const char *LAYER_NAME[L_COUNT] = {"Roads", "Tiles", "Finds", "Nodes", "Me"};
static bool layerOn[L_COUNT] = {true, false, true, true, true};

// ------------------------------------------------------------------ road files (.rd from Mycorrhiza)
struct RoadRegion {
  String file, name;
  float s, w, n, e;
  uint32_t ways;
};
static std::vector<RoadRegion> roadRegions;
static bool regionsLoaded = false;

static void loadRoadRegions() {
  roadRegions.clear();
  regionsLoaded = true;
  if (!sdOK || !SD.exists("/capdeck/map/regions.json")) return;
  File f = SD.open("/capdeck/map/regions.json");
  JsonDocument d;
  if (f) { DeserializationError e = deserializeJson(d, f); f.close(); if (e) return; }
  for (JsonObject r : d.as<JsonArray>()) {
    RoadRegion rr;
    rr.file = (const char *)(r["file"] | "");
    rr.name = (const char *)(r["name"] | "");
    rr.s = r["south"] | 0.0f; rr.w = r["west"] | 0.0f;
    rr.n = r["north"] | 0.0f; rr.e = r["east"] | 0.0f;
    rr.ways = r["ways"] | 0;
    if (rr.file.length()) roadRegions.push_back(rr);
  }
}

static uint16_t roadColour(uint8_t cls, bool dim) {
  // mycelial threads: brightest trunk lines, fading to faint capillaries
  uint16_t hot = blend565(col::MESH, col::GPS, 0.4f);
  if (cls == 0) return dim ? blend565(hot, col::BG, 0.5f) : hot;                 // motorway
  if (cls == 1) return dim ? blend565(col::MESH, col::BG, 0.4f) : col::MESH;     // trunk
  return dim ? blend565(col::MESH, col::BG, 0.25f) : blend565(col::MESH, col::BG, 0.6f);  // primary
}

// Draws every road that touches the viewport. Streams points from SD; nothing large stays in RAM.
static void drawRoads(LovyanGFX &g, double cpx, double cpy, int z, int mapY, int mapH) {
  if (!regionsLoaded) loadRoadRegions();
  double west = (cpx - SCR_W / 2.0), north = (cpy - mapH / 2.0);
  int cx = SCR_W / 2, cy = mapY + mapH / 2;
  for (auto &rr : roadRegions) {
    // region bbox to pixels: skip a region wholly off-screen
    double rx0 = lonToPx(rr.w, z), rx1 = lonToPx(rr.e, z);
    double ry0 = latToPx(rr.n, z), ry1 = latToPx(rr.s, z);
    if (rx1 < west - 40 || rx0 > west + SCR_W + 40 || ry1 < north - 40 || ry0 > north + mapH + 40) continue;
    File f = SD.open(rr.file);
    if (!f) continue;
    char magic[4];
    f.read((uint8_t *)magic, 4);
    if (strncmp(magic, "CDR1", 4)) { f.close(); continue; }
    uint32_t ways; float bb[4];
    f.read((uint8_t *)&ways, 4);
    f.read((uint8_t *)bb, 16);
    for (uint32_t wi = 0; wi < ways; wi++) {
      uint8_t hdr[4];
      if (f.read(hdr, 4) != 4) break;
      uint8_t cls = hdr[0];
      uint16_t n = hdr[2] | (hdr[3] << 8);
      int32_t wbb[4];
      f.read((uint8_t *)wbb, 16);
      size_t bytes = (size_t)n * 8;
      // way bbox in px; cull if off-screen
      double wx0 = lonToPx(wbb[1] / 1e6, z), wx1 = lonToPx(wbb[3] / 1e6, z);
      double wy0 = latToPx(wbb[2] / 1e6, z), wy1 = latToPx(wbb[0] / 1e6, z);
      if (wx1 < west - 20 || wx0 > west + SCR_W + 20 || wy1 < north - 20 || wy0 > north + mapH + 20) {
        f.seek(f.position() + bytes);
        continue;
      }
      uint16_t c = roadColour(cls, z < 11);
      int px = 0, py = 0;
      bool have = false;
      for (uint16_t i = 0; i < n; i++) {
        int32_t ll[2];
        if (f.read((uint8_t *)ll, 8) != 8) { have = false; break; }
        int x = cx + (int)lround(lonToPx(ll[1] / 1e6, z) - cpx);
        int y = cy + (int)lround(latToPx(ll[0] / 1e6, z) - cpy);
        if (have) {
          if (!(x < -8 && px < -8) && !(x > SCR_W + 8 && px > SCR_W + 8) &&
              !(y < mapY - 8 && py < mapY - 8) && !(y > mapY + mapH + 8 && py > mapY + mapH + 8)) {
            g.drawLine(px, py, x, y, c);
            if (cls == 0) g.drawLine(px, py + 1, x, y + 1, blend565(c, col::BG, 0.5f));  // thicken motorways
          }
        }
        px = x; py = y; have = true;
      }
    }
    f.close();
  }
}

// ------------------------------------------------------------------ raster tiles (optional)
static bool drawTiles(LovyanGFX &g, double cpx, double cpy, int z, int mapY, int w, int h) {
  if (!sdOK) return false;
  bool any = false;
  int x0 = (int)floor((cpx - w / 2.0) / 256.0), x1 = (int)floor((cpx + w / 2.0) / 256.0);
  int y0 = (int)floor((cpy - h / 2.0) / 256.0), y1 = (int)floor((cpy + h / 2.0) / 256.0);
  for (int tx = x0; tx <= x1; tx++)
    for (int ty = y0; ty <= y1; ty++) {
      int sx = (int)lround(tx * 256.0 - (cpx - w / 2.0));
      int sy = mapY + (int)lround(ty * 256.0 - (cpy - h / 2.0));
      char path[48];
      snprintf(path, sizeof path, "/capdeck/map/tiles/%d/%d/%d.png", z, tx, ty);
      if (SD.exists(path)) { g.drawPngFile(SD, path, sx, sy); any = true; continue; }
      snprintf(path, sizeof path, "/capdeck/map/tiles/%d/%d/%d.jpg", z, tx, ty);
      if (SD.exists(path)) { g.drawJpgFile(SD, path, sx, sy); any = true; }
    }
  return any;
}

// ------------------------------------------------------------------ find symbols (dots, each a mushroom)
static uint16_t findColour(FindKind k) {
  switch (k) {
    case FK_SPORE: return col::LORA;
    case FK_FRUIT: return col::MESH;
    case FK_PUFFBALL: return col::PORTAL;
    case FK_FAIRY: return col::GPS;
    case FK_DEATHCAP: return col::BAD;
    case FK_MOREL: return col::FILES;
    case FK_CORDYCEPS: return col::RF;
    default: return col::TEXT;
  }
}

// Each kind gets a distinct little glyph so the map reads at a glance.
void drawFindSymbol(LovyanGFX &g, int x, int y, FindKind k, uint16_t c) {
  switch (k) {
    case FK_FAIRY:  // Wi-Fi: a ring (fairy ring)
      g.drawCircle(x, y, 4, c); g.fillCircle(x, y, 1, c); break;
    case FK_DEATHCAP:  // hidden net: ring with a slash
      g.drawCircle(x, y, 4, c); g.drawLine(x - 3, y - 3, x + 3, y + 3, c); break;
    case FK_PUFFBALL:  // BLE: a soft filled puff
      g.fillCircle(x, y, 3, c); g.drawCircle(x, y, 4, blend565(c, col::BG, 0.5f)); break;
    case FK_FRUIT:  // mesh node: a cap on a stem
      g.fillRect(x - 1, y, 2, 4, c); g.fillArc(x, y, 4, 0, 180, 360, c); break;
    case FK_CORDYCEPS:  // LoRaWAN: a club / antler
      g.drawLine(x, y + 4, x, y - 2, c); g.drawLine(x, y - 2, x - 2, y - 4, c);
      g.drawLine(x, y - 2, x + 2, y - 4, c); break;
    case FK_MOREL:  // rare: a diamond
      g.drawLine(x, y - 4, x + 4, y, c); g.drawLine(x + 4, y, x, y + 4, c);
      g.drawLine(x, y + 4, x - 4, y, c); g.drawLine(x - 4, y, x, y - 4, c); break;
    default:  // spore / packet: a plus spark
      g.drawFastHLine(x - 3, y, 7, c); g.drawFastVLine(x, y - 3, 7, c); break;
  }
}

// ------------------------------------------------------------------ the map
void appMap(int focusLayer) {
  const int mapY = BAR_H, mapH = SCR_H - BAR_H;
  int z = 15;
  int focusNode = mapFocusNode;
  mapFocusNode = -1;
  if (focusLayer >= 0 && focusLayer < L_COUNT) layerOn[focusLayer] = true;

  double cLat = 0, cLon = 0;
  bool haveCenter = false;
  if (mapStartLat != 0 || mapStartLon != 0) { cLat = mapStartLat; cLon = mapStartLon; haveCenter = true; mapStartLat = mapStartLon = 0; z = 13; }
  if (!haveCenter) haveCenter = myPosition(cLat, cLon);
  bool followMe = haveCenter && focusNode < 0;

  int selNode = -1;
  if (focusNode >= 0)
    for (size_t i = 0; i < meshNodes.size(); i++)
      if ((int)meshNodes[i].num == focusNode && meshNodes[i].hasPos) {
        selNode = i; cLat = meshNodes[i].lat; cLon = meshNodes[i].lon; haveCenter = true; followMe = false;
      }
  if (!haveCenter)  // nothing yet: first node or find with a position
    for (auto &n : meshNodes)
      if (n.hasPos) { cLat = n.lat; cLon = n.lon; haveCenter = true; break; }
  if (!haveCenter)
    for (auto &fnd : forageFinds)
      if (fnd.hasPos) { cLat = fnd.lat; cLon = fnd.lon; haveCenter = true; break; }

  // background sprite: roads + tiles, redrawn only when the view moves
  M5Canvas bg(&M5Cardputer.Display);
  bg.setColorDepth(16);
  bool haveBg = bg.createSprite(SCR_W, mapH) != nullptr;
  bool bgDirty = true, tiles = false;
  double bgLat = 1e9, bgLon = 1e9; int bgZ = -1;
  int selFind = -1, cycle = 0;  // TAB target: -1 none, else index into a combined list
  uint32_t lastDraw = 0; int frame = 0;

  while (true) {
    Key k = readKey();
    stayAwake();
    if (k.back) break;
    if ((k.ch == '-' || k.ch == '_') && z > 3) { z--; bgDirty = true; }
    if ((k.ch == '=' || k.ch == '+') && z < 18) { z++; bgDirty = true; }
    double mpp = metersPerPx(cLat, z);
    double panPx = 40;
    if (k.fn || k.left || k.right || k.up || k.down) {
      double dlon = panPx * mpp / (111320.0 * cos(cLat * DEG_TO_RAD));
      double dlat = panPx * mpp / 110540.0;
      bool panned = false;
      if (k.left) { cLon -= dlon; panned = true; }
      if (k.right) { cLon += dlon; panned = true; }
      if (k.up) { cLat += dlat; panned = true; }
      if (k.down) { cLat -= dlat; panned = true; }
      if (panned) { followMe = false; selNode = -1; selFind = -1; bgDirty = true; }
    }
    if (k.ch == 'm' || k.ch == 'c') {
      double la, lo;
      if (myPosition(la, lo)) { cLat = la; cLon = lo; followMe = true; selNode = selFind = -1; bgDirty = true; }
      else toast("No GPS position yet", col::WARN, 900);
    }
    if (k.ch == 'l') {  // cycle a layer on/off via a quick menu
      std::vector<ListItem> li;
      for (int i = 0; i < L_COUNT; i++) li.push_back({LAYER_NAME[i], layerOn[i] ? "on" : "off", ICON_MAP, layerOn[i] ? col::OK : col::DIM});
      int s2 = 0;
      int r = listMenu("Layers", col::MESH, li, s2, ICON_MAP);
      if (r >= 0) { layerOn[r] = !layerOn[r]; bgDirty = true; }
    }
    // TAB: step through on-screen nodes then finds, centring each
    if (k.tab) {
      std::vector<std::pair<int,int>> targets;  // (0=node/1=find, index)
      if (layerOn[L_NODES]) for (size_t i = 0; i < meshNodes.size(); i++) if (meshNodes[i].hasPos) targets.push_back({0, (int)i});
      if (layerOn[L_FINDS]) for (size_t i = 0; i < forageFinds.size(); i++) if (forageFinds[i].hasPos) targets.push_back({1, (int)i});
      if (!targets.empty()) {
        cycle = (cycle + 1) % targets.size();
        auto t = targets[cycle];
        selNode = selFind = -1;
        if (t.first == 0) { selNode = t.second; cLat = meshNodes[selNode].lat; cLon = meshNodes[selNode].lon; }
        else { selFind = t.second; cLat = forageFinds[selFind].lat; cLon = forageFinds[selFind].lon; }
        followMe = false; bgDirty = true;
      }
    }
    if (k.enter) {
      if (selNode >= 0) { meshNodeDetail(meshNodes[selNode]); bgDirty = true; }
      else if (selFind >= 0) {
        Find &fn = forageFinds[selFind];
        String s = fn.label + "\n" + findName(fn.kind) + " (" + findPlain(fn.kind) + ")\n\n";
        if (fn.macLen) { char m[20]; snprintf(m, sizeof m, "%02X:%02X:%02X:%02X:%02X:%02X", fn.mac[0],fn.mac[1],fn.mac[2],fn.mac[3],fn.mac[4],fn.mac[5]); s += String("MAC: ") + m + "\n"; }
        s += "Signal: " + String(fn.rssi) + " dBm\n";
        if (fn.channel) s += "Channel: " + String(fn.channel) + "\n";
        if (fn.hasPos) s += "Here: " + String(fn.lat, 5) + ", " + String(fn.lon, 5) + "\n";
        textViewer(fn.label.length() ? fn.label.c_str() : findName(fn.kind), s, findColour(fn.kind));
        bgDirty = true;
      }
    }
    if (followMe) { double la, lo; if (myPosition(la, lo) && (fabs(la - cLat) > 1e-6 || fabs(lo - cLon) > 1e-6)) { cLat = la; cLon = lo; bgDirty = true; } }

    if (millis() - lastDraw < 120 && !k.any) { delay(5); continue; }
    lastDraw = millis();
    frame++;
    double cpx = lonToPx(cLon, z), cpy = latToPx(cLat, z);
    int cx = SCR_W / 2, cy = mapY + mapH / 2;

    if (!haveCenter) {
      canvas.fillScreen(col::BG);
      drawStatusBar("MAP", col::MESH);
      canvas.setFont(&fonts::Font2); canvas.setTextDatum(middle_center); canvas.setTextColor(col::TEXT);
      canvas.drawString("No position yet", cx, cy - 10);
      canvas.setFont(&fonts::Font0); canvas.setTextColor(col::DIM);
      canvas.drawString("get a GPS fix, forage, or add a region in Mycorrhiza", cx, cy + 8);
      pushFrame();
      haveCenter = myPosition(cLat, cLon);
      if (haveCenter) followMe = true;
      continue;
    }

    if (haveBg && (bgDirty || bgZ != z || bgLat != cLat || bgLon != cLon)) {
      bg.fillScreen(col::BG);
      tiles = layerOn[L_TILES] && drawTiles(bg, cpx, cpy, z, 0, SCR_W, mapH);
      if (!tiles) {  // faint graticule so panning reads
        for (int gx = (int)fmod(-cpx, 32.0); gx < SCR_W; gx += 32) bg.drawFastVLine(gx + 32, 0, mapH, blend565(col::PANEL, col::BG, 0.6f));
        for (int gy = (int)fmod(-cpy, 32.0); gy < mapH; gy += 32) bg.drawFastHLine(0, gy + 32, SCR_W, blend565(col::PANEL, col::BG, 0.6f));
      }
      if (layerOn[L_ROADS]) drawRoads(bg, cpx, cpy, z, 0, mapH);
      bgDirty = false; bgZ = z; bgLat = cLat; bgLon = cLon;
    }
    if (haveBg) bg.pushSprite(&canvas, 0, mapY);
    else { canvas.fillRect(0, mapY, SCR_W, mapH, col::BG); if (layerOn[L_ROADS]) drawRoads(canvas, cpx, cpy, z, mapY, mapH); }
    drawStatusBar("MAP", col::MESH);

    // me + range rings
    double mla, mlo; bool me = myPosition(mla, mlo);
    int mx = cx, my = cy;
    if (me && layerOn[L_ME]) {
      mx = cx + (int)lround(lonToPx(mlo, z) - cpx);
      my = cy + (int)lround(latToPx(mla, z) - cpy);
      const double steps[] = {30.48, 76.2, 152.4, 402.3, 804.7, 1609.3, 3218.7, 8046.7, 16093.4, 32186.9};
      double ring = steps[0];
      for (double s : steps) if (s / mpp <= 46) ring = s;
      for (int r = 1; r <= 4; r++) canvas.drawCircle(mx, my, (int)(ring * r / mpp), tiles ? col::DIM : col::FAINT);
    }

    // finds
    if (layerOn[L_FINDS]) {
      canvas.setFont(&fonts::Font0);
      for (size_t i = 0; i < forageFinds.size(); i++) {
        Find &fn = forageFinds[i];
        if (!fn.hasPos) continue;
        int x = cx + (int)lround(lonToPx(fn.lon, z) - cpx);
        int y = cy + (int)lround(latToPx(fn.lat, z) - cpy);
        if (x < -8 || x > SCR_W + 8 || y < mapY - 8 || y > SCR_H + 8) continue;
        uint16_t c = findColour(fn.kind);
        if ((int)i == selFind) canvas.drawCircle(x, y, 7 + frame % 3, c);
        drawFindSymbol(canvas, x, y, fn.kind, c);
      }
    }

    // nodes
    if (layerOn[L_NODES]) {
      canvas.setFont(&fonts::Font0);
      for (size_t i = 0; i < meshNodes.size(); i++) {
        MeshNode &n = meshNodes[i];
        if (!n.hasPos) continue;
        int x = cx + (int)lround(lonToPx(n.lon, z) - cpx);
        int y = cy + (int)lround(latToPx(n.lat, z) - cpy);
        if (x < -20 || x > SCR_W + 20 || y < mapY - 10 || y > SCR_H + 10) continue;
        bool fresh = n.lastMs && millis() - n.lastMs < 15UL * 60 * 1000;
        uint16_t c = (int)i == selNode ? col::TEXT : fresh ? col::MESH : col::DIM;
        if ((int)i == selNode) canvas.drawCircle(x, y, 6 + frame % 4, col::MESH);
        canvas.fillCircle(x, y, 3, c);
        canvas.drawCircle(x, y, 4, col::BG);
        String lab = n.shortName.length() ? n.shortName : String("node");
        int tw = canvas.textWidth(lab);
        canvas.setTextDatum(bottom_center);
        canvas.fillRect(x - tw / 2 - 1, y - 14, tw + 2, 9, col::BG);
        canvas.setTextColor(c);
        canvas.drawString(lab, x, y - 6);
      }
    }

    // me marker on top
    if (me && layerOn[L_ME] && mx > -5 && mx < SCR_W + 5 && my > mapY - 5 && my < SCR_H + 5) {
      canvas.fillTriangle(mx, my - 6, mx - 5, my + 5, mx + 5, my + 5, gpsFix() ? col::GPS : col::DIM);
      canvas.drawTriangle(mx, my - 6, mx - 5, my + 5, mx + 5, my + 5, col::BG);
    }

    // north arrow + scale bar
    canvas.fillTriangle(SCR_W - 10, mapY + 4, SCR_W - 14, mapY + 13, SCR_W - 6, mapY + 13, col::TEXT);
    canvas.setTextDatum(top_center); canvas.setTextColor(col::TEXT);
    canvas.drawString("N", SCR_W - 10, mapY + 15);
    {
      const double steps[] = {30.48, 76.2, 152.4, 402.3, 804.7, 1609.3, 3218.7, 8046.7, 16093.4, 32186.9};
      double ring = steps[0];
      for (double s : steps) if (s / mpp <= 46) ring = s;
      int sl = (int)(ring / mpp);
      canvas.fillRect(4, SCR_H - 12, sl + 4, 10, col::BG);
      canvas.drawFastHLine(6, SCR_H - 5, sl, col::TEXT);
      canvas.drawFastVLine(6, SCR_H - 8, 4, col::TEXT);
      canvas.drawFastVLine(6 + sl, SCR_H - 8, 4, col::TEXT);
      canvas.setTextDatum(bottom_left); canvas.drawString(fmtDistance(ring), 8, SCR_H - 6);
    }

    // info strip
    canvas.setTextDatum(top_left);
    String info;
    if (selNode >= 0) {
      MeshNode &n = meshNodes[selNode];
      info = (n.shortName.length() ? n.shortName : String("node"));
      if (me) info += "  " + fmtDistance(distanceM(mla, mlo, n.lat, n.lon));
    } else if (selFind >= 0) {
      info = findName(forageFinds[selFind].kind);
      if (me && forageFinds[selFind].hasPos) info += "  " + fmtDistance(distanceM(mla, mlo, forageFinds[selFind].lat, forageFinds[selFind].lon));
    } else {
      int nn = 0, nf = 0;
      for (auto &n : meshNodes) if (n.hasPos) nn++;
      for (auto &fnd : forageFinds) if (fnd.hasPos) nf++;
      info = String(nn) + " nodes  " + String(nf) + " finds  z" + String(z);
    }
    int iw = canvas.textWidth(info);
    canvas.fillRect(2, mapY + 2, iw + 6, 11, col::BG);
    canvas.setTextColor(selNode >= 0 ? col::MESH : selFind >= 0 ? findColour(forageFinds[selFind].kind) : col::DIM);
    canvas.drawString(info, 5, mapY + 4);

    // OSM attribution when roads are shown
    if (layerOn[L_ROADS] && !roadRegions.empty()) {
      canvas.setTextDatum(bottom_right);
      canvas.setTextColor(blend565(col::DIM, col::BG, 0.4f));
      canvas.drawString("(c) OpenStreetMap", SCR_W - 4, SCR_H - 2);
    }
    pushFrame();
  }
  if (haveBg) bg.deleteSprite();
}
