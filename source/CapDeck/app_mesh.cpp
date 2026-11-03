// CapDeck - Mesh: passive Meshtastic node finder with a GPS-centred map
#include "core.h"
#include <TinyGPSPlus.h>
#include <algorithm>

// ------------------------------------------------------------------ web-mercator helpers
static double lonToPx(double lon, int z) { return (lon + 180.0) / 360.0 * 256.0 * (double)(1UL << z); }
static double latToPx(double lat, int z) {
  double r = lat * DEG_TO_RAD;
  return (1.0 - log(tan(r) + 1.0 / cos(r)) / PI) / 2.0 * 256.0 * (double)(1UL << z);
}
static double metersPerPx(double lat, int z) { return 156543.03392 * cos(lat * DEG_TO_RAD) / (double)(1UL << z); }

static bool listening() { return radioMode == RM_MESH; }

static bool ensureListening() {
  if (!loraPresent && !loraInit()) {
    toast("Cap LoRa-1262 not detected", col::BAD, 1500);
    return false;
  }
  if (listening()) return true;
  if (!confirmBox("Switch the radio to Meshtastic listening? (CapDeck LoRa chat pauses)", col::MESH)) return false;
  cfg.meshListen = true;
  saveSettings();
  loraSetMode(RM_MESH);
  return true;
}

static uint32_t ageMs(const MeshNode &n) {
  if (n.lastMs) return millis() - n.lastMs;
  if (n.lastHeard && timeValid()) return (uint32_t)(time(nullptr) - n.lastHeard) * 1000UL;
  return UINT32_MAX;
}

static String nodeLabel(const MeshNode &n) {
  if (n.shortName.length()) return n.shortName;
  char b[8];
  snprintf(b, sizeof b, "%04lx", (unsigned long)(n.num & 0xFFFF));
  return b;
}

static void nodeDetail(const MeshNode &n) {
  char id[12];
  snprintf(id, sizeof id, "!%08lx", (unsigned long)n.num);
  String s = (n.longName.length() ? n.longName : String("(name not heard yet)")) + "\n";
  s += "Short: " + (n.shortName.length() ? n.shortName : String("-")) + "   ID: " + id + "\n\n";
  uint32_t age = ageMs(n);
  s += "Last heard: " + (age == UINT32_MAX ? String("earlier session") : fmtAge(age) + " ago") + "\n";
  if (n.lastMs) s += "Signal: " + String((int)n.rssi) + " dBm  SNR " + String(n.snr, 1) + "  hops " + String(n.hops) + "\n";
  s += "Packets this session: " + String(n.packets) + "\n\n";
  if (n.hasPos) {
    s += "Position: " + String(n.lat, 5) + ", " + String(n.lon, 5) + "\n";
    s += "Altitude: " + String((int)(n.alt * 3.28084)) + " ft\n";
    double la, lo;
    if (myPosition(la, lo)) {
      double d = distanceM(la, lo, n.lat, n.lon);
      s += "From you: " + fmtDistance(d) + "  " + String(TinyGPSPlus::cardinal(bearingDeg(la, lo, n.lat, n.lon))) + " (" +
           String((int)bearingDeg(la, lo, n.lat, n.lon)) + " deg)\n";
    }
  } else {
    s += "No position shared. (Meshtastic only broadcasts location when the owner opts in.)\n";
  }
  textViewer(n.shortName.length() ? n.shortName.c_str() : id, s, col::MESH);
}

// ------------------------------------------------------------------ map
static bool drawTiles(LovyanGFX &g, double cpx, double cpy, int z, int ox, int oy, int w, int h) {
  if (!sdOK) return false;
  bool any = false;
  int x0 = (int)floor((cpx - w / 2.0) / 256.0), x1 = (int)floor((cpx + w / 2.0) / 256.0);
  int y0 = (int)floor((cpy - h / 2.0) / 256.0), y1 = (int)floor((cpy + h / 2.0) / 256.0);
  for (int tx = x0; tx <= x1; tx++)
    for (int ty = y0; ty <= y1; ty++) {
      int sx = ox + (int)lround(tx * 256.0 - (cpx - w / 2.0));
      int sy = oy + (int)lround(ty * 256.0 - (cpy - h / 2.0));
      char path[48];
      snprintf(path, sizeof path, "/capdeck/map/%d/%d/%d.png", z, tx, ty);
      if (SD.exists(path)) { g.drawPngFile(SD, path, sx, sy); any = true; continue; }
      snprintf(path, sizeof path, "/capdeck/map/%d/%d/%d.jpg", z, tx, ty);
      if (SD.exists(path)) { g.drawJpgFile(SD, path, sx, sy); any = true; }
    }
  return any;
}

static void mapScreen(int focusNum = -1) {
  const int mapY = BAR_H, mapH = SCR_H - BAR_H;
  int z = 14;
  double cLat = 0, cLon = 0;
  bool haveCenter = myPosition(cLat, cLon);
  bool followMe = haveCenter;
  int sel = -1;  // index into meshNodes of the selected node
  if (focusNum >= 0)
    for (size_t i = 0; i < meshNodes.size(); i++)
      if ((int)meshNodes[i].num == focusNum) sel = i;
  if (!haveCenter) {  // no GPS ever: centre on the first node with a position
    for (size_t i = 0; i < meshNodes.size(); i++)
      if (meshNodes[i].hasPos) { cLat = meshNodes[i].lat; cLon = meshNodes[i].lon; haveCenter = true; if (sel < 0) sel = i; break; }
  }
  if (sel >= 0 && meshNodes[sel].hasPos) { cLat = meshNodes[sel].lat; cLon = meshNodes[sel].lon; followMe = false; }

  M5Canvas bg(&M5Cardputer.Display);
  bg.setColorDepth(16);
  bool haveBg = bg.createSprite(SCR_W, mapH) != nullptr;
  bool bgDirty = true, tiles = false;
  double bgLat = 0, bgLon = 0;
  int bgZ = -1;
  uint32_t lastDraw = 0;
  int frame = 0;

  while (true) {
    Key k = readKey();
    if (k.back) break;
    if (k.left && z > 3) { z--; bgDirty = true; }
    if (k.right && z < 18) { z++; bgDirty = true; }
    std::vector<int> withPos;
    for (size_t i = 0; i < meshNodes.size(); i++)
      if (meshNodes[i].hasPos) withPos.push_back(i);
    if ((k.up || k.down) && !withPos.empty()) {
      int cur = -1;
      for (size_t i = 0; i < withPos.size(); i++)
        if (withPos[i] == sel) cur = i;
      cur = k.down ? (cur + 1) % withPos.size() : (cur <= 0 ? withPos.size() - 1 : cur - 1);
      sel = withPos[cur];
      cLat = meshNodes[sel].lat;
      cLon = meshNodes[sel].lon;
      followMe = false;
      bgDirty = true;
    }
    if (k.ch == 'm' || k.ch == 'c') {  // back to me
      double la, lo;
      if (myPosition(la, lo)) { cLat = la; cLon = lo; followMe = true; sel = -1; bgDirty = true; }
      else toast("No GPS position yet", col::WARN, 900);
    }
    if (k.enter && sel >= 0) { nodeDetail(meshNodes[sel]); bgDirty = true; }
    if (followMe) {
      double la, lo;
      if (myPosition(la, lo) && (fabs(la - cLat) > 1e-5 || fabs(lo - cLon) > 1e-5)) { cLat = la; cLon = lo; bgDirty = true; }
    }
    if (millis() - lastDraw < 150 && !k.any) { delay(5); continue; }
    lastDraw = millis();
    frame++;

    double cpx = lonToPx(cLon, z), cpy = latToPx(cLat, z);
    int cx = SCR_W / 2, cy = mapY + mapH / 2;
    // ---- background (tiles or grid), cached until the view moves
    if (haveBg && (bgDirty || bgZ != z || bgLat != cLat || bgLon != cLon)) {
      bg.fillScreen(col::BG);
      tiles = haveCenter && drawTiles(bg, cpx, cpy, z, 0, 0, SCR_W, mapH);
      if (!tiles) {
        for (int x = (int)fmod(-cpx, 32.0) + 32 * 4; x < SCR_W + 32 * 4; x += 32) bg.drawFastVLine(x - 32 * 4, 0, mapH, col::PANEL);
        for (int y = (int)fmod(-cpy, 32.0) + 32 * 4; y < mapH + 32 * 4; y += 32) bg.drawFastHLine(0, y - 32 * 4, SCR_W, col::PANEL);
      }
      bgDirty = false;
      bgZ = z;
      bgLat = cLat;
      bgLon = cLon;
    }
    if (haveBg) bg.pushSprite(&canvas, 0, mapY);
    else canvas.fillRect(0, mapY, SCR_W, mapH, col::BG);
    drawStatusBar("MESH MAP", col::MESH);

    if (!haveCenter) {
      canvas.setFont(&fonts::Font2);
      canvas.setTextDatum(middle_center);
      canvas.setTextColor(col::TEXT);
      canvas.drawString("No positions yet", cx, cy - 10);
      canvas.setFont(&fonts::Font0);
      canvas.setTextColor(col::DIM);
      canvas.drawString(listening() ? "listening... get a GPS fix or wait for nodes" : "Meshtastic listening is OFF", cx, cy + 8);
      pushFrame();
      haveCenter = myPosition(cLat, cLon);
      if (haveCenter) followMe = true;
      continue;
    }

    // ---- range rings around me
    double mla, mlo;
    bool me = myPosition(mla, mlo);
    double mpp = metersPerPx(cLat, z);
    // choose a ring step that gives ~40px rings (1-2-5 in miles/feet)
    const double steps[] = {30.48, 76.2, 152.4, 402.3, 804.7, 1609.3, 3218.7, 8046.7, 16093.4, 32186.9, 80467.2, 160934.4};
    double ring = steps[0];
    for (double s : steps)
      if (s / mpp <= 48) ring = s;
    int mx = cx, my = cy;
    if (me) {
      mx = cx + (int)lround(lonToPx(mlo, z) - cpx);
      my = cy + (int)lround(latToPx(mla, z) - cpy);
      for (int r = 1; r <= 4; r++) canvas.drawCircle(mx, my, (int)(ring * r / mpp), tiles ? col::DIM : col::FAINT);
    }
    // ---- nodes
    canvas.setFont(&fonts::Font0);
    for (size_t i = 0; i < meshNodes.size(); i++) {
      MeshNode &n = meshNodes[i];
      if (!n.hasPos) continue;
      int x = cx + (int)lround(lonToPx(n.lon, z) - cpx);
      int y = cy + (int)lround(latToPx(n.lat, z) - cpy);
      if (x < -20 || x > SCR_W + 20 || y < mapY - 10 || y > SCR_H + 10) continue;
      uint32_t a = ageMs(n);
      bool fresh = a < 15UL * 60 * 1000;
      uint16_t c = (int)i == sel ? col::TEXT : fresh ? col::MESH : col::DIM;
      if ((int)i == sel) canvas.drawCircle(x, y, 6 + frame % 4, col::MESH);
      canvas.fillCircle(x, y, 3, c);
      canvas.drawCircle(x, y, 4, col::BG);
      canvas.setTextDatum(bottom_center);
      String lab = nodeLabel(n);
      int tw = canvas.textWidth(lab);
      canvas.fillRect(x - tw / 2 - 1, y - 14, tw + 2, 9, col::BG);
      canvas.setTextColor(c);
      canvas.drawString(lab, x, y - 6);
    }
    // ---- me marker
    if (me && mx > -5 && mx < SCR_W + 5 && my > mapY - 5 && my < SCR_H + 5) {
      canvas.fillTriangle(mx, my - 6, mx - 5, my + 5, mx + 5, my + 5, gpsFix() ? col::GPS : col::DIM);
      canvas.drawTriangle(mx, my - 6, mx - 5, my + 5, mx + 5, my + 5, col::BG);
    }
    // ---- north arrow + scale
    canvas.fillTriangle(SCR_W - 10, mapY + 4, SCR_W - 14, mapY + 13, SCR_W - 6, mapY + 13, col::TEXT);
    canvas.setTextDatum(top_center);
    canvas.setTextColor(col::TEXT);
    canvas.drawString("N", SCR_W - 10, mapY + 15);
    int sl = (int)(ring / mpp);
    canvas.fillRect(4, SCR_H - 12, sl + 4, 10, col::BG);
    canvas.drawFastHLine(6, SCR_H - 5, sl, col::TEXT);
    canvas.drawFastVLine(6, SCR_H - 8, 4, col::TEXT);
    canvas.drawFastVLine(6 + sl, SCR_H - 8, 4, col::TEXT);
    canvas.setTextDatum(bottom_left);
    canvas.drawString(fmtDistance(ring), 8, SCR_H - 6);
    // ---- info strip
    canvas.setTextDatum(top_left);
    String info;
    if (sel >= 0) {
      MeshNode &n = meshNodes[sel];
      info = nodeLabel(n);
      if (me) info += "  " + fmtDistance(distanceM(mla, mlo, n.lat, n.lon)) + " " + TinyGPSPlus::cardinal(bearingDeg(mla, mlo, n.lat, n.lon));
      uint32_t a = ageMs(n);
      if (a != UINT32_MAX) info += "  " + fmtAge(a);
    } else {
      info = String(withPos.size()) + "/" + String(meshNodes.size()) + " nodes on map  z" + String(z);
    }
    int iw = canvas.textWidth(info);
    canvas.fillRect(2, mapY + 2, iw + 6, 11, col::BG);
    canvas.setTextColor(sel >= 0 ? col::MESH : col::DIM);
    canvas.drawString(info, 5, mapY + 4);
    if (!listening()) {
      canvas.setTextDatum(bottom_right);
      canvas.setTextColor(col::WARN);
      canvas.drawString("listening OFF", SCR_W - 4, SCR_H - 3);
    }
    pushFrame();
  }
  if (haveBg) bg.deleteSprite();
}

// ------------------------------------------------------------------ lists
static void nodeList() {
  int sel = 0;
  while (true) {
    std::vector<int> order;
    for (size_t i = 0; i < meshNodes.size(); i++) order.push_back(i);
    std::sort(order.begin(), order.end(), [](int a, int b) { return ageMs(meshNodes[a]) < ageMs(meshNodes[b]); });
    double la, lo;
    bool me = myPosition(la, lo);
    std::vector<ListItem> items;
    for (int i : order) {
      MeshNode &n = meshNodes[i];
      String right;
      if (n.hasPos && me) right = fmtDistance(distanceM(la, lo, n.lat, n.lon));
      uint32_t a = ageMs(n);
      if (a != UINT32_MAX) right += (right.length() ? " " : "") + fmtAge(a);
      String label = nodeLabel(n) + (n.longName.length() ? "  " + n.longName : "");
      items.push_back({label, right, n.hasPos ? ICON_MAP : ICON_MESH, a < 15UL * 60 * 1000 ? col::MESH : col::DIM});
    }
    int r = listMenu("Nodes heard", col::MESH, items, sel, ICON_MESH);
    if (r < 0) return;
    MeshNode &n = meshNodes[order[r]];
    if (n.hasPos) {
      std::vector<ListItem> acts = {{"Show on map", "", ICON_MAP, 0}, {"Details", "", ICON_INFO, 0}};
      int s2 = 0;
      int a = listMenu(nodeLabel(n).c_str(), col::MESH, acts, s2, ICON_MESH);
      if (a == 0) mapScreen(n.num);
      else if (a == 1) nodeDetail(n);
    } else {
      nodeDetail(n);
    }
  }
}

static void messages() {
  size_t seen = SIZE_MAX;
  while (true) {
    Key k = readKey();
    if (k.back || k.enter) return;
    if (seen == meshTexts.size()) { delay(20); continue; }
    seen = meshTexts.size();
    std::vector<std::pair<String, bool>> chat;
    for (auto &m : meshTexts) chat.push_back({meshName(m.from) + ": " + m.text, false});
    if (chat.empty()) chat.push_back({"No public messages yet. Texts on the default channel show up here (and in /capdeck/mesh/messages.txt).", false});
    drawChatView("Mesh messages", col::MESH, ICON_MESH, chat, 0, nullptr, listening() ? "listening" : "OFF",
                 listening() ? col::OK : col::WARN);
    pushFrame();
  }
}

static const char *MESH_HELP =
    "CapDeck listens to Meshtastic on the US default frequency for the chosen modem preset "
    "(LongFast = 906.875 MHz, what most US meshes use). It is receive-only: it never transmits into the mesh.\n\n"
    "Packets on the public default channel are decrypted with its well-known key, so you see node "
    "names, positions and public texts. Private channels and direct messages only show who sent them.\n\n"
    "Positions: newer Meshtastic firmware does NOT share location unless the owner turns it on, so "
    "plenty of nodes appear in the list without a dot on the map.\n\n"
    "MAP keys: , / zoom   ; . jump between nodes   ENTER details   M back to me   DEL exit\n\n"
    "Offline map tiles (optional): put standard z/x/y tiles in /capdeck/map/<z>/<x>/<y>.png (or .jpg), "
    "e.g. exported for your area with an atlas tool. Without tiles you get a grid with range rings.\n\n"
    "While listening, CapDeck LoRa chat is paused - switch back in this menu.";

void appMesh() {
  int sel = 0;
  while (true) {
    int withPos = 0;
    for (auto &n : meshNodes) withPos += n.hasPos;
    std::vector<ListItem> items = {
        {"Map", String(withPos) + " placed", ICON_MAP, 0},
        {"Nodes heard", String(meshNodes.size()), ICON_MESH, 0},
        {"Public messages", String(meshTexts.size()), ICON_INFO, 0},
        {"Listening", listening() ? "ON" : "off", ICON_LORA, listening() ? col::OK : col::WARN},
        {"Preset", MESH_PRESETS[cfg.meshPreset % MESH_PRESET_COUNT].name, ICON_CONF, 0},
        {"Stats", String(meshPackets) + " pkts", ICON_INFO, 0},
        {"Help", "", ICON_INFO, 0},
    };
    int r = listMenu("Meshtastic", col::MESH, items, sel, ICON_MESH);
    if (r < 0) return;
    switch (r) {
      case 0: if (listening() || !meshNodes.empty() || confirmBox("Listening is off. Open the map anyway?", col::MESH)) mapScreen(); break;
      case 1: nodeList(); break;
      case 2: messages(); break;
      case 3:
        if (listening()) {
          cfg.meshListen = false;
          saveSettings();
          loraSetMode(RM_CAPDECK);
          toast("Radio back on CapDeck chat", col::LORA, 900);
        } else ensureListening();
        break;
      case 4:
        cfg.meshPreset = (cfg.meshPreset + 1) % MESH_PRESET_COUNT;
        saveSettings();
        if (listening()) loraSetMode(RM_MESH);
        break;
      case 5: {
        const MeshPreset &p = MESH_PRESETS[cfg.meshPreset % MESH_PRESET_COUNT];
        String s = "Preset: " + String(p.name) + "\nFrequency: " + String(p.freq, 4) + " MHz\nBW " + String(p.bw, 0) +
                   " kHz  SF" + String(p.sf) + "  CR 4/" + String(p.cr) + "\n\nPackets: " + String(meshPackets) +
                   "\nNot decodable (private/DM): " + String(meshUndecoded) + "\nNodes: " + String(meshNodes.size()) +
                   "\nWith position: " + String(withPos);
        textViewer("Mesh stats", s, col::MESH);
        break;
      }
      case 6: textViewer("Mesh help", MESH_HELP, col::MESH); break;
    }
  }
}
