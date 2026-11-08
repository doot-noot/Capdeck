// CapDeck - Rhizomorph: Reticulum radio.
// Rhizomorphs are the root-like cords some fungi grow to carry things over long distances.
// Here the Cardputer + Cap becomes an RNode for Sideband (or any Reticulum app) over Bluetooth LE:
// the phone does identities, encryption, LXMF messages and routing; CapDeck just carries packets
// between the phone and the Cap's SX1262.
//
// Device side of the RNode KISS protocol, implemented from the host code in Reticulum's
// RNS/Interfaces/RNodeInterface.py (what the host sends, and what it checks):
//  - BLE: Nordic UART service, advertised name starts with "RNode ", bonded (PIN pairing).
//  - detect: CMD_DETECT 0x73 -> 0x46, report firmware >= 1.52, platform ESP32 (0x80).
//  - every radio setting the host sends is echoed back; the host compares bandwidth, TX power,
//    SF and radio state exactly (frequency within 100 Hz) and drops the link on a mismatch.
//  - RSSI is reported +157, SNR as signed quarter-dB, both just before each CMD_DATA frame.
//  - On air: a 1-byte RNode header (random sequence in the high nibble, bit 0 = split), so packets
//    up to Reticulum's 500-byte MTU travel as one or two LoRa frames, interoperable with real RNodes.
// Our own code; no RNode firmware source is copied.
#include "core.h"
#include <Preferences.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLESecurity.h>
#include <freertos/stream_buffer.h>
#include <RadioLib.h>
#include <esp_mac.h>

bool rhizoRunning = false;

namespace {
// ---- KISS / RNode protocol constants (values from RNodeInterface.py)
enum : uint8_t {
  FEND = 0xC0, FESC = 0xDB, TFEND = 0xDC, TFESC = 0xDD,
  CMD_DATA = 0x00, CMD_FREQUENCY = 0x01, CMD_BANDWIDTH = 0x02, CMD_TXPOWER = 0x03, CMD_SF = 0x04,
  CMD_CR = 0x05, CMD_RADIO_STATE = 0x06, CMD_RADIO_LOCK = 0x07, CMD_DETECT = 0x08, CMD_LEAVE = 0x0A,
  CMD_ST_ALOCK = 0x0B, CMD_LT_ALOCK = 0x0C, CMD_READY = 0x0F,
  CMD_STAT_RX = 0x21, CMD_STAT_TX = 0x22, CMD_STAT_RSSI = 0x23, CMD_STAT_SNR = 0x24, CMD_STAT_BAT = 0x27,
  CMD_BLINK = 0x30, CMD_RANDOM = 0x40, CMD_BOARD = 0x47, CMD_PLATFORM = 0x48, CMD_MCU = 0x49,
  CMD_FW_VERSION = 0x50, CMD_ERROR = 0x90,
  DETECT_REQ = 0x73, DETECT_RESP = 0x46,
  RADIO_STATE_OFF = 0x00, RADIO_STATE_ON = 0x01, RADIO_STATE_ASK = 0xFF,
  ERROR_INITRADIO = 0x01, ERROR_TXFAILED = 0x02,
  PLATFORM_ESP32 = 0x80, MCU_ESP32 = 0x81,
  FW_MAJ = 1, FW_MIN = 82,   // host requires >= 1.52
  FLAG_SPLIT = 0x01,
};
const int RSSI_OFFSET = 157;
const int HW_MTU = 508;              // host buffers CMD_DATA up to this
const int LORA_MAX = 255;            // one SX1262 frame
const char *NUS_SVC = "6E400001-B5A3-F393-E0A9-E50E24DCCA9E";
const char *NUS_RX = "6E400002-B5A3-F393-E0A9-E50E24DCCA9E";  // host writes here
const char *NUS_TX = "6E400003-B5A3-F393-E0A9-E50E24DCCA9E";  // we notify here

// ---- state
struct Radio {
  uint32_t freq = 0, bw = 0;  // Hz
  uint8_t sf = 0, cr = 0, state = RADIO_STATE_OFF, lock = 0;
  int8_t txp = 0;
  uint16_t stAlock = 0, ltAlock = 0;
} rp;
uint32_t statRx = 0, statTx = 0;
float lastRssi = 0, lastSnr = 0;
String status = "waiting for Sideband";
String warn;
uint32_t pin = 0;
char devName[16] = "RNode";
volatile bool connected = false, bonded = false;
volatile uint16_t peerMtu = 23;
StreamBufferHandle_t inbox = nullptr;
BLECharacteristic *txChar = nullptr;
BLEServer *server = nullptr;
volatile bool rxFlag = false;
void IRAM_ATTR onLoraRx() { rxFlag = true; }

// split reassembly
uint8_t pend[HW_MTU];
int pendLen = 0;
int8_t pendSeq = -1;

// ---- host link: KISS-escape a frame and push it out as BLE notifications
void sendRaw(const uint8_t *b, size_t n) {
  if (!connected || !txChar) return;
  size_t chunk = max(20, (int)peerMtu - 3);
  for (size_t o = 0; o < n; o += chunk) {
    size_t len = min(chunk, n - o);
    txChar->setValue((uint8_t *)b + o, len);
    txChar->notify();
    delay(3);  // let the stack drain; Android drops back-to-back notifies otherwise
  }
}
void sendFrame(uint8_t cmd, const uint8_t *d, size_t n) {
  static uint8_t out[HW_MTU * 2 + 8];
  size_t k = 0;
  out[k++] = FEND;
  out[k++] = cmd;
  for (size_t i = 0; i < n && k < sizeof out - 3; i++) {
    if (d[i] == FEND) { out[k++] = FESC; out[k++] = TFEND; }
    else if (d[i] == FESC) { out[k++] = FESC; out[k++] = TFESC; }
    else out[k++] = d[i];
  }
  out[k++] = FEND;
  sendRaw(out, k);
}
void send1(uint8_t cmd, uint8_t v) { sendFrame(cmd, &v, 1); }
void send4(uint8_t cmd, uint32_t v) {
  uint8_t b[4] = {(uint8_t)(v >> 24), (uint8_t)(v >> 16), (uint8_t)(v >> 8), (uint8_t)v};
  sendFrame(cmd, b, 4);
}
void send2(uint8_t cmd, uint16_t v) {
  uint8_t b[2] = {(uint8_t)(v >> 8), (uint8_t)v};
  sendFrame(cmd, b, 2);
}

// ---- BLE callbacks (run on the BLE task: keep them tiny)
class SrvCB : public BLEServerCallbacks {
  void onConnect(BLEServer *, ble_gap_conn_desc *) override { connected = true; peerMtu = 23; }
  void onDisconnect(BLEServer *, ble_gap_conn_desc *) override { connected = false; bonded = false; BLEDevice::startAdvertising(); }
  void onMtuChanged(BLEServer *, ble_gap_conn_desc *, uint16_t m) override { peerMtu = m; }
} srvCb;
class RxCB : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *c, ble_gap_conn_desc *) override {
    String v = c->getValue();
    if (inbox && v.length()) xStreamBufferSend(inbox, v.c_str(), v.length(), 0);
  }
} rxCb;
class SecCB : public BLESecurityCallbacks {
  uint32_t onPassKeyRequest() override { return pin; }
  void onPassKeyNotify(uint32_t) override {}
  bool onSecurityRequest() override { return true; }
  bool onConfirmPIN(uint32_t) override { return true; }
  void onAuthenticationComplete(ble_gap_conn_desc *d) override { bonded = d && d->sec_state.bonded; }
} secCb;

// ---- radio
bool inBand(uint32_t hz) {
  float m = hz / 1e6f;
  return m >= bandLo() && m <= bandHi();
}
uint16_t preambleFor(uint8_t sf, uint32_t bw) {
  // aim for >= ~24 ms of preamble, never fewer than 18 symbols (what RNodes use)
  float symMs = (float)(1UL << sf) / (bw / 1000.0f);
  int s = (int)ceilf(24.0f / symMs);
  return (uint16_t)max(18, s);
}
bool radioUp() {
  SX1262 *r = loraRadio();
  warn = "";
  if (!loraPresent || !r) { warn = "No Cap LoRa found"; return false; }
  if (!rp.freq || !rp.bw || !rp.sf || !rp.cr) { warn = "Sideband hasn't sent radio settings yet"; return false; }
  if (!inBand(rp.freq)) {
    warn = String(rp.freq / 1e6, 3) + " MHz is outside " + bandName() + ". Change it in Sideband, or switch Band in LoRa settings.";
    return false;
  }
  r->standby();
  bool ok = r->setFrequency(rp.freq / 1e6f) == RADIOLIB_ERR_NONE;
  ok &= r->setBandwidth(rp.bw / 1000.0f) == RADIOLIB_ERR_NONE;
  ok &= r->setSpreadingFactor(rp.sf) == RADIOLIB_ERR_NONE;
  ok &= r->setCodingRate(rp.cr) == RADIOLIB_ERR_NONE;
  ok &= r->setSyncWord(0x12) == RADIOLIB_ERR_NONE;  // RNode's private sync word (0x1424 on SX126x)
  ok &= r->setPreambleLength(preambleFor(rp.sf, rp.bw)) == RADIOLIB_ERR_NONE;
  ok &= r->setOutputPower(rp.txp) == RADIOLIB_ERR_NONE;
  r->setCRC(true);
  r->setPacketReceivedAction(onLoraRx);
  rxFlag = false;
  ok &= r->startReceive() == RADIOLIB_ERR_NONE;
  if (!ok) warn = "The radio refused those settings";
  return ok;
}
void radioDown() {
  SX1262 *r = loraRadio();
  if (r) { r->clearPacketReceivedAction(); r->standby(); }
}

bool txOne(const uint8_t *b, size_t n) {
  SX1262 *r = loraRadio();
  // listen before talk: a few CAD checks with random backoff, like an RNode's CSMA
  for (int i = 0; i < 6; i++) {
    if (r->scanChannel() != RADIOLIB_LORA_DETECTED) break;
    delay(20 + esp_random() % 80);
  }
  int st = r->transmit((uint8_t *)b, n);
  return st == RADIOLIB_ERR_NONE;
}
void transmitPacket(const uint8_t *d, size_t n) {
  if (rp.state != RADIO_STATE_ON || !n) return;
  SX1262 *r = loraRadio();
  r->clearPacketReceivedAction();
  uint8_t hdr = esp_random() & 0xF0;
  uint8_t buf[LORA_MAX];
  bool ok;
  if (n > LORA_MAX - 1) {  // two frames, same sequence, split flag set
    hdr |= FLAG_SPLIT;
    size_t a = LORA_MAX - 1;
    buf[0] = hdr; memcpy(buf + 1, d, a);
    ok = txOne(buf, a + 1);
    buf[0] = hdr; memcpy(buf + 1, d + a, n - a);
    ok &= txOne(buf, n - a + 1);
  } else {
    buf[0] = hdr; memcpy(buf + 1, d, n);
    ok = txOne(buf, n + 1);
  }
  if (ok) statTx++;
  else send1(CMD_ERROR, ERROR_TXFAILED);
  r->setPacketReceivedAction(onLoraRx);
  rxFlag = false;
  r->startReceive();
  send1(CMD_READY, 0x01);
  ledPulse(0);
}

void deliver(const uint8_t *d, size_t n) {
  int rs = constrain((int)lroundf(lastRssi) + RSSI_OFFSET, 0, 255);
  send1(CMD_STAT_RSSI, (uint8_t)rs);
  send1(CMD_STAT_SNR, (uint8_t)(int8_t)constrain((int)lroundf(lastSnr * 4), -128, 127));
  sendFrame(CMD_DATA, d, n);
  statRx++;
}

void pollLora() {
  if (!rxFlag) return;
  rxFlag = false;
  SX1262 *r = loraRadio();
  uint8_t b[LORA_MAX];
  size_t n = r->getPacketLength();
  if (n >= 2 && n <= LORA_MAX && r->readData(b, n) == RADIOLIB_ERR_NONE) {
    lastRssi = r->getRSSI();
    lastSnr = r->getSNR();
    uint8_t hdr = b[0];
    int8_t seq = hdr >> 4;
    if (hdr & FLAG_SPLIT) {
      if (pendSeq == seq && pendLen) {  // second half
        if (pendLen + (int)n - 1 <= HW_MTU) { memcpy(pend + pendLen, b + 1, n - 1); deliver(pend, pendLen + n - 1); }
        pendLen = 0; pendSeq = -1;
      } else {  // first half: hold it
        memcpy(pend, b + 1, n - 1); pendLen = n - 1; pendSeq = seq;
      }
    } else {
      pendLen = 0; pendSeq = -1;
      deliver(b + 1, n - 1);
    }
    ledPulse(0);
  }
  r->startReceive();
}

// ---- host command handling
uint8_t cmdBuf[HW_MTU + 8];
int cmdLen = 0;
uint8_t cmd = 0xFE;
bool inFrame = false, esc = false;

// a setting changed while the radio is on: apply it now (or report that we can't)
void reapply() {
  if (rp.state == RADIO_STATE_ON && !radioUp()) { rp.state = RADIO_STATE_OFF; send1(CMD_ERROR, ERROR_INITRADIO); send1(CMD_RADIO_STATE, rp.state); }
}

uint32_t be32(const uint8_t *b) { return ((uint32_t)b[0] << 24) | ((uint32_t)b[1] << 16) | ((uint32_t)b[2] << 8) | b[3]; }

void handle() {
  const uint8_t *d = cmdBuf;
  int n = cmdLen;
  switch (cmd) {
    case CMD_DATA: if (n) transmitPacket(d, n); break;
    case CMD_DETECT: if (n && d[0] == DETECT_REQ) send1(CMD_DETECT, DETECT_RESP); status = "Sideband found us"; break;
    case CMD_FW_VERSION: { uint8_t v[2] = {FW_MAJ, FW_MIN}; sendFrame(CMD_FW_VERSION, v, 2); break; }
    case CMD_PLATFORM: send1(CMD_PLATFORM, PLATFORM_ESP32); break;
    case CMD_MCU: send1(CMD_MCU, MCU_ESP32); break;
    case CMD_BOARD: send1(CMD_BOARD, 0x00); break;
    case CMD_FREQUENCY:
      if (n >= 4 && be32(d)) rp.freq = be32(d);
      send4(CMD_FREQUENCY, rp.freq);
      reapply();
      break;
    case CMD_BANDWIDTH:
      if (n >= 4 && be32(d)) rp.bw = be32(d);
      send4(CMD_BANDWIDTH, rp.bw);
      reapply();
      break;
    case CMD_TXPOWER:
      if (n && d[0] != 0xFF) rp.txp = (int8_t)constrain((int)(int8_t)d[0], -9, 22);  // SX1262 range; echo what we really use
      send1(CMD_TXPOWER, (uint8_t)rp.txp);
      reapply();
      break;
    case CMD_SF:
      if (n && d[0] != 0xFF) rp.sf = constrain(d[0], 5, 12);
      send1(CMD_SF, rp.sf);
      reapply();
      break;
    case CMD_CR:
      if (n && d[0] != 0xFF) rp.cr = constrain(d[0], 5, 8);
      send1(CMD_CR, rp.cr);
      reapply();
      break;
    case CMD_ST_ALOCK: if (n >= 2) rp.stAlock = (d[0] << 8) | d[1]; send2(CMD_ST_ALOCK, rp.stAlock); break;
    case CMD_LT_ALOCK: if (n >= 2) rp.ltAlock = (d[0] << 8) | d[1]; send2(CMD_LT_ALOCK, rp.ltAlock); break;
    case CMD_RADIO_LOCK: if (n && d[0] != 0xFF) rp.lock = d[0]; send1(CMD_RADIO_LOCK, rp.lock); break;
    case CMD_RADIO_STATE:
      if (n && d[0] == RADIO_STATE_ON) {
        if (radioUp()) { rp.state = RADIO_STATE_ON; status = "tethered to Sideband"; }
        else { rp.state = RADIO_STATE_OFF; send1(CMD_ERROR, ERROR_INITRADIO); }
      } else if (n && d[0] == RADIO_STATE_OFF) { radioDown(); rp.state = RADIO_STATE_OFF; status = "radio off"; }
      send1(CMD_RADIO_STATE, rp.state);
      break;
    case CMD_LEAVE: radioDown(); rp.state = RADIO_STATE_OFF; status = "Sideband left"; break;
    case CMD_STAT_RX: send4(CMD_STAT_RX, statRx); break;
    case CMD_STAT_TX: send4(CMD_STAT_TX, statTx); break;
    case CMD_RANDOM: send1(CMD_RANDOM, (uint8_t)esp_random()); break;
    case CMD_BLINK: ledPulse(0); break;
    default: break;  // display mirroring, ROM, firmware update etc: not supported, ignored safely
  }
}

void feed(uint8_t c) {
  if (c == FEND) {
    if (inFrame && cmd != 0xFE) handle();
    inFrame = true; esc = false; cmd = 0xFE; cmdLen = 0;
    return;
  }
  if (!inFrame) return;
  if (cmd == 0xFE) { cmd = c; return; }
  if (c == FESC) { esc = true; return; }
  if (esc) { c = c == TFEND ? FEND : c == TFESC ? FESC : c; esc = false; }
  if (cmdLen < (int)sizeof cmdBuf) cmdBuf[cmdLen++] = c;
}

void bleStartRnode() {
  Preferences p;
  p.begin("rhizo", false);
  pin = p.getUInt("pin", 0);
  if (!pin) { pin = 100000 + esp_random() % 900000; p.putUInt("pin", pin); }
  p.end();
  uint8_t mac[6];
  esp_read_mac(mac, ESP_MAC_BT);
  snprintf(devName, sizeof devName, "RNode %02X%02X", mac[4], mac[5]);  // Sideband looks for "RNode ..."
  if (!inbox) inbox = xStreamBufferCreate(4096, 1);
  BLEDevice::init(devName);
  BLEDevice::setMTU(247);
  BLEDevice::setSecurityCallbacks(&secCb);
  BLESecurity *sec = new BLESecurity();
  sec->setPassKey(true, pin);           // fixed 6-digit PIN, shown on screen
  sec->setCapability(ESP_IO_CAP_OUT);   // we display, the phone types
  sec->setAuthenticationMode(true, true, true);  // bonding, MITM protection, secure connections
  server = BLEDevice::createServer();
  server->setCallbacks(&srvCb);
  BLEService *svc = server->createService(NUS_SVC);
  BLECharacteristic *rx = svc->createCharacteristic(NUS_RX, BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR |
                                                                BLECharacteristic::PROPERTY_WRITE_ENC);
  rx->setCallbacks(&rxCb);
  txChar = svc->createCharacteristic(NUS_TX, BLECharacteristic::PROPERTY_NOTIFY | BLECharacteristic::PROPERTY_READ_ENC);
  svc->start();
  BLEAdvertising *adv = BLEDevice::getAdvertising();
  adv->addServiceUUID(NUS_SVC);
  adv->setScanResponse(true);
  BLEDevice::startAdvertising();
}
void bleStopRnode() {
  BLEDevice::deinit(false);
  server = nullptr; txChar = nullptr; connected = false; bonded = false;
}

const char *RHIZO_HELP =
    "Rhizomorph turns the Cardputer + Cap into an RNode: a LoRa radio for Reticulum apps like Sideband. "
    "Your phone does the messaging and encryption; CapDeck just carries the packets.\n\n"
    "FIRST TIME:\n"
    "1. Keep this screen open.\n"
    "2. On the phone, Bluetooth settings: pair with \"RNode ....\" and type the PIN shown here.\n"
    "3. In Sideband: Connectivity > Connect via RNode, turn on \"Use Bluetooth\" (BLE), and set the radio: "
    "frequency, bandwidth, SF, coding rate, TX power. Everyone you want to reach must use the same settings.\n"
    "4. Start Sideband's RNode interface. The top line here changes to \"tethered\".\n\n"
    "While Rhizomorph is open, Mycelium (Meshtastic) is paused: one radio, one network at a time. "
    "The screen can sleep and it keeps relaying.\n\n"
    "Frequencies must be inside your Band (LoRa > Radio settings). Use the right antenna.\n\n"
    "Experimental: built from Reticulum's published host code, not yet tested against Sideband on real hardware.";
}  // namespace

void appRhizomorph() {
  if (!loraPresent) { toast("Rhizomorph needs the Cap LoRa", col::WARN, 1600); return; }
  RadioMode prev = radioMode;
  loraSetMode(RM_SCAN);  // we own the radio while open
  rp = Radio();
  statRx = statTx = 0;
  status = "waiting for Sideband";
  warn = "";
  bleStartRnode();
  rhizoRunning = true;
  uint32_t lastDraw = 0, lastBat = 0;
  while (true) {
    Key k = readKey();
    uint8_t tmp[256];
    size_t got;
    while (inbox && (got = xStreamBufferReceive(inbox, tmp, sizeof tmp, 0)) > 0)
      for (size_t i = 0; i < got; i++) feed(tmp[i]);
    if (rp.state == RADIO_STATE_ON) pollLora();
    if (connected && millis() - lastBat > 10000) {  // battery state for Sideband's status page
      lastBat = millis();
      uint8_t b[2] = {(uint8_t)(batteryCharging() ? 0x02 : 0x01), (uint8_t)constrain((int)M5Cardputer.Power.getBatteryLevel(), 0, 100)};
      sendFrame(CMD_STAT_BAT, b, 2);
    }
    if (k.back) {
      if (!connected || confirmBox("Stop relaying for Sideband?", col::WARN)) break;
      lastDraw = 0;
    }
    if (k.ch == 'h' || k.ch == '?') { textViewer("Rhizomorph", RHIZO_HELP, col::LORA); lastDraw = 0; }
    if (screenIsOff()) { delay(2); continue; }
    if (millis() - lastDraw < 250 && !k.any) { delay(2); continue; }
    lastDraw = millis();

    drawHeader("Rhizomorph", col::LORA, ICON_RHIZO);
    canvas.setFont(&fonts::Font0);
    canvas.setTextDatum(top_left);
    // the one-line status, terminal style
    String top = "> rhizomorph :: " + (rp.state == RADIO_STATE_ON ? String("tethered") : connected ? String("linked") : String("listening"));
    if (rp.freq) top += " :: " + String(rp.freq / 1e6, 1) + " MHz SF" + String(rp.sf);
    canvas.setTextColor(col::LORA);
    canvas.drawString(top, 4, BAR_H + 4);
    int y = BAR_H + 18;
    canvas.setTextColor(col::TEXT);
    canvas.setFont(&fonts::Font2);
    if (!connected) {
      canvas.drawString(String("Pair \"") + devName + "\"  PIN:", 6, y);
      canvas.setFont(&fonts::FreeSansBold12pt7b);
      char pb[8]; snprintf(pb, sizeof pb, "%06lu", (unsigned long)pin);
      canvas.setTextColor(accent());
      canvas.drawString(pb, 6, y + 16);
      canvas.setFont(&fonts::Font0);
      canvas.setTextColor(col::DIM);
      canvas.drawString("then start the RNode interface in Sideband", 6, y + 44);
    } else {
      canvas.drawString(status, 6, y);
      canvas.setFont(&fonts::Font0);
      canvas.setTextColor(col::DIM);
      canvas.drawString("in " + String(statRx) + "   out " + String(statTx) + (statRx ? "   last " + String(lastRssi, 0) + " dBm" : String("")), 6, y + 18);
      if (rp.bw) canvas.drawString("BW " + String(rp.bw / 1000) + " kHz  CR 4/" + String(rp.cr) + "  " + String(rp.txp) + " dBm", 6, y + 30);
    }
    if (warn.length()) {
      std::vector<String> w;
      wrapText(warn, SCR_W - 12, w);
      canvas.setTextColor(col::WARN);
      for (size_t i = 0; i < w.size() && i < 2; i++) canvas.drawString(w[i], 6, SCR_H - 30 + i * 10);
    }
    canvas.setTextColor(col::FAINT);
    canvas.setTextDatum(bottom_right);
    canvas.drawString("H help   DEL stop", SCR_W - 4, SCR_H - 2);
    pushFrame();
  }
  rhizoRunning = false;
  radioDown();
  bleStopRnode();
  loraSetMode(prev == RM_SCAN ? (cfg.meshListen ? RM_MESH : RM_CAPDECK) : prev);
}
