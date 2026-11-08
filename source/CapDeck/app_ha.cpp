// CapDeck - Home Assistant: trigger deck, push-to-talk Assist, typed Assist chat
#include "core.h"
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WebSocketsClient.h>

bool haReady() { return cfg.haUrl.length() > 7 && cfg.haToken.length() > 20; }

static void parseUrl(String &host, uint16_t &port) {
  String u = cfg.haUrl;
  bool https = u.startsWith("https://");
  u.replace("https://", "");
  u.replace("http://", "");
  int slash = u.indexOf('/');
  if (slash >= 0) u = u.substring(0, slash);
  int colon = u.indexOf(':');
  if (colon >= 0) {
    host = u.substring(0, colon);
    port = u.substring(colon + 1).toInt();
  } else {
    host = u;
    port = https ? 443 : 80;
  }
}

int haPost(const String &path, const String &body, String *resp, bool auth) {
  HTTPClient http;
  http.setTimeout(8000);
  http.setConnectTimeout(4000);
  if (!http.begin(cfg.haUrl + path)) return -100;
  http.addHeader("Content-Type", "application/json");
  if (auth) http.addHeader("Authorization", "Bearer " + cfg.haToken);
  int code = http.POST(body);
  if (resp && code > 0) *resp = http.getString();
  http.end();
  return code;
}

static int haGet(const String &path, String *resp) {
  HTTPClient http;
  http.setTimeout(5000);
  http.setConnectTimeout(3000);
  if (!http.begin(cfg.haUrl + path)) return -100;
  http.addHeader("Authorization", "Bearer " + cfg.haToken);
  int code = http.GET();
  if (resp && code > 0) *resp = http.getString();
  http.end();
  return code;
}

static String codeMeaning(int code) {
  if (code == 401) return "401 - token rejected";
  if (code == 404) return "404 - not found (check the name)";
  if (code == 400) return "400 - bad request";
  if (code < 0) return "can't reach " + cfg.haUrl;
  return "HTTP " + String(code);
}

static bool ensureHA() {
  if (!haReady()) {
    toast("Add your Home Assistant url + token to /capdeck/config.json on the SD card", col::WARN, 3000);
    return false;
  }
  if (!wifiWait(15000, "Connecting")) {
    toast("Wi-Fi didn't connect. Check Config > Wi-Fi.", col::BAD, 2200);
    return false;
  }
  return true;
}

// ------------------------------------------------------------------ TTS playback
// Streams a 16 kHz mono WAV from Home Assistant straight to the speaker.
class WavSink : public Stream {
 public:
  static const int N = 3, LEN = 1024;
  int16_t bufs[N][LEN];
  int cur = 0, fill = 0;
  uint32_t rate = 16000;

  void flushBuf() {
    if (!fill) return;
    while (M5Cardputer.Speaker.isPlaying(0) >= 2) delay(1);
    uint32_t r = (rate >= 8000 && rate <= 48000) ? rate : 16000;
    M5Cardputer.Speaker.playRaw(bufs[cur], fill, r, false, 1, 0);
    cur = (cur + 1) % N;
    fill = 0;
  }
  size_t write(uint8_t b) override { return write(&b, 1); }
  size_t write(const uint8_t *d, size_t n) override {
    for (size_t i = 0; i < n; i++) feed(d[i]);
    return n;
  }
  int available() override { return 0; }
  int read() override { return -1; }
  int peek() override { return -1; }
  void flush() override { flushBuf(); }

 private:
  enum { RIFF, CHUNK, FMT, SKIP, DATA } st = RIFF;
  uint8_t h[12];
  int hn = 0;
  uint32_t left = 0, pos = 0;
  uint8_t lo = 0;
  bool haveLo = false;

  void feed(uint8_t b) {
    switch (st) {
      case RIFF:
        h[hn++] = b;
        if (hn == 12) {
          hn = 0;
          if (memcmp(h, "RIFF", 4) == 0) st = CHUNK;
          else {  // not a WAV: treat as raw 16-bit PCM
            st = DATA;
            for (int i = 0; i < 12; i++) feed(h[i]);
          }
        }
        break;
      case CHUNK:
        h[hn++] = b;
        if (hn == 8) {
          hn = 0;
          left = h[4] | (h[5] << 8) | (h[6] << 16) | ((uint32_t)h[7] << 24);
          pos = 0;
          if (memcmp(h, "data", 4) == 0) st = DATA;
          else if (memcmp(h, "fmt ", 4) == 0) st = left ? FMT : CHUNK;
          else st = left ? SKIP : CHUNK;
        }
        break;
      case FMT:  // sample rate lives at bytes 4..7 of the fmt chunk
        if (pos >= 4 && pos < 8) {
          if (pos == 4) rate = 0;
          rate |= (uint32_t)b << (8 * (pos - 4));
        }
        pos++;
        if (--left == 0) st = CHUNK;
        break;
      case SKIP:
        if (--left == 0) st = CHUNK;
        break;
      case DATA:
        if (!haveLo) { lo = b; haveLo = true; break; }
        haveLo = false;
        bufs[cur][fill++] = (int16_t)(lo | (b << 8));
        if (fill == LEN) flushBuf();
        break;
    }
  }
};

static void speak(const String &text) {
  if (!cfg.sound || !cfg.haTts.length() || !text.length()) return;
  JsonDocument req;
  req["engine_id"] = cfg.haTts;
  req["message"] = text;
  req["options"]["preferred_format"] = "wav";
  req["options"]["preferred_sample_rate"] = 16000;
  req["options"]["preferred_sample_channels"] = 1;
  req["options"]["preferred_sample_bytes"] = 2;
  String body, resp;
  serializeJson(req, body);
  int code = haPost("/api/tts_get_url", body, &resp);
  if (code != 200) return;
  JsonDocument r;
  if (deserializeJson(r, resp)) return;
  String path = r["path"] | "";
  if (!path.length()) return;
  HTTPClient http;
  http.setTimeout(8000);
  if (!http.begin(cfg.haUrl + path)) return;
  if (http.GET() == 200) {
    WavSink *sink = new WavSink();
    M5Cardputer.Speaker.begin();
    http.writeToStream(sink);
    sink->flush();
    while (M5Cardputer.Speaker.isPlaying()) {
      M5Cardputer.update();
      if (M5Cardputer.Keyboard.isPressed()) { M5Cardputer.Speaker.stop(); break; }
      delay(5);
    }
    delete sink;
  }
  http.end();
}

// ------------------------------------------------------------------ trigger deck
static uint8_t iconForService(const HAButton &b) {
  if (b.webhook.length()) return ICON_WIFI;
  String d = b.service.substring(0, b.service.indexOf('.'));
  if (d == "light") return ICON_DISPLAY;
  if (d == "switch" || d == "input_boolean") return ICON_POWER;
  if (d == "script" || d == "automation") return ICON_CONF;
  if (d == "media_player") return ICON_SOUND;
  if (d == "climate") return ICON_CLOCK;
  return ICON_HA;
}

// Non-interactive HA calls for the Macros engine. Return "" on success or an error string.
String haCallService(const String &service, const String &entity, const String &dataJson) {
  int dot = service.indexOf('.');
  if (dot < 0) return "service must look like light.toggle";
  JsonDocument body;
  if (dataJson.length()) deserializeJson(body, dataJson);
  if (entity.length()) body["entity_id"] = entity;
  String s;
  serializeJson(body, s);
  if (s == "null") s = "{}";
  int code = haPost("/api/services/" + service.substring(0, dot) + "/" + service.substring(dot + 1), s);
  return (code >= 200 && code < 300) ? "" : codeMeaning(code);
}

String haCallWebhook(const String &id) {
  int code = haPost("/api/webhook/" + id, "{\"source\":\"capdeck\"}", nullptr, false);
  return (code >= 200 && code < 300) ? "" : codeMeaning(code);
}

static bool fireButton(const HAButton &b, String &err) {
  int code;
  if (b.webhook.length()) {
    code = haPost("/api/webhook/" + b.webhook, "{\"source\":\"capdeck\",\"button\":\"" + b.name + "\"}", nullptr, false);
  } else {
    int dot = b.service.indexOf('.');
    if (dot < 0) { err = "service must look like light.toggle"; return false; }
    JsonDocument body;
    if (b.data.length()) deserializeJson(body, b.data);
    if (b.entity.length()) body["entity_id"] = b.entity;
    String s;
    serializeJson(body, s);
    if (s == "null") s = "{}";
    code = haPost("/api/services/" + b.service.substring(0, dot) + "/" + b.service.substring(dot + 1), s);
  }
  if (code >= 200 && code < 300) return true;
  err = codeMeaning(code);
  return false;
}

static String entityState(const String &entity) {
  String resp;
  if (haGet("/api/states/" + entity, &resp) != 200) return "";
  JsonDocument d;
  JsonDocument filter;
  filter["state"] = true;
  if (deserializeJson(d, resp, DeserializationOption::Filter(filter))) return "";
  return d["state"] | "";
}

static void deck() {
  if (cfg.haButtons.empty()) {
    toast("No buttons yet - add them under \"ha_buttons\" in /capdeck/config.json", col::WARN, 2600);
    return;
  }
  if (!ensureHA()) return;
  std::vector<ListItem> items;
  drawHeader("Loading states...", col::HA, ICON_HA);
  pushFrame();
  for (auto &b : cfg.haButtons) {
    ListItem it{b.name, "", iconForService(b), 0};
    if (b.entity.length() && cfg.haButtons.size() <= 16) it.right = entityState(b.entity);
    items.push_back(it);
  }
  int sel = 0;
  while (true) {
    int r = listMenu("Trigger deck", col::HA, items, sel, ICON_HA);
    if (r < 0) return;
    const HAButton &b = cfg.haButtons[r];
    drawHeader("Sending...", col::HA, ICON_HA);
    drawIcon(canvas, items[r].icon, SCR_W / 2, 76, 48, col::HA);
    pushFrame();
    String err;
    if (fireButton(b, err)) {
      beep(3000, 30);
      if (b.entity.length()) {
        delay(250);
        items[r].right = entityState(b.entity);
      }
      toast("OK  " + b.name + (items[r].right.length() ? "  -> " + items[r].right : ""), col::OK, 800);
    } else {
      beep(500, 200);
      toast("FAILED  " + b.name + "\n" + err, col::BAD, 2500);
    }
  }
}

// ------------------------------------------------------------------ chat rendering
struct ChatLine {
  String text;
  bool me;
};

static void drawChat(const char *title, std::vector<ChatLine> &chat, int scrollUp, const String &input, const String &status,
                     uint16_t statusColor, bool showInput) {
  drawHeader(title, col::HA, ICON_HA);
  int bottom = showInput ? SCR_H - 22 : SCR_H - 12;
  canvas.setFont(&fonts::Font2);
  // build wrapped lines from the newest backwards
  struct L { String s; bool me; };
  std::vector<L> lines;
  for (auto &c : chat) {
    std::vector<String> w;
    wrapText(c.text, SCR_W - 40, w);
    for (auto &s : w) lines.push_back({s, c.me});
    lines.push_back({"", c.me});
  }
  int lh = 15;
  int y = bottom - lh;
  int start = (int)lines.size() - 1 - scrollUp;
  for (int i = start; i >= 0 && y >= BAR_H + 18; i--, y -= lh) {
    if (!lines[i].s.length()) { y += lh / 2; continue; }
    canvas.setTextDatum(top_left);
    int w = canvas.textWidth(lines[i].s);
    if (lines[i].me) {
      canvas.fillRoundRect(SCR_W - w - 14, y, w + 10, lh, 4, col::HA);
      canvas.setTextColor(col::BG);
      canvas.drawString(lines[i].s, SCR_W - w - 9, y + 0);
    } else {
      canvas.fillRoundRect(4, y, w + 10, lh, 4, col::PANEL2);
      canvas.setTextColor(col::TEXT);
      canvas.drawString(lines[i].s, 9, y + 0);
    }
  }
  canvas.setFont(&fonts::Font0);
  if (showInput) {
    canvas.fillRect(0, SCR_H - 20, SCR_W, 20, col::PANEL);
    canvas.drawFastHLine(0, SCR_H - 20, SCR_W, col::HA);
    canvas.setFont(&fonts::Font2);
    canvas.setTextDatum(middle_left);
    canvas.setTextColor(input.length() ? col::TEXT : col::DIM);
    String shown = input.length() ? input : String("type a message, ENTER sends");
    while (canvas.textWidth(shown) > SCR_W - 14) shown.remove(0, 1);
    canvas.drawString(shown, 5, SCR_H - 10);
    if (input.length() && (millis() / 450) % 2) canvas.fillRect(6 + canvas.textWidth(shown), SCR_H - 17, 2, 14, col::HA);
  }
  if (status.length()) {
    canvas.setFont(&fonts::Font0);
    canvas.setTextDatum(middle_right);
    canvas.setTextColor(statusColor);
    canvas.drawString(status, SCR_W - 6, BAR_H + 9);
  }
}

static String extractSpeech(JsonVariant response) {
  String s = response["speech"]["plain"]["speech"] | "";
  if (!s.length()) s = response["speech"]["ssml"]["speech"] | "";
  return s;
}

// ------------------------------------------------------------------ typed assist
static void typeAssist() {
  if (!ensureHA()) return;
  std::vector<ChatLine> chat;
  String input, cid, status = "ready";
  uint16_t sc = col::DIM;
  int scrollUp = 0;
  bool dirty = true;
  while (true) {
    Key k = readKey();
    if (k.any) dirty = true;
    if (k.esc || (k.del && !input.length())) return;
    if (k.fn && k.up) scrollUp++;
    if (k.fn && k.down) scrollUp = max(0, scrollUp - 1);
    if (k.del && input.length()) input.remove(input.length() - 1);
    else if (k.enter && input.length()) {
      chat.push_back({input, true});
      scrollUp = 0;
      drawChat("Assist", chat, 0, "", "thinking...", col::WARN, true);
      pushFrame();
      JsonDocument req;
      req["text"] = input;
      if (cid.length()) req["conversation_id"] = cid;
      if (cfg.haAgent.length()) req["agent_id"] = cfg.haAgent;
      String body, resp;
      serializeJson(req, body);
      input = "";
      int code = haPost("/api/conversation/process", body, &resp);
      if (code == 200) {
        JsonDocument r;
        deserializeJson(r, resp);
        cid = r["conversation_id"] | cid;
        String reply = extractSpeech(r["response"]);
        chat.push_back({reply.length() ? reply : String("(no reply)"), false});
        status = "ready";
        sc = col::DIM;
        drawChat("Assist", chat, 0, "", "speaking", col::OK, true);
        pushFrame();
        speak(reply);
      } else {
        chat.push_back({"Error: " + codeMeaning(code), false});
        status = "error";
        sc = col::BAD;
      }
      if (chat.size() > 30) chat.erase(chat.begin(), chat.begin() + 2);
    } else if (k.space) input += ' ';
    else if (k.ch && !k.fn && input.length() < 200) input += k.ch;
    static uint32_t blink = 0;
    if (millis() - blink > 450) { blink = millis(); dirty = true; }
    if (!dirty) { delay(10); continue; }
    dirty = false;
    drawChat("Assist", chat, scrollUp, input, status, sc, true);
    pushFrame();
  }
}

// ------------------------------------------------------------------ push-to-talk assist (websocket pipeline)
namespace ptt {
enum State { CONNECTING, AUTHING, READY, STARTING, LISTENING, THINKING, ERROR };
static WebSocketsClient *ws = nullptr;
static State state = CONNECTING;
static int msgId = 1, runId = 0;
static int handler = -1;
static String cid, heard, reply, err;
static bool replyReady = false;

static const int NB = 6, BL = 512;  // 32 ms buffers
static int16_t bufs[NB][BL];
static QueueHandle_t filledQ = nullptr;
static std::vector<int> freeBufs, pending;
static bool micOn = false;

static void onBufferDone(void *, void *data, size_t) {
  for (int i = 0; i < NB; i++)
    if (data == bufs[i]) { xQueueSend(filledQ, &i, 0); return; }
}

static void sendAudio(int i) {
  static uint8_t frame[1 + BL * 2];
  frame[0] = (uint8_t)handler;
  memcpy(frame + 1, bufs[i], BL * 2);
  ws->sendBIN(frame, sizeof frame);
}

static void micStart() {
  M5Cardputer.Speaker.end();
  M5Cardputer.Mic.setBufferReleaseCallback(nullptr, onBufferDone);
  M5Cardputer.Mic.begin();
  if (!filledQ) filledQ = xQueueCreate(NB, sizeof(int));
  xQueueReset(filledQ);
  freeBufs.clear();
  pending.clear();
  for (int i = 0; i < NB; i++) freeBufs.push_back(i);
  micOn = true;
}

static void micPump() {
  if (!micOn) return;
  while (M5Cardputer.Mic.isRecording() < 2 && !freeBufs.empty()) {
    int i = freeBufs.back();
    freeBufs.pop_back();
    if (!M5Cardputer.Mic.record(bufs[i], BL, 16000)) { freeBufs.push_back(i); break; }
  }
  int i;
  while (xQueueReceive(filledQ, &i, 0) == pdTRUE) {
    if (handler >= 0) {
      // flush anything recorded before HA told us the handler id
      for (int p : pending) { sendAudio(p); freeBufs.push_back(p); }
      pending.clear();
      sendAudio(i);
      freeBufs.push_back(i);
    } else if (pending.size() < NB - 2) {
      pending.push_back(i);  // keep it until the stream is ready
    } else {
      freeBufs.push_back(i);  // too early; drop
    }
  }
}

static void micStop() {
  if (!micOn) return;
  M5Cardputer.Mic.end();
  M5Cardputer.Mic.setBufferReleaseCallback(nullptr, nullptr);
  micOn = false;
  M5Cardputer.Speaker.begin();
}

static void onWs(WStype_t type, uint8_t *payload, size_t length) {
  if (type == WStype_DISCONNECTED) {
    if (state != ERROR) state = CONNECTING;
    return;
  }
  if (type != WStype_TEXT) return;
  JsonDocument d;
  if (deserializeJson(d, payload, length)) return;
  String t = d["type"] | "";
  if (t == "auth_required") {
    state = AUTHING;
    ws->sendTXT("{\"type\":\"auth\",\"access_token\":\"" + cfg.haToken + "\"}");
  } else if (t == "auth_ok") {
    state = READY;
  } else if (t == "auth_invalid") {
    state = ERROR;
    err = "Token rejected (auth_invalid)";
  } else if (t == "result" && d["id"] == runId && !(d["success"] | false)) {
    state = ERROR;
    err = String(d["error"]["message"] | "pipeline error");
  } else if (t == "event" && d["id"] == runId) {
    JsonObject ev = d["event"];
    String et = ev["type"] | "";
    if (et == "run-start") {
      handler = ev["data"]["runner_data"]["stt_binary_handler_id"] | -1;
    } else if (et == "stt-end") {
      heard = ev["data"]["stt_output"]["text"] | "";
    } else if (et == "intent-end") {
      JsonVariant out = ev["data"]["intent_output"];
      cid = out["conversation_id"] | cid;
      reply = extractSpeech(out["response"]);
    } else if (et == "error") {
      err = String(ev["data"]["message"] | "error");
      state = ERROR;
    } else if (et == "run-end") {
      replyReady = true;
    }
  }
}

static void startRun() {
  heard = reply = err = "";
  replyReady = false;
  handler = -1;
  runId = ++msgId;
  JsonDocument d;
  d["id"] = runId;
  d["type"] = "assist_pipeline/run";
  d["start_stage"] = "stt";
  d["end_stage"] = "intent";
  d["input"]["sample_rate"] = 16000;
  if (cfg.haPipeline.length()) d["pipeline"] = cfg.haPipeline;
  if (cid.length()) d["conversation_id"] = cid;
  String s;
  serializeJson(d, s);
  ws->sendTXT(s);
  state = STARTING;
}
}  // namespace ptt

static void drawTalk(int frame, std::vector<ChatLine> &chat) {
  using namespace ptt;
  const char *label;
  uint16_t c;
  switch (state) {
    case CONNECTING: label = "connecting..."; c = col::WARN; break;
    case AUTHING: label = "signing in..."; c = col::WARN; break;
    case READY: label = "hold SPACE / G0 to talk"; c = col::HA; break;
    case STARTING:
    case LISTENING: label = "listening - release to send"; c = col::OK; break;
    case THINKING: label = "thinking..."; c = col::WARN; break;
    default: label = "error - ENTER to retry"; c = col::BAD; break;
  }
  if (!chat.empty()) {
    drawChat("Talk", chat, 0, "", "", 0, false);
    canvas.fillRect(0, SCR_H - 12, SCR_W, 12, col::BG);
  } else {
    drawHeader("Talk to Assist", col::HA, ICON_HA);
    drawIcon(canvas, ICON_HA, SCR_W / 2, 70, 46, c);
  }
  if (state == LISTENING || state == STARTING) {
    // animated level ring in the corner
    for (int i = 0; i < 3; i++) {
      int r = 6 + ((frame * 3 + i * 7) % 21);
      canvas.drawCircle(SCR_W - 18, BAR_H + 30, r, col::OK);
    }
    canvas.fillCircle(SCR_W - 18, BAR_H + 30, 5, col::BAD);
  }
  canvas.setFont(&fonts::Font0);
  canvas.setTextDatum(bottom_center);
  canvas.setTextColor(c);
  canvas.drawString(state == ERROR && err.length() ? err.substring(0, 38) : String(label), SCR_W / 2, SCR_H - 2);
  pushFrame();
}

static void talkAssist() {
  using namespace ptt;
  if (!ensureHA()) return;
  String host;
  uint16_t port;
  parseUrl(host, port);
  if (cfg.haUrl.startsWith("https")) {
    toast("Talk mode needs your local http:// address (e.g. http://192.168.1.20:8123)", col::WARN, 2600);
    return;
  }
  ws = new WebSocketsClient();
  ws->begin(host, port, "/api/websocket");
  ws->onEvent(onWs);
  ws->setReconnectInterval(3000);
  state = CONNECTING;
  cid = "";
  std::vector<ChatLine> chat;
  int frame = 0;
  uint32_t lastDraw = 0, talkStart = 0;
  while (true) {
    ws->loop();
    micPump();
    bool held = false;
    Key k;
    if (state == STARTING || state == LISTENING) {
      sysTick();
      held = talkKeyHeld();
    } else {
      k = readKey();
      if (k.back) break;
      held = talkKeyHeld();
    }
    if (state == READY && held) {
      startRun();
      micStart();
      talkStart = millis();
      beep(1600, 30);
    }
    if (state == STARTING && handler >= 0) state = LISTENING;
    if ((state == STARTING || state == LISTENING) && (!held || millis() - talkStart > 12000)) {
      // released: drain the mic and close the audio stream
      uint32_t t = millis();
      while (millis() - t < 120) { micPump(); ws->loop(); delay(2); }
      micStop();
      if (handler >= 0) {
        uint8_t end = (uint8_t)handler;
        ws->sendBIN(&end, 1);
      }
      state = THINKING;
      beep(2400, 20);
    }
    if (state == THINKING && replyReady) {
      if (heard.length()) chat.push_back({heard, true});
      if (reply.length()) chat.push_back({reply, false});
      if (!heard.length() && !reply.length()) chat.push_back({"(didn't catch that)", false});
      if (chat.size() > 20) chat.erase(chat.begin(), chat.begin() + 2);
      state = READY;
      drawTalk(frame, chat);
      speak(reply);
    }
    if (state == ERROR) {
      micStop();
      if (k.enter) { state = ws->isConnected() ? READY : CONNECTING; err = ""; }
    }
    if (millis() - lastDraw > 60) {
      drawTalk(frame++, chat);
      lastDraw = millis();
    }
    delay(2);
  }
  micStop();
  ws->disconnect();
  delete ws;
  ws = nullptr;
}

// ------------------------------------------------------------------ status
static void haStatus() {
  if (!ensureHA()) return;
  String r, text;
  int code = haGet("/api/config", &r);
  if (code == 200) {
    JsonDocument d;
    deserializeJson(d, r);
    text = "Connected!\n\nName: " + String(d["location_name"] | "?") + "\nVersion: " + String(d["version"] | "?") +
           "\nTime zone: " + String(d["time_zone"] | "?");
  } else {
    text = "Couldn't talk to Home Assistant.\n\n" + codeMeaning(code);
  }
  text += "\n\nURL: " + cfg.haUrl + "\nDevice IP: " + WiFi.localIP().toString() + "\nWi-Fi: " + WiFi.SSID() + " (" +
          String(WiFi.RSSI()) + " dBm)";
  text += "\nTTS: " + (cfg.haTts.length() ? cfg.haTts : String("off")) +
          "\nPipeline: " + (cfg.haPipeline.length() ? cfg.haPipeline : String("default"));
  textViewer("HA status", text, col::HA);
}

static const char *HA_HELP =
    "SETUP\n"
    "1. In Home Assistant: your profile > Security > Long-lived access tokens > Create. Copy it.\n"
    "2. Put the SD card in a computer and edit /capdeck/config.json:\n"
    "   wifi: your network\n"
    "   home_assistant.url: http://<HA IP>:8123\n"
    "   home_assistant.token: the token\n"
    "3. Add buttons under ha_buttons. Each one is either\n"
    "   {name, service, entity} e.g. light.toggle + light.bedroom\n"
    "   or {name, webhook} to fire an automation's webhook trigger.\n"
    "   Optional \"data\": {...} adds service data (brightness, etc).\n\n"
    "TALK uses your default Assist pipeline (set Whisper + an LLM conversation agent in "
    "Settings > Voice assistants). Hold SPACE or the G0 button, speak, let go.\n"
    "Replies are spoken if tts_engine is set (e.g. tts.piper) - otherwise shown as text.\n\n"
    "TYPE sends text straight to the conversation agent.";

void appHomeAssistant() {
  int sel = 0;
  while (true) {
    std::vector<ListItem> items = {{"Trigger deck", String(cfg.haButtons.size()) + " buttons", ICON_HA, 0},
                                   {"Talk (push-to-talk)", "", ICON_SOUND, 0},
                                   {"Type to Assist", "", ICON_INFO, 0},
                                   {"Connection status", haReady() ? "" : "not set up", ICON_WIFI, 0},
                                   {"Setup help", "", ICON_CONF, 0}};
    int r = listMenu("Home Assistant", col::HA, items, sel, ICON_HA);
    if (r < 0) return;
    switch (r) {
      case 0: deck(); break;
      case 1: talkAssist(); break;
      case 2: typeAssist(); break;
      case 3: haStatus(); break;
      case 4: textViewer("HA setup", HA_HELP, col::HA); break;
    }
  }
}
