// CapDeck - Forage: collecting spores. A passive foray that logs what's in the air around you -
// Wi-Fi networks, Bluetooth devices, and Meshtastic nodes/packets - and pins the ones with a
// position to the Map. Nothing is transmitted; it only listens.
//   Spore         a LoRa / mesh packet            Fairy ring   a Wi-Fi network
//   Fruiting body a Meshtastic node               Death cap    a hidden Wi-Fi network
//   Puffball      a Bluetooth device              Cordyceps    a LoRaWAN device (via the LoRa hunter)
//   Morel         a rare find (right next to you, or a mesh node new to your lifetime log)
// Export a "Spore print" (WiGLE 1.4 CSV) to map your foray on wigle.net.
#include "core.h"
#include <WiFi.h>
#include <Preferences.h>
#if __has_include(<BLEDevice.h>)
#include <BLEDevice.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>
#define HAVE_BLE 1
#endif

std::vector<Find> forageFinds;
bool forageRunning = false;

uint16_t findColour_pub(FindKind k);   // palette mirror, defined below
static void forageStop_toMap();        // M key: end the foray and open the map

static const char *KIND_NAME[FK_KIND_COUNT] = {"Spore", "Fruiting body", "Puffball", "Fairy ring", "Death cap", "Morel", "Cordyceps"};
static const char *KIND_PLAIN[FK_KIND_COUNT] = {"LoRa/mesh packet", "Meshtastic node", "Bluetooth device", "Wi-Fi network", "hidden Wi-Fi", "rare find", "LoRaWAN device"};
const char *findName(FindKind k) { return k < FK_KIND_COUNT ? KIND_NAME[k] : "?"; }
const char *findPlain(FindKind k) { return k < FK_KIND_COUNT ? KIND_PLAIN[k] : "?"; }

static uint32_t lifetime[FK_KIND_COUNT];
static bool lifeLoaded = false;
static uint32_t newThisForay = 0;
static String feed[6];  // rolling ticker of the last few finds
static int feedN = 0;

static void lifeLoad() {
  if (lifeLoaded) return;
  Preferences p;
  p.begin("forage", true);
  for (int i = 0; i < FK_KIND_COUNT; i++) { char kk[4]; snprintf(kk, sizeof kk, "c%d", i); lifetime[i] = p.getULong(kk, 0); }
  p.end();
  lifeLoaded = true;
}
static void lifeSave() {
  Preferences p;
  p.begin("forage", false);
  for (int i = 0; i < FK_KIND_COUNT; i++) { char kk[4]; snprintf(kk, sizeof kk, "c%d", i); p.putULong(kk, lifetime[i]); }
  p.end();
}

static void pushFeed(const String &s) {
  for (int i = 5; i > 0; i--) feed[i] = feed[i - 1];
  feed[0] = s;
  if (feedN < 6) feedN++;
}

// Find one we already have by MAC (wifi/ble); returns index or -1.
static int findByMac(const uint8_t *mac) {
  for (size_t i = 0; i < forageFinds.size(); i++)
    if (forageFinds[i].macLen == 6 && !memcmp(forageFinds[i].mac, mac, 6)) return i;
  return -1;
}

// Record / update a find. New finds bump the lifetime counter and the ticker.
static void logFind(FindKind kind, const String &label, const uint8_t *mac, int rssi, uint8_t channel, uint8_t auth) {
  bool rare = rssi >= -35;  // close enough to touch -> a Morel
  int idx = mac ? findByMac(mac) : -1;
  if (idx >= 0) {  // update
    Find &f = forageFinds[idx];
    f.rssi = rssi;
    f.lastMs = millis();
    f.hits++;
    double la, lo;
    if (!f.hasPos && myPosition(la, lo)) { f.hasPos = true; f.lat = la; f.lon = lo; }
    if (rare && f.kind != FK_MOREL) { f.kind = FK_MOREL; lifetime[FK_MOREL]++; pushFeed("Morel! " + label); }
    return;
  }
  Find f;
  f.kind = rare ? FK_MOREL : kind;
  f.label = label.length() ? label : String("(unnamed)");
  f.macLen = 0;
  if (mac) { memcpy(f.mac, mac, 6); f.macLen = 6; }
  f.rssi = rssi;
  f.channel = channel;
  f.auth = auth;
  f.hits = 1;
  f.firstMs = f.lastMs = millis();
  f.firstUnix = timeValid() ? time(nullptr) : 0;
  double la, lo;
  f.hasPos = myPosition(la, lo);
  f.lat = la; f.lon = lo;
  if (forageFinds.size() < 2000) forageFinds.push_back(f);
  lifetime[f.kind]++;
  newThisForay++;
  pushFeed(String(findName(f.kind)) + ": " + f.label);
  beep(rare ? 3200 : 2600, 6);
}

// ------------------------------------------------------------------ BLE
#ifdef HAVE_BLE
// The scan callback runs on the Bluetooth task, so it only queues; the main loop logs the find.
struct Puff { uint8_t mac[6]; int8_t rssi; char name[24]; };
static QueueHandle_t puffQ = nullptr;
class PuffCB : public BLEAdvertisedDeviceCallbacks {
  void onResult(BLEAdvertisedDevice d) override {
    if (!puffQ) return;
    Puff p;
    uint8_t *m = d.getAddress().getNative();
    for (int i = 0; i < 6; i++) p.mac[i] = m[5 - i];  // NimBLE stores little-endian
    p.rssi = d.getRSSI();
    String name = d.haveName() ? d.getName() : String("");
    strlcpy(p.name, name.c_str(), sizeof p.name);
    xQueueSend(puffQ, &p, 0);  // drop if full rather than block the BT stack
  }
};
static void drainPuffs() {
  Puff p;
  while (puffQ && xQueueReceive(puffQ, &p, 0) == pdTRUE) logFind(FK_PUFFBALL, String(p.name), p.mac, p.rssi, 0, 0);
}
static PuffCB puffCb;
static BLEScan *bleScan = nullptr;
static bool bleUp = false;
static bool bleStart() {
  if (bleUp) return true;
  if (!puffQ) puffQ = xQueueCreate(48, sizeof(Puff));
  if (!BLEDevice::getInitialized()) BLEDevice::init("");
  bleScan = BLEDevice::getScan();
  if (!bleScan) return false;
  bleScan->setActiveScan(false);  // passive: never probes, just listens
  bleScan->setInterval(160);
  bleScan->setWindow(48);
  bleScan->setAdvertisedDeviceCallbacks(&puffCb, true);
  bleScan->start(0, nullptr, false);  // continuous
  bleUp = true;
  return true;
}
static void bleStop() {
  if (!bleUp) return;
  bleScan->stop();
  bleScan->clearResults();
  BLEDevice::deinit(false);
  bleUp = false;
}
#else
static bool bleStart() { return false; }
static void bleStop() {}
static void drainPuffs() {}
#endif

// ------------------------------------------------------------------ mesh bridge
static std::vector<uint32_t> seenNodes;
static uint32_t lastMeshPackets = 0;
static void pollMesh() {
  // new mesh nodes -> fruiting bodies (Morel if brand new to the lifetime log... handled by rare rule on signal)
  for (auto &n : meshNodes) {
    bool known = false;
    for (auto s : seenNodes) if (s == n.num) { known = true; break; }
    if (known) continue;
    seenNodes.push_back(n.num);
    uint8_t mac[6] = {(uint8_t)(n.num >> 24), (uint8_t)(n.num >> 16), (uint8_t)(n.num >> 8), (uint8_t)n.num, 0xE5, 0x7A};
    Find f;
    f.kind = FK_FRUIT;
    f.label = n.longName.length() ? n.longName : (n.shortName.length() ? n.shortName : String("mesh node"));
    memcpy(f.mac, mac, 6); f.macLen = 6;
    f.rssi = (int)n.rssi; f.channel = 0; f.auth = 0; f.hits = n.packets;
    f.firstMs = f.lastMs = millis();
    f.firstUnix = timeValid() ? time(nullptr) : 0;
    f.hasPos = n.hasPos; f.lat = n.lat; f.lon = n.lon;
    forageFinds.push_back(f);
    lifetime[FK_FRUIT]++;
    newThisForay++;
    pushFeed("Fruiting body: " + f.label);
    beep(2600, 6);
  }
  // packets heard -> spores (counter only; a packet has no position)
  if (meshPackets > lastMeshPackets) {
    uint32_t d = meshPackets - lastMeshPackets;
    lastMeshPackets = meshPackets;
    lifetime[FK_SPORE] += d;
    newThisForay += d;
  }
}

// ------------------------------------------------------------------ WiGLE CSV export ("Spore print")
static String csvEsc(const String &s) {
  String o = s;
  o.replace("\"", "\"\"");
  if (o.indexOf(',') >= 0 || o.indexOf('"') >= 0) o = "\"" + o + "\"";
  return o;
}
static String stampOf(uint32_t unix) {
  if (!unix) return "1970-01-01 00:00:00";
  time_t t = unix; struct tm tmv; localtime_r(&t, &tmv);
  char b[24]; strftime(b, sizeof b, "%Y-%m-%d %H:%M:%S", &tmv); return b;
}
static void sporePrint() {
  if (!sdOK) { toast("No SD card", col::BAD, 1200); return; }
  int withPos = 0;
  for (auto &f : forageFinds) if (f.hasPos && f.macLen == 6) withPos++;
  if (!withPos) { toast("Nothing with a GPS position yet to export", col::WARN, 2000); return; }
  SD.mkdir("/capdeck"); SD.mkdir("/capdeck/forage");
  char path[64];
  snprintf(path, sizeof path, "/capdeck/forage/spore_print_%lu.csv", (unsigned long)(timeValid() ? time(nullptr) : millis() / 1000));
  File f = SD.open(path, FILE_WRITE);
  if (!f) { toast("Couldn't write the file", col::BAD, 1400); return; }
  f.println("WigleWifi-1.4,appRelease=" CAPDECK_VERSION ",model=Cardputer,release=" CAPDECK_VERSION ",device=CapDeck,display=,board=ESP32S3,brand=M5Stack");
  f.println("MAC,SSID,AuthMode,FirstSeen,Channel,RSSI,CurrentLatitude,CurrentLongitude,AltitudeMeters,AccuracyMeters,Type");
  for (auto &fd : forageFinds) {
    if (!fd.hasPos || fd.macLen != 6) continue;
    char mac[20]; snprintf(mac, sizeof mac, "%02X:%02X:%02X:%02X:%02X:%02X", fd.mac[0],fd.mac[1],fd.mac[2],fd.mac[3],fd.mac[4],fd.mac[5]);
    bool ble = fd.kind == FK_PUFFBALL;
    String auth = ble ? "" : (fd.kind == FK_DEATHCAP ? "[hidden]" : (fd.auth ? "[WPA2]" : "[OPEN]"));
    f.print(mac); f.print(',');
    f.print(csvEsc(fd.label)); f.print(',');
    f.print(auth); f.print(',');
    f.print(stampOf(fd.firstUnix)); f.print(',');
    f.print(fd.channel); f.print(',');
    f.print(fd.rssi); f.print(',');
    f.print(String(fd.lat, 6)); f.print(',');
    f.print(String(fd.lon, 6)); f.print(',');
    f.print("0,0,");
    f.println(ble ? "BLE" : "WIFI");
  }
  f.close();
  toast(String(withPos) + " logged to " + String(path).substring(16), col::OK, 2600);
}

// ------------------------------------------------------------------ UI
static const char *FORAGE_HELP =
    "A passive foray. CapDeck only listens - it never transmits here.\n\n"
    "WHAT YOU'LL FIND:\n"
    "Spore - a LoRa or mesh packet in the air\n"
    "Fruiting body - a Meshtastic node\n"
    "Puffball - a Bluetooth device\n"
    "Fairy ring - a Wi-Fi network\n"
    "Death cap - a hidden Wi-Fi network\n"
    "Cordyceps - a LoRaWAN device (seen in the LoRa hunter)\n"
    "Morel - a rare find: something right next to you, or a brand-new node\n\n"
    "Finds with a GPS position show on the Map as little mushrooms. Export a Spore print "
    "(WiGLE CSV) to map a whole foray on wigle.net. Turn on Mycelium listening to catch mesh nodes; "
    "a GPS fix tags finds with where you were.";

static void forageList() {
  int sel = 0;
  while (true) {
    std::vector<ListItem> items;
    for (auto &f : forageFinds) {
      String r = String(f.rssi) + "dBm" + (f.hasPos ? " *" : "");
      items.push_back({String(findName(f.kind)) + ": " + f.label, r, 255, 0});
    }
    if (items.empty()) items.push_back({"Nothing yet", "start a foray", 255, col::DIM});
    int r = listMenu(("Finds (" + String(forageFinds.size()) + ")").c_str(), col::GPS, items, sel, ICON_FORAGE);
    if (r < 0) return;
    if (forageFinds.empty()) return;
    Find &f = forageFinds[r];
    String s = f.label + "\n" + findName(f.kind) + " (" + findPlain(f.kind) + ")\n\n";
    if (f.macLen) { char m[20]; snprintf(m, sizeof m, "%02X:%02X:%02X:%02X:%02X:%02X", f.mac[0],f.mac[1],f.mac[2],f.mac[3],f.mac[4],f.mac[5]); s += String("MAC: ") + m + "\n"; }
    s += "Signal: " + String(f.rssi) + " dBm\n";
    if (f.channel) s += "Channel: " + String(f.channel) + "\n";
    s += "Seen: " + String(f.hits) + "x\n";
    s += f.hasPos ? "Position: " + String(f.lat, 5) + ", " + String(f.lon, 5) + "\n" : String("No position (no GPS fix when heard)\n");
    textViewer(f.label.c_str(), s, findColour_pub(f.kind));
  }
}

// small public colour accessor (map.cpp owns drawFindSymbol; mirror its palette here)
uint16_t findColour_pub(FindKind k) {
  switch (k) {
    case FK_SPORE: return col::LORA; case FK_FRUIT: return col::MESH; case FK_PUFFBALL: return col::PORTAL;
    case FK_FAIRY: return col::GPS; case FK_DEATHCAP: return col::BAD; case FK_MOREL: return col::FILES;
    case FK_CORDYCEPS: return col::RF; default: return col::TEXT;
  }
}

void appForage() {
  lifeLoad();
  bool ble = bleStart();
  bool wasMesh = (radioMode == RM_MESH);
  if (!wasMesh && loraPresent && cfg.meshListen) loraSetMode(RM_MESH);
  WiFi.scanDelete();
  int16_t scanState = -2;  // -2 idle, -1 running
  uint32_t lastScanKick = 0, lastBleClear = 0, lastDraw = 0;
  uint32_t t0 = millis();
  forageRunning = true;
  bool quit = false;

  while (!quit) {
    Key k = readKey();
    pollMesh();
    drainPuffs();

    // Wi-Fi: async passive scan, re-kicked when the last one finishes
    if (scanState != -1 && millis() - lastScanKick > 2500) {
      WiFi.scanDelete();
      WiFi.scanNetworks(true, true, true, 240);  // async, show_hidden, passive
      scanState = -1;
      lastScanKick = millis();
    }
    if (scanState == -1) {
      int16_t r = WiFi.scanComplete();
      if (r >= 0) {
        for (int i = 0; i < r; i++) {
          uint8_t *b = WiFi.BSSID(i);
          if (!b) continue;
          String ss = WiFi.SSID(i);
          bool hidden = ss.length() == 0;
          uint8_t auth = WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? 0 : 1;
          logFind(hidden ? FK_DEATHCAP : FK_FAIRY, hidden ? String("(hidden)") : ss, b, WiFi.RSSI(i), WiFi.channel(i), auth);
        }
        WiFi.scanDelete();
        scanState = -2;
      }
    }
#ifdef HAVE_BLE
    if (bleUp && millis() - lastBleClear > 30000) { bleScan->clearResults(); lastBleClear = millis(); }  // bound RAM, keep RSSI fresh
#endif

    if (k.back) quit = true;
    if (k.ch == 'l') { forageList(); lastDraw = 0; }
    if (k.ch == 'm') { forageStop_toMap(); return; }  // jump to the map (defined below via goto-free helper)
    if (k.ch == 'e') { sporePrint(); lastDraw = 0; }
    if (k.ch == 'h' || k.ch == '?') { textViewer("Forage", FORAGE_HELP, col::GPS); lastDraw = 0; }

    if (screenIsOff()) { delay(20); continue; }  // pocket foraging: keep scanning, don't draw
    if (millis() - lastDraw < 220 && !k.any) { delay(8); continue; }
    lastDraw = millis();

    drawHeader("Forage", col::GPS, ICON_FORAGE);
    // tallies
    canvas.setFont(&fonts::Font0);
    const FindKind order[] = {FK_FAIRY, FK_DEATHCAP, FK_PUFFBALL, FK_FRUIT, FK_SPORE, FK_MOREL};
    for (int i = 0; i < 6; i++) {
      FindKind kk = order[i];
      int col0 = (i % 2) * 118 + 8;
      int row = i / 2;
      int yy = BAR_H + 8 + row * 16;
      drawFindSymbol(canvas, col0 + 4, yy + 4, kk, findColour_pub(kk));
      canvas.setTextDatum(middle_left);
      canvas.setTextColor(col::TEXT);
      int sess = 0;
      for (auto &f : forageFinds) if (f.kind == kk) sess++;
      canvas.drawString(String(findName(kk)) + " " + String(sess), col0 + 14, yy + 4);
    }
    // live ticker
    int fy = BAR_H + 8 + 3 * 16 + 4;
    canvas.drawFastHLine(6, fy, SCR_W - 12, col::FAINT);
    fy += 3;
    canvas.setTextColor(col::DIM);
    canvas.setTextDatum(top_left);
    for (int i = 0; i < feedN && i < 3; i++) {
      canvas.setTextColor(i == 0 ? accent() : col::DIM);
      canvas.drawString(feed[i], 8, fy + i * 10);
    }
    // status line
    canvas.setTextDatum(bottom_left);
    canvas.setTextColor(col::DIM);
    String st = String("WiFi") + (scanState == -1 ? ".." : "  ") + (ble ? "  BLE" : "  (no BLE)") + ((radioMode == RM_MESH) ? "  Mesh" : "");
    canvas.drawString(st, 6, SCR_H - 2);
    canvas.setTextDatum(bottom_right);
    canvas.drawString(fitText("L list  M map  E print  H help", 150), SCR_W - 4, SCR_H - 2);
    pushFrame();
  }

  forageRunning = false;
  bleStop();
  WiFi.scanDelete();
  if (!wasMesh && radioMode == RM_MESH && !cfg.meshListen) loraSetMode(RM_CAPDECK);
  lifeSave();
}

// jump straight to the map without leaving finds behind (finds layer is on by default)
static void forageStop_toMap() {
  forageRunning = false;
  bleStop();
  WiFi.scanDelete();
  lifeSave();
  appMap(-1);
}
