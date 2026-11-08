// CapDeck - Mesh: passive Meshtastic node finder with a GPS-centred map
#include "core.h"
#include <TinyGPSPlus.h>
#include <RadioLib.h>
#include <algorithm>

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

void meshNodeDetail(const MeshNode &n) {
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

// ------------------------------------------------------------------ map: the shared Map app, nodes layer in focus
extern int mapFocusNode;
static void mapScreen(int focusNum = -1) {
  mapFocusNode = focusNum;
  appMap(1);
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
      else if (a == 1) meshNodeDetail(n);
    } else {
      meshNodeDetail(n);
    }
  }
}

static void messages() {
  String input;
  bool canSend = cfg.meshTx && listening();
  size_t seen = SIZE_MAX;
  bool dirty = true;
  uint32_t blink = 0;
  while (true) {
    Key k = readKey();
    if (k.any) dirty = true;
    if (!canSend) {
      if (k.back || k.enter) return;
    } else {
      if (k.esc || (k.del && !input.length())) return;
      if (k.del && input.length()) input.remove(input.length() - 1);
      else if (k.enter && input.length()) {
        String preset = MESH_PRESETS[cfg.meshPreset % MESH_PRESET_COUNT].name;
        bool ok = meshSendText(preset, input);
        // show our own sent line locally
        meshTexts.push_back({meshMyNum(), input, (uint32_t)millis(), preset});
        if (meshTexts.size() > 40) meshTexts.erase(meshTexts.begin());
        input = "";
        beep(ok ? 2600 : 500, ok ? 15 : 150);
        if (!ok) toast("Send failed", col::BAD, 900);
      } else if (k.space) input += ' ';
      else if (k.ch && !k.fn && input.length() < 200) input += k.ch;
    }
    if (millis() - blink > 450) { blink = millis(); dirty = true; }
    if (seen != meshTexts.size()) { seen = meshTexts.size(); dirty = true; }
    if (!dirty) { delay(10); continue; }
    dirty = false;
    std::vector<std::pair<String, bool>> chat;
    for (auto &m : meshTexts) chat.push_back({"#" + m.channel + " " + meshName(m.from) + ": " + m.text, m.from == meshMyNum()});
    if (chat.empty())
      chat.push_back({canSend ? "No public messages yet. Type below to send one, or wait to hear others."
                              : "No public messages yet. Texts on the default channel show up here.",
                      false});
    drawChatView("Mesh messages", col::MESH, ICON_MESH, chat, 0, canSend ? &input : nullptr,
                 listening() ? (canSend ? "send on" : "listening") : "OFF", listening() ? col::OK : col::WARN);
    pushFrame();
  }
}

static const char *MESH_TX_WARN =
    "Turn on sending to the mesh?\n\n"
    "CapDeck will send YOUR messages and node info, and NEVER relay anyone else's traffic (the "
    "\"Client Mute\" role) - so it can't disrupt the mesh.\n\n"
    "This is new and NOT yet hardware-tested. Please try it on your own private channel first and watch a "
    "real Meshtastic node to confirm your messages arrive cleanly before using the public mesh.";

static void identity() {
  int sel = 0;
  while (true) {
    char idn[12];
    snprintf(idn, sizeof idn, "!%08lx", (unsigned long)meshMyNum());
    std::vector<ListItem> items = {
        {"Node ID", idn, ICON_MESH, 0},
        {"Short name", cfg.meshShort.length() ? cfg.meshShort : "(auto)", ICON_CONF, 0},
        {"Long name", cfg.meshLong.length() ? cfg.meshLong : "(auto)", ICON_CONF, 0},
        {"Broadcast my node info now", "", ICON_MESH, col::MESH},
        {"Share my location once", "", ICON_GPS, col::GPS},
    };
    int r = listMenu("My mesh identity", col::MESH, items, sel, ICON_MESH);
    if (r < 0) return;
    if (r == 1) { if (textInput("Short name (<=4 chars)", cfg.meshShort, col::MESH, false, 4)) saveSettings(); }
    else if (r == 2) { if (textInput("Long name", cfg.meshLong, col::MESH, false, 20)) saveSettings(); }
    else if (r == 3) {
      if (!cfg.meshTx) { toast("Turn on Sending first", col::WARN, 1200); continue; }
      toast(meshSendNodeInfo() ? "Sent node info" : "Send failed", col::MESH, 900);
    } else if (r == 4) {
      if (!cfg.meshTx) { toast("Turn on Sending first", col::WARN, 1200); continue; }
      if (confirmBox("Broadcast your current GPS position to the mesh once?", col::GPS))
        toast(meshSendPosition() ? "Position sent" : "No fix / send failed", col::GPS, 1000);
    }
  }
}

// Cycle channel-activity-detection over every preset to find where nearby nodes are talking.
static void findMesh() {
  if (!loraPresent && !loraInit()) { toast("Cap LoRa-1262 not detected", col::BAD, 1500); return; }
  SX1262 *r = loraRadio();
  loraSetMode(RM_SCAN);
  r->standby();
  r->calibrateImage(bandCenter());
  std::vector<uint32_t> hits(MESH_PRESET_COUNT, 0), lastHit(MESH_PRESET_COUNT, 0);
  int pi = 0, best = -1;
  uint32_t start = millis(), lastDraw = 0, rounds = 0;
  bool chosen = false;
  while (true) {
    Key k = readKey();
    if (k.back) break;
    if (k.enter && best >= 0) { chosen = true; break; }
    const MeshPreset &p = MESH_PRESETS[pi];
    if (!meshPresetAllowed(p)) { if (++pi >= MESH_PRESET_COUNT) { pi = 0; rounds++; } continue; }
    r->standby();
    r->setBandwidth(p.bw);
    r->setSpreadingFactor(p.sf);
    r->setFrequency(meshFreq(p), true);
    int st = r->scanChannel();
    if (st == RADIOLIB_LORA_DETECTED || st == RADIOLIB_PREAMBLE_DETECTED) {
      hits[pi]++;
      lastHit[pi] = millis();
      beep(2900, 10);
    }
    if (++pi >= MESH_PRESET_COUNT) { pi = 0; rounds++; }
    best = -1;
    for (int i = 0; i < MESH_PRESET_COUNT; i++)
      if (hits[i] && (best < 0 || hits[i] > hits[best])) best = i;
    if (millis() - lastDraw < 150) continue;
    lastDraw = millis();
    drawHeader("Find the mesh", col::MESH, ICON_SCAN);
    canvas.setFont(&fonts::Font0);
    canvas.setTextDatum(middle_right);
    canvas.setTextColor(col::DIM);
    canvas.drawString(fmtAge(millis() - start), SCR_W - 6, BAR_H + 9);
    for (int i = 0; i < MESH_PRESET_COUNT; i++) {
      int y = BAR_H + 21 + i * 9;
      bool hot = lastHit[i] && millis() - lastHit[i] < 1500;
      canvas.setTextDatum(top_left);
      canvas.setTextColor(i == best ? col::MESH : hits[i] ? col::TEXT : col::DIM);
      canvas.drawString(MESH_PRESETS[i].name, 6, y);
      canvas.drawString(meshPresetAllowed(MESH_PRESETS[i]) ? String(meshFreq(MESH_PRESETS[i]), 3) : String("not in EU"), 82, y);
      int bw = min(80, (int)hits[i] * 4);
      canvas.fillRect(140, y + 1, bw, 6, hot ? col::OK : col::MESH);
      canvas.setTextDatum(top_right);
      canvas.drawString(String(hits[i]), SCR_W - 4, y);
    }
    canvas.setTextDatum(bottom_center);
    canvas.setTextColor(best >= 0 ? col::OK : col::FAINT);
    canvas.drawString(best >= 0 ? "ENTER: listen on " + String(MESH_PRESETS[best].name) : String("listening on all presets... DEL stops"),
                      SCR_W / 2, SCR_H);
    pushFrame();
  }
  if (chosen) {
    cfg.meshPreset = best;
    cfg.meshListen = true;
    saveSettings();
    meshBuildChannels();
    loraSetMode(RM_MESH);
    toast(String("Listening on ") + MESH_PRESETS[best].name + ". Names and positions arrive as nodes broadcast them (can take a while).", col::OK, 2500);
  } else {
    loraSetMode(cfg.meshListen ? RM_MESH : RM_CAPDECK);
  }
}

static const char *MESH_HELP =
    "CapDeck listens to Meshtastic on the US default frequency for the chosen modem preset "
    "(LongFast = 906.875 MHz, what most US meshes use). Sending is OFF by default; when you turn it on, CapDeck "
    "is a Client-Mute node: it sends only its own messages and never relays anyone else's.\n\n"
    "Packets on the public default channel are decrypted with its well-known key, so you see node "
    "names, positions and public texts. The Michigan Mesh community channels (Michigan, WMI, EMI, Muskegon, "
    "NMI, YOOPER, Washtenaw) are built in too, and you can add more under \"mesh_channels\" in config.json. "
    "Private channels and direct messages only show who sent them.\n\n"
    "FIND THE MESH listens across every preset and shows where LoRa traffic is - press ENTER to lock on.\n\n"
    "Positions: newer Meshtastic firmware does NOT share location unless the owner turns it on, so "
    "plenty of nodes appear in the list without a dot on the map.\n\n"
    "MAP keys: arrows (Fn + ; . , /) pan   - = zoom   TAB next node   ENTER details   L layers   M me   DEL exit\n\n"
    "Roads: download a city's major roads in Mycorrhiza > Map regions; they draw as mycelium threads. Optional "
    "picture tiles in /capdeck/map/<z>/<x>/<y>.png (or .jpg) still work as a layer.\n\n"
    "While listening, CapDeck LoRa chat is paused - switch back in this menu.";

// When the radio isn't listening, say so once - and let one key fix it (no more digging for the toggle).
static int asleepPopup() {
  canvas.setFont(&fonts::Font2);
  int y = 34, h = 74;
  canvas.fillRoundRect(16, y + 3, SCR_W - 32, h, 6, 0x0000);
  canvas.fillRoundRect(14, y, SCR_W - 32, h, 6, col::PANEL2);
  canvas.drawRoundRect(14, y, SCR_W - 32, h, 6, col::MESH);
  drawIcon(canvas, ICON_MESH, 40, y + 30, 32, col::MESH, col::PANEL2);
  canvas.setTextDatum(top_left);
  canvas.setTextColor(col::TEXT);
  canvas.drawString("The mycelium is quiet", 64, y + 10);
  canvas.setTextColor(col::DIM);
  canvas.drawString("Listening is off.", 64, y + 28);
  canvas.setFont(&fonts::Font0);
  canvas.setTextColor(col::MESH);
  canvas.drawString("ENTER listen   M map   DEL menu", 22, y + h - 14);
  pushFrame();
  while (true) {
    Key k = readKey();
    if (k.enter) return 1;
    if (k.ch == 'm' || k.ch == 'M') return 2;
    if (k.back) return 0;
    delay(10);
  }
}

void appMesh() {
  if (loraPresent && !listening()) {
    int a = asleepPopup();
    if (a == 1) { cfg.meshListen = true; saveSettings(); loraSetMode(RM_MESH); toast("Listening", col::MESH, 700); }
    if (a == 2) mapScreen();
  }
  enum Act { A_MAP, A_LISTEN, A_NODES, A_MSGS, A_SEND, A_ID, A_FIND, A_PRESET, A_STATS, A_HELP };
  int sel = 0;
  while (true) {
    int withPos = 0;
    for (auto &n : meshNodes) withPos += n.hasPos;
    std::vector<ListItem> items = {
        {"Map", String(withPos) + " placed", ICON_MAP, 0},
        {"Listening", listening() ? "ON" : "off", ICON_LORA, listening() ? col::OK : col::WARN},
        {"Nodes heard", String(meshNodes.size()), ICON_MESH, 0},
        {"Public messages", String(meshTexts.size()), ICON_INFO, 0},
        {"Sending", cfg.meshTx ? "ON (Client Mute)" : "off", ICON_MESH, cfg.meshTx ? col::OK : col::DIM},
        {"My identity", cfg.meshShort.length() ? cfg.meshShort : "set name", ICON_CONF, 0},
        {"Find the mesh", "auto", ICON_SCAN, 0},
        {"Preset", MESH_PRESETS[cfg.meshPreset % MESH_PRESET_COUNT].name, ICON_CONF, 0},
        {"Stats", String(meshPackets) + " pkts", ICON_INFO, 0},
        {"Help", "", ICON_INFO, 0},
    };
    int r = listMenu("Meshtastic", col::MESH, items, sel, ICON_MESH);
    if (r < 0) return;
    switch ((Act)r) {
      case A_MAP: mapScreen(); break;
      case A_NODES: nodeList(); break;
      case A_MSGS: messages(); break;
      case A_LISTEN:
        if (listening()) {
          cfg.meshListen = false;
          cfg.meshTx = false;
          saveSettings();
          loraSetMode(RM_CAPDECK);
          toast("Radio back on CapDeck chat", col::LORA, 900);
        } else ensureListening();
        break;
      case A_SEND:
        if (cfg.meshTx) {
          cfg.meshTx = false;
          saveSettings();
          toast("Sending off - listen only", col::DIM, 900);
        } else if (ensureListening() && confirmBox(MESH_TX_WARN, col::WARN)) {
          cfg.meshTx = true;
          saveSettings();
          meshSendNodeInfo();  // announce ourselves
          toast("Sending on. You're a Client-Mute node - test on a private channel first!", col::OK, 2600);
        }
        break;
      case A_ID: identity(); break;
      case A_FIND: findMesh(); break;
      case A_PRESET:
        do cfg.meshPreset = (cfg.meshPreset + 1) % MESH_PRESET_COUNT;
        while (!meshPresetAllowed(MESH_PRESETS[cfg.meshPreset]));
        saveSettings();
        meshBuildChannels();
        if (listening()) loraSetMode(RM_MESH);
        break;
      case A_STATS: {
        const MeshPreset &p = MESH_PRESETS[cfg.meshPreset % MESH_PRESET_COUNT];
        String st = "Preset: " + String(p.name) + "\nFrequency: " + String(meshFreq(p), 4) + " MHz\nBW " + String(p.bw, 0) +
                    " kHz  SF" + String(p.sf) + "  CR 4/" + String(p.cr) + "\n\nPackets: " + String(meshPackets) +
                    "\nNot decodable (private/DM): " + String(meshUndecoded) + "\nNodes: " + String(meshNodes.size()) +
                    "\nWith position: " + String(withPos);
        textViewer("Mesh stats", st, col::MESH);
        break;
      }
      case A_HELP: textViewer("Mesh help", MESH_HELP, col::MESH); break;
    }
  }
}
