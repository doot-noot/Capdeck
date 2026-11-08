// CapDeck - passive Meshtastic listener.
// Receives Meshtastic packets on a US modem preset, decrypts ones sent on the
// default public channel key (AES-128-CTR, the same scheme the firmware uses),
// and decodes just enough protobuf to learn node names, positions and texts.
// Transmit uses the Meshtastic "Client Mute" role: CapDeck sends only its OWN packets
// (text, node info, optional position) and NEVER rebroadcasts other nodes' traffic.
#include "core.h"
#include <RadioLib.h>
#include <mbedtls/aes.h>
#include <mbedtls/base64.h>
#include <ArduinoJson.h>

// US region default frequency slots (902-928 MHz, slot = djb2(preset name) % slots)
const MeshPreset MESH_PRESETS[] = {
    {"LongFast", 906.875f, 250.0f, 11, 5},  {"MediumFast", 913.125f, 250.0f, 9, 5},
    {"MediumSlow", 914.875f, 250.0f, 10, 5}, {"LongSlow", 905.3125f, 125.0f, 12, 8},
    {"LongMod", 902.6875f, 125.0f, 11, 8},  {"ShortFast", 918.875f, 250.0f, 7, 5},
    {"ShortSlow", 920.625f, 250.0f, 8, 5},  {"ShortTurbo", 926.75f, 500.0f, 7, 5},
    {"LongTurbo", 908.75f, 500.0f, 11, 8},  {"MediumTurbo", 925.25f, 500.0f, 9, 5},
};
const int MESH_PRESET_COUNT = sizeof(MESH_PRESETS) / sizeof(MESH_PRESETS[0]);
// EU_868: Meshtastic's sub-band is 869.4-869.65 MHz. Same slot rule as the US table above
// (slot = djb2(preset name) % slots that fit): 250 kHz presets -> 869.525, LongSlow -> 869.4625,
// LongMod -> 869.5875. 500 kHz (Turbo) presets don't fit, so they aren't allowed on EU.
static uint32_t djb2(const char *s) { uint32_t h = 5381; while (*s) h = ((h << 5) + h) + (uint8_t)*s++; return h; }
bool meshPresetAllowed(const MeshPreset &p) { return cfg.band != 1 || p.bw <= 250.0f; }
float meshFreq(const MeshPreset &p) {
  if (cfg.band != 1) return p.freq;
  int slots = (int)(0.25f / (p.bw / 1000.0f) + 0.001f);
  if (slots < 1) return 869.525f;  // not allowed; callers skip these
  return 869.4f + p.bw / 2000.0f + (djb2(p.name) % slots) * (p.bw / 1000.0f);
}
float bandCenter() { return cfg.band == 1 ? 868.0f : 915.0f; }
float bandLo() { return cfg.band == 1 ? 863.0f : 902.0f; }
float bandHi() { return cfg.band == 1 ? 870.0f : 928.0f; }
const char *bandName() { return cfg.band == 1 ? "EU 868" : "US 915"; }

std::vector<MeshNode> meshNodes;
std::vector<MeshText> meshTexts;
uint32_t meshPackets = 0, meshUndecoded = 0;
static bool dirty = false;

static const uint8_t DEFAULT_PSK[16] = {0xd4, 0xf1, 0xbb, 0x3a, 0x20, 0x29, 0x07, 0x59,
                                        0xf0, 0xbc, 0xff, 0xab, 0xcf, 0x4e, 0x69, 0x01};

// ------------------------------------------------------------------ channels
// hash = xor(name bytes) ^ xor(key bytes). The primary channel's name is the preset name.
struct MeshChannel {
  String name;
  uint8_t key[32];
  uint8_t keyLen;  // 16 or 32 (0 = unencrypted)
  uint8_t hash;
};
static std::vector<MeshChannel> channels;

static bool expandPsk(const uint8_t *psk, size_t len, MeshChannel &c) {
  if (len == 1) {  // short form: index into the default key
    if (psk[0] == 0) { c.keyLen = 0; return true; }
    memcpy(c.key, DEFAULT_PSK, 16);
    c.key[15] = DEFAULT_PSK[15] + psk[0] - 1;
    c.keyLen = 16;
    return true;
  }
  if (len == 16 || len == 32) { memcpy(c.key, psk, len); c.keyLen = len; return true; }
  if (len == 0) { c.keyLen = 0; return true; }
  return false;
}

static void addChannel(const String &name, const uint8_t *psk, size_t pskLen) {
  MeshChannel c;
  c.name = name;
  if (!expandPsk(psk, pskLen, c)) return;
  uint8_t h = 0;
  for (size_t i = 0; i < name.length(); i++) h ^= (uint8_t)name[i];
  for (int i = 0; i < c.keyLen; i++) h ^= c.key[i];
  c.hash = h;
  channels.push_back(c);
}

static void addChannelB64(const String &name, const String &b64) {
  uint8_t psk[48];
  size_t n = 0;
  if (mbedtls_base64_decode(psk, sizeof psk, &n, (const uint8_t *)b64.c_str(), b64.length()) != 0) return;
  addChannel(name, psk, n);
}

void meshBuildChannels() {
  channels.clear();
  uint8_t one = 1;
  addChannel(MESH_PRESETS[cfg.meshPreset % MESH_PRESET_COUNT].name, &one, 1);  // public default channel
  // Michigan Mesh community channels (michmesh.net), all on PSK "MA=="
  const char *mi[] = {"Michigan", "WMI", "EMI", "Muskegon", "NMI", "YOOPER", "Washtenaw"};
  for (auto n : mi) addChannelB64(n, "MA==");
  // extra channels from config.json: "mesh_channels": [{"name": "...", "psk": "base64"}]
  if (sdOK) {
    File f = SD.open("/capdeck/config.json", FILE_READ);
    if (f) {
      JsonDocument d;
      if (!deserializeJson(d, f))
        for (JsonObject c : d["mesh_channels"].as<JsonArray>()) addChannelB64(String(c["name"] | ""), String(c["psk"] | "AQ=="));
      f.close();
    }
  }
}

static void aesCtr(const MeshChannel &ch, uint32_t from, uint32_t id, uint8_t *data, size_t len) {
  if (!ch.keyLen) return;
  uint8_t nonce[16] = {0}, stream[16];
  uint64_t pid = id;
  memcpy(nonce, &pid, 8);       // packet id as 64-bit little endian
  memcpy(nonce + 8, &from, 4);  // sender node number
  size_t off = 0;
  mbedtls_aes_context ctx;
  mbedtls_aes_init(&ctx);
  mbedtls_aes_setkey_enc(&ctx, ch.key, ch.keyLen * 8);
  mbedtls_aes_crypt_ctr(&ctx, len, &off, nonce, stream, data, data);
  mbedtls_aes_free(&ctx);
}

// ------------------------------------------------------------------ transmit (Client-Mute behaviour)
// CapDeck sends its OWN packets (text, node info, optional position) and NEVER rebroadcasts other
// nodes' traffic. That is the Meshtastic "Client Mute" role: a full, valid participant that adds no
// routing load to the mesh, so it can't disrupt anyone else's network.
#include <esp_system.h>
SX1262 *loraRadio();

static const MeshChannel *chanByName(const String &name) {
  if (channels.empty()) meshBuildChannels();
  for (auto &c : channels)
    if (c.name.equalsIgnoreCase(name)) return &c;
  return channels.empty() ? nullptr : &channels[0];  // default to primary
}

uint32_t meshMyNum() {
  return (uint32_t)(ESP.getEfuseMac() & 0xFFFFFFFF);  // stable per-device node number
}

// --- minimal protobuf writer ---
struct PBW {
  std::vector<uint8_t> b;
  void varint(uint64_t v) {
    do {
      uint8_t x = v & 0x7F;
      v >>= 7;
      if (v) x |= 0x80;
      b.push_back(x);
    } while (v);
  }
  void tag(uint32_t f, uint8_t wt) { varint((f << 3) | wt); }
  void vfield(uint32_t f, uint64_t v) { tag(f, 0); varint(v); }
  void f32field(uint32_t f, uint32_t v) { tag(f, 5); for (int i = 0; i < 4; i++) b.push_back((v >> (8 * i)) & 0xFF); }
  void bytes(uint32_t f, const uint8_t *d, size_t n) { tag(f, 2); varint(n); b.insert(b.end(), d, d + n); }
  void str(uint32_t f, const String &s) { bytes(f, (const uint8_t *)s.c_str(), s.length()); }
};

// Build Data{portnum,payload}, encrypt for the channel, frame with the 16-byte header, transmit.
static bool meshSendRaw(const String &channelName, uint32_t portnum, const uint8_t *payload, size_t plen) {
  if (!loraPresent || radioMode != RM_MESH) return false;
  const MeshChannel *ch = chanByName(channelName);
  if (!ch) return false;
  PBW data;
  data.vfield(1, portnum);          // Data.portnum
  data.bytes(2, payload, plen);     // Data.payload
  if (data.b.size() > 233) return false;

  uint32_t from = meshMyNum();
  uint32_t to = 0xFFFFFFFF;         // broadcast
  uint32_t id = esp_random() | 1;   // nonzero packet id

  uint8_t frame[256];
  memcpy(frame + 0, &to, 4);        // header matches the decode path (native LE)
  memcpy(frame + 4, &from, 4);
  memcpy(frame + 8, &id, 4);
  frame[12] = (3) | (3 << 5);       // hop_limit 3, hop_start 3, want_ack 0
  frame[13] = ch->hash;
  frame[14] = 0;                    // next_hop unknown
  frame[15] = from & 0xFF;          // relay_node = us (originator)
  memcpy(frame + 16, data.b.data(), data.b.size());
  aesCtr(*ch, from, id, frame + 16, data.b.size());  // CTR: encrypt == decrypt

  SX1262 *r = loraRadio();
  r->standby();
  int st = r->transmit(frame, 16 + data.b.size());
  r->startReceive();
  return st == RADIOLIB_ERR_NONE;
}

bool meshSendText(const String &channelName, const String &text) {
  return meshSendRaw(channelName, 1 /*TEXT_MESSAGE_APP*/, (const uint8_t *)text.c_str(), text.length());
}

static String myNodeId() {
  char id[12];
  snprintf(id, sizeof id, "!%08lx", (unsigned long)meshMyNum());
  return id;
}

bool meshSendNodeInfo() {
  PBW u;
  u.str(1, myNodeId());
  u.str(2, cfg.meshLong.length() ? cfg.meshLong : String("CapDeck ") + myNodeId().substring(5));
  u.str(3, cfg.meshShort.length() ? cfg.meshShort : myNodeId().substring(7));  // 4-char short
  u.vfield(5, 255);  // HardwareModel.PRIVATE_HW - honest "not an official device"
  return meshSendRaw(MESH_PRESETS[cfg.meshPreset % MESH_PRESET_COUNT].name, 4 /*NODEINFO_APP*/, u.b.data(), u.b.size());
}

bool meshSendPosition() {
  double la, lo;
  extern bool myPosition(double &, double &);
  if (!myPosition(la, lo)) return false;
  PBW p;
  p.f32field(1, (uint32_t)(int32_t)lround(la * 1e7));  // latitude_i (sfixed32)
  p.f32field(2, (uint32_t)(int32_t)lround(lo * 1e7));  // longitude_i
  if (timeValid()) p.f32field(4, (uint32_t)time(nullptr));
  return meshSendRaw(MESH_PRESETS[cfg.meshPreset % MESH_PRESET_COUNT].name, 3 /*POSITION_APP*/, p.b.data(), p.b.size());
}

// Periodic, polite self-announce while TX is enabled (every 15 min). Never rebroadcasts anything.
void meshTxTick() {
  static uint32_t lastInfo = 0;
  if (!cfg.meshTx || radioMode != RM_MESH) return;
  if (lastInfo == 0 || millis() - lastInfo > 15UL * 60 * 1000) {
    meshSendNodeInfo();
    lastInfo = millis();
  }
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
    ledPulse(0);  // soft pulse for a new node
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

  if (channels.empty()) meshBuildChannels();
  uint32_t port = 0;
  const uint8_t *payload = nullptr;
  size_t plen = 0;
  static uint8_t data[256];
  const MeshChannel *hit = nullptr;
  size_t dl = len - 16;
  for (auto &ch : channels) {
    if (ch.hash != chan) continue;
    memcpy(data, buf + 16, dl);
    aesCtr(ch, from, id, data, dl);
    // Data { portnum = 1; payload = 2; ... }
    PB pb{data, data + dl};
    port = 0;
    payload = nullptr;
    while (pb.more()) {
      uint8_t wt;
      uint32_t f = pb.key(wt);
      if (f == 1 && wt == 0) port = pb.varint();
      else if (f == 2 && wt == 2) pb.bytes(payload, plen);
      else pb.skip(wt);
    }
    if (pb.ok && payload && port > 0 && port < 1024) { hit = &ch; break; }
  }
  if (!hit) {  // private channel or direct message: header only
    meshUndecoded++;
    return;
  }
  switch (port) {
    case 1: {  // TEXT_MESSAGE_APP
      String t = str(payload, min(plen, (size_t)200));
      meshTexts.push_back({from, t, (uint32_t)millis(), hit->name});
      if (meshTexts.size() > 40) meshTexts.erase(meshTexts.begin());
      if (sdOK) {
        File f = SD.open("/capdeck/mesh/messages.txt", FILE_APPEND);
        if (f) {
          f.printf("[%s] #%s %s: %s\n", timeValid() ? timeStr(true).c_str() : "--", hit->name.c_str(), meshName(from).c_str(), t.c_str());
          f.close();
        }
      }
      beep(3000, 40);
      ledPulse(0);
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
