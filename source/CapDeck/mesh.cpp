// CapDeck - passive Meshtastic listener.
// Receives Meshtastic packets on a US modem preset, decrypts ones sent on the
// default public channel key (AES-128-CTR, the same scheme the firmware uses),
// and decodes just enough protobuf to learn node names, positions and texts.
// Receive-only: CapDeck never transmits into the mesh.
#include "core.h"
#include <mbedtls/aes.h>

// US region default frequency slots (902-928 MHz, slot = djb2(preset name) % slots)
const MeshPreset MESH_PRESETS[] = {
    {"LongFast", 906.875f, 250.0f, 11, 5},  {"MediumFast", 913.125f, 250.0f, 9, 5},
    {"MediumSlow", 914.875f, 250.0f, 10, 5}, {"LongSlow", 905.3125f, 125.0f, 12, 8},
    {"LongMod", 902.6875f, 125.0f, 11, 8},  {"ShortFast", 918.875f, 250.0f, 7, 5},
    {"ShortSlow", 920.625f, 250.0f, 8, 5},  {"ShortTurbo", 926.75f, 500.0f, 7, 5},
};
const int MESH_PRESET_COUNT = sizeof(MESH_PRESETS) / sizeof(MESH_PRESETS[0]);

std::vector<MeshNode> meshNodes;
std::vector<MeshText> meshTexts;
uint32_t meshPackets = 0, meshUndecoded = 0;
static bool dirty = false;

static const uint8_t DEFAULT_PSK[16] = {0xd4, 0xf1, 0xbb, 0x3a, 0x20, 0x29, 0x07, 0x59,
                                        0xf0, 0xbc, 0xff, 0xab, 0xcf, 0x4e, 0x69, 0x01};

// channel hash = xor(name bytes) ^ xor(key bytes); default channel uses the preset name
static uint8_t defaultChannelHash() {
  const char *name = MESH_PRESETS[cfg.meshPreset % MESH_PRESET_COUNT].name;
  uint8_t h = 0;
  for (const char *p = name; *p; p++) h ^= (uint8_t)*p;
  for (uint8_t b : DEFAULT_PSK) h ^= b;
  return h;
}

static void aesCtr(uint32_t from, uint32_t id, uint8_t *data, size_t len) {
  uint8_t nonce[16] = {0}, stream[16];
  uint64_t pid = id;
  memcpy(nonce, &pid, 8);   // packet id as 64-bit little endian
  memcpy(nonce + 8, &from, 4);  // sender node number
  size_t off = 0;
  mbedtls_aes_context ctx;
  mbedtls_aes_init(&ctx);
  mbedtls_aes_setkey_enc(&ctx, DEFAULT_PSK, 128);
  mbedtls_aes_crypt_ctr(&ctx, len, &off, nonce, stream, data, data);
  mbedtls_aes_free(&ctx);
}

// ------------------------------------------------------------------ tiny protobuf reader
struct PB {
  const uint8_t *p, *end;
  bool ok = true;
  bool more() const { return ok && p < end; }
  uint64_t varint() {
    uint64_t v = 0;
    for (int s = 0; s < 64 && p < end; s += 7) {
      uint8_t b = *p++;
      v |= (uint64_t)(b & 0x7F) << s;
      if (!(b & 0x80)) return v;
    }
    ok = false;
    return 0;
  }
  uint32_t fixed32() {
    if (end - p < 4) { ok = false; return 0; }
    uint32_t v;
    memcpy(&v, p, 4);
    p += 4;
    return v;
  }
  // reads a field key; returns field number, sets wire type
  uint32_t key(uint8_t &wt) {
    uint64_t k = varint();
    wt = k & 7;
    return k >> 3;
  }
  bool bytes(const uint8_t *&b, size_t &n) {
    uint64_t l = varint();
    if (!ok || l > (uint64_t)(end - p)) { ok = false; return false; }
    b = p;
    n = l;
    p += l;
    return true;
  }
  void skip(uint8_t wt) {
    const uint8_t *b;
    size_t n;
    switch (wt) {
      case 0: varint(); break;
      case 1: if (end - p < 8) ok = false; else p += 8; break;
      case 2: bytes(b, n); break;
      case 5: fixed32(); break;
      default: ok = false;
    }
  }
};

static String str(const uint8_t *b, size_t n) {
  String s;
  s.reserve(n);
  for (size_t i = 0; i < n; i++) s += (char)b[i];
  return s;
}

MeshNode *meshFind(uint32_t num) {
  for (auto &n : meshNodes)
    if (n.num == num) return &n;
  return nullptr;
}

String meshName(uint32_t num) {
  MeshNode *n = meshFind(num);
  if (n && n->shortName.length()) return n->shortName;
  char b[12];
  snprintf(b, sizeof b, "!%08lx", (unsigned long)num);
  return b;
}

static MeshNode &touch(uint32_t num) {
  MeshNode *n = meshFind(num);
  if (!n) {
    if (meshNodes.size() >= 120) {
      // drop the stalest node
      size_t old = 0;
      for (size_t i = 1; i < meshNodes.size(); i++)
        if (meshNodes[i].lastMs < meshNodes[old].lastMs) old = i;
      meshNodes.erase(meshNodes.begin() + old);
    }
    MeshNode nn;
    nn.num = num;
    meshNodes.push_back(nn);
    n = &meshNodes.back();
    beep(2400, 15);
  }
  return *n;
}

static void decodePosition(MeshNode &n, const uint8_t *b, size_t len) {
  PB pb{b, b + len};
  int32_t lat = 0, lon = 0, alt = 0;
  bool haveLat = false, haveLon = false;
  while (pb.more()) {
    uint8_t wt;
    uint32_t f = pb.key(wt);
    if (f == 1 && wt == 5) { lat = (int32_t)pb.fixed32(); haveLat = true; }
    else if (f == 2 && wt == 5) { lon = (int32_t)pb.fixed32(); haveLon = true; }
    else if (f == 3 && wt == 0) alt = (int32_t)pb.varint();
    else pb.skip(wt);
  }
  if (pb.ok && haveLat && haveLon && (lat || lon)) {
    n.lat = lat * 1e-7;
    n.lon = lon * 1e-7;
    n.alt = alt;
    n.hasPos = true;
    dirty = true;
  }
}

static void decodeUser(MeshNode &n, const uint8_t *b, size_t len) {
  PB pb{b, b + len};
  while (pb.more()) {
    uint8_t wt;
    uint32_t f = pb.key(wt);
    const uint8_t *s;
    size_t sl;
    if ((f == 2 || f == 3) && wt == 2 && pb.bytes(s, sl)) {
      if (f == 2) n.longName = str(s, min(sl, (size_t)40));
      else n.shortName = str(s, min(sl, (size_t)8));
      dirty = true;
    } else pb.skip(wt);
  }
}

void meshHandlePacket(const uint8_t *buf, size_t len, float rssi, float snr) {
  if (len < 17) return;
  uint32_t to, from, id;
  memcpy(&to, buf, 4);
  memcpy(&from, buf + 4, 4);
  memcpy(&id, buf + 8, 4);
  uint8_t flags = buf[12], chan = buf[13];
  meshPackets++;
  MeshNode &n = touch(from);
  n.rssi = rssi;
  n.snr = snr;
  uint8_t hopLimit = flags & 0x07, hopStart = (flags & 0xE0) >> 5;
  n.hops = hopStart >= hopLimit ? hopStart - hopLimit : 0;
  n.lastMs = millis();
  n.lastHeard = timeValid() ? (uint32_t)time(nullptr) : 0;
  n.packets++;

  if (chan != defaultChannelHash()) {  // private channel or direct message: header only
    meshUndecoded++;
    return;
  }
  uint8_t data[256];
  size_t dl = len - 16;
  memcpy(data, buf + 16, dl);
  aesCtr(from, id, data, dl);
  // Data { portnum = 1; payload = 2; ... }
  PB pb{data, data + dl};
  uint32_t port = 0;
  const uint8_t *payload = nullptr;
  size_t plen = 0;
  while (pb.more()) {
    uint8_t wt;
    uint32_t f = pb.key(wt);
    if (f == 1 && wt == 0) port = pb.varint();
    else if (f == 2 && wt == 2) pb.bytes(payload, plen);
    else pb.skip(wt);
  }
  if (!pb.ok || !payload) { meshUndecoded++; return; }
  switch (port) {
    case 1: {  // TEXT_MESSAGE_APP
      String t = str(payload, min(plen, (size_t)200));
      meshTexts.push_back({from, t, (uint32_t)millis()});
      if (meshTexts.size() > 40) meshTexts.erase(meshTexts.begin());
      if (sdOK) {
        File f = SD.open("/capdeck/mesh/messages.txt", FILE_APPEND);
        if (f) {
          f.printf("[%s] %s: %s\n", timeValid() ? timeStr(true).c_str() : "--", meshName(from).c_str(), t.c_str());
          f.close();
        }
      }
      beep(3000, 40);
      break;
    }
    case 3: decodePosition(n, payload, plen); break;  // POSITION_APP
    case 4: decodeUser(n, payload, plen); break;      // NODEINFO_APP
    default: break;
  }
}

// ------------------------------------------------------------------ persistence (/capdeck/mesh/nodes.csv)
static const char *NODES_PATH = "/capdeck/mesh/nodes.csv";

void meshLoad() {
  if (!sdOK) return;
  if (!SD.exists("/capdeck/mesh")) SD.mkdir("/capdeck/mesh");
  File f = SD.open(NODES_PATH, FILE_READ);
  if (!f) return;
  while (f.available() && meshNodes.size() < 120) {
    String l = f.readStringUntil('\n');
    l.trim();
    if (!l.length() || l[0] == '#') continue;
    // num,short,long,lat,lon,alt,lastHeard
    std::vector<String> c;
    int from = 0;
    for (int i = 0; i < 6; i++) {
      int k = l.indexOf(',', from);
      if (k < 0) break;
      c.push_back(l.substring(from, k));
      from = k + 1;
    }
    c.push_back(l.substring(from));
    if (c.size() < 7) continue;
    MeshNode n;
    n.num = strtoul(c[0].c_str(), nullptr, 16);
    n.shortName = c[1];
    n.longName = c[2];
    n.lat = c[3].toDouble();
    n.lon = c[4].toDouble();
    n.alt = c[5].toInt();
    n.hasPos = n.lat != 0 || n.lon != 0;
    n.lastHeard = strtoul(c[6].c_str(), nullptr, 10);
    meshNodes.push_back(n);
  }
  f.close();
}

void meshSaveIfDirty() {
  if (!dirty || !sdOK) return;
  dirty = false;
  if (!SD.exists("/capdeck/mesh")) SD.mkdir("/capdeck/mesh");
  File f = SD.open(NODES_PATH, FILE_WRITE);
  if (!f) return;
  f.println("# num,short,long,lat,lon,alt_m,last_heard_unix");
  for (auto &n : meshNodes) {
    String ln = n.longName, sn = n.shortName;
    ln.replace(",", " ");
    sn.replace(",", " ");
    f.printf("%08lx,%s,%s,%.7f,%.7f,%ld,%lu\n", (unsigned long)n.num, sn.c_str(), ln.c_str(), n.lat, n.lon, (long)n.alt,
             (unsigned long)n.lastHeard);
  }
  f.close();
}
