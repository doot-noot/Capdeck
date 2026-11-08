// CapDeck - Local AI: chat with your own llama.cpp / Ollama / LM Studio server
// Uses the OpenAI-compatible /v1/chat/completions endpoint those servers all provide.
#include "core.h"
#include <ArduinoJson.h>
#include <HTTPClient.h>

static String stripThinking(String s) {
  // reasoning models wrap their scratch work in <think>...</think>; hide it on a 240px screen
  int a = s.indexOf("<think>");
  while (a >= 0) {
    int b = s.indexOf("</think>", a);
    if (b < 0) { s.remove(a); break; }
    s.remove(a, b + 8 - a);
    a = s.indexOf("<think>");
  }
  s.trim();
  return s;
}

String aiChat(const std::vector<std::pair<String, String>> &history, String &err) {
  if (!cfg.aiUrl.length()) { err = "No server set. LOCAL AI > Server address."; return ""; }
  JsonDocument req;
  req["model"] = cfg.aiModel.length() ? cfg.aiModel : String("local");
  req["max_tokens"] = 500;
  req["stream"] = false;
  JsonArray msgs = req["messages"].to<JsonArray>();
  if (cfg.aiSystem.length()) {
    JsonObject m = msgs.add<JsonObject>();
    m["role"] = "system";
    m["content"] = cfg.aiSystem;
  }
  // keep the request small: last 10 turns
  size_t start = history.size() > 10 ? history.size() - 10 : 0;
  for (size_t i = start; i < history.size(); i++) {
    JsonObject m = msgs.add<JsonObject>();
    m["role"] = history[i].first;
    m["content"] = history[i].second;
  }
  String body;
  serializeJson(req, body);
  HTTPClient http;
  http.setConnectTimeout(5000);
  http.setTimeout(65000);  // local models can take a while (max for HTTPClient)
  String url = cfg.aiUrl;
  if (!url.endsWith("/v1")) url += "/v1";
  if (!http.begin(url + "/chat/completions")) { err = "Bad server address"; return ""; }
  http.addHeader("Content-Type", "application/json");
  if (cfg.aiKey.length()) http.addHeader("Authorization", "Bearer " + cfg.aiKey);
  int code = http.POST(body);
  if (code != 200) {
    err = code < 0 ? "Can't reach " + cfg.aiUrl + " (is the server running and listening on the network, not just localhost?)"
                   : "Server said HTTP " + String(code) + ": " + http.getString().substring(0, 120);
    http.end();
    return "";
  }
  JsonDocument filter;
  filter["choices"][0]["message"]["content"] = true;
  filter["error"] = true;
  JsonDocument resp;
  String raw = http.getString();  // handles chunked replies too
  http.end();
  DeserializationError de = deserializeJson(resp, raw, DeserializationOption::Filter(filter));
  if (de) { err = String("Bad reply: ") + de.c_str(); return ""; }
  String out = resp["choices"][0]["message"]["content"] | "";
  if (!out.length()) err = "Empty reply";
  return stripThinking(out);
}

static std::vector<std::pair<String, String>> history;  // role, content (kept while powered on)

static void chat() {
  if (!cfg.aiUrl.length()) {
    toast("Set the server address first (Local AI > Server address)", col::WARN, 2000);
    return;
  }
  if (!wifiWait(15000, "Connecting")) { toast("Wi-Fi didn't connect", col::BAD); return; }
  String input, status = "ready";
  uint16_t sc = col::DIM;
  int scrollUp = 0;
  bool dirty = true;
  uint32_t blink = 0;
  auto view = [&]() {
    std::vector<std::pair<String, bool>> v;
    for (auto &h : history) v.push_back({h.second, h.first == "user"});
    return v;
  };
  while (true) {
    Key k = readKey();
    if (k.any) dirty = true;
    if (k.esc || (k.del && !input.length())) return;
    if (k.fn && k.up) scrollUp++;
    if (k.fn && k.down) scrollUp = max(0, scrollUp - 1);
    if (k.del && input.length()) input.remove(input.length() - 1);
    else if (k.enter && input.length()) {
      history.push_back({"user", input});
      input = "";
      scrollUp = 0;
      auto v = view();
      drawChatView("Local AI", col::AI, ICON_AI, v, 0, &input, "thinking...", col::WARN);
      pushFrame();
      String err;
      String reply = aiChat(history, err);
      if (reply.length()) {
        history.push_back({"assistant", reply});
        status = "ready";
        sc = col::DIM;
        beep(2600, 20);
      } else {
        history.pop_back();
        toast(err, col::BAD, 2600);
        status = "error";
        sc = col::BAD;
      }
      if (history.size() > 30) history.erase(history.begin(), history.begin() + 2);
    } else if (k.space) input += ' ';
    else if (k.ch && !k.fn && input.length() < 300) input += k.ch;
    if (millis() - blink > 450) { blink = millis(); dirty = true; }
    if (!dirty) { delay(10); continue; }
    dirty = false;
    auto v = view();
    if (v.empty()) v.push_back({"Ask your local model anything. Fn+; / Fn+. scroll, DEL on an empty line exits.", false});
    drawChatView("Local AI", col::AI, ICON_AI, v, scrollUp, &input, status, sc);
    pushFrame();
  }
}

static void testServer() {
  if (!cfg.aiUrl.length()) { toast("No server address set", col::WARN); return; }
  if (!wifiWait(15000, "Connecting")) return;
  drawHeader("Testing...", col::AI, ICON_AI);
  pushFrame();
  HTTPClient http;
  http.setConnectTimeout(4000);
  http.setTimeout(6000);
  String url = cfg.aiUrl;
  if (!url.endsWith("/v1")) url += "/v1";
  http.begin(url + "/models");
  if (cfg.aiKey.length()) http.addHeader("Authorization", "Bearer " + cfg.aiKey);
  int code = http.GET();
  String text;
  if (code == 200) {
    JsonDocument d;
    deserializeJson(d, http.getString());
    text = "Server is up!\n\nModels:\n";
    for (JsonObject m : d["data"].as<JsonArray>()) text += "  " + String(m["id"] | "?") + "\n";
    text += "\nUsing: " + (cfg.aiModel.length() ? cfg.aiModel : String("(server default)"));
  } else {
    text = "No answer from " + url + "/models (" + (code < 0 ? String("connection failed") : "HTTP " + String(code)) + ").\n\n"
           "llama-server: start it with --host 0.0.0.0 --port 8080 so other devices can reach it.\n"
           "Ollama: set OLLAMA_HOST=0.0.0.0 and use port 11434.\n"
           "Also allow the port through the Windows firewall.";
  }
  http.end();
  textViewer("AI server", text, col::AI);
}

bool saveAiToConfig();

static const char *AI_HELP =
    "Talks to any OpenAI-compatible server on your network: llama.cpp's llama-server, Ollama, LM Studio, vLLM...\n\n"
    "Server address examples:\n  http://192.168.1.50:8080   (llama-server)\n  http://192.168.1.50:11434  (Ollama)\n\n"
    "Model can stay blank for llama-server. For Ollama, type the model name (e.g. llama3.2).\n\n"
    "Easiest setup: open PORTAL on the Cardputer, join its Wi-Fi from your phone and edit everything "
    "there - or chat with the AI from the phone through the portal.";

static const char *AI_SETUP =
    "Local AI runs on YOUR computer, not on the Cardputer. CapDeck is just the keyboard and screen.\n\n"
    "1. On a PC on the same Wi-Fi, install Ollama (ollama.com) or LM Studio.\n"
    "2. Get a model: in a terminal, ollama pull llama3.2\n"
    "3. Let other devices in:\n"
    "   Ollama: set OLLAMA_HOST=0.0.0.0 then restart it\n"
    "   LM Studio: Developer tab > Serve on local network\n"
    "4. Find the PC's IP (Windows: ipconfig, look for IPv4).\n"
    "5. Here, Server address = http://THAT-IP:11434 (Ollama) or :1234 (LM Studio).\n"
    "6. Test connection. If it fails, allow the port through the PC's firewall.\n\n"
    "Model: leave blank for the server default, or type e.g. llama3.2.";

void appAI() {
  int sel = 0;
  static bool hinted = false;
  if (!cfg.aiUrl.length() && !hinted) { hinted = true; textViewer("Set up Local AI", AI_SETUP, col::AI); }
  while (true) {
    std::vector<ListItem> items = {{"Chat", history.size() ? String(history.size() / 2) + " turns" : "", ICON_AI, 0},
                                   {"Server address", cfg.aiUrl.length() ? cfg.aiUrl.substring(7) : "not set", ICON_WIFI, 0},
                                   {"Model", cfg.aiModel.length() ? cfg.aiModel : "default", ICON_CONF, 0},
                                   {"Test connection", "", ICON_SCAN, 0},
                                   {"New conversation", "", ICON_POWER, 0},
                                   {"Setup guide", cfg.aiUrl.length() ? "" : "start here", ICON_INFO, cfg.aiUrl.length() ? (uint16_t)0 : col::WARN},
                                   {"Help", "", ICON_INFO, 0}};
    int r = listMenu("Local AI", col::AI, items, sel, ICON_AI);
    if (r < 0) return;
    switch (r) {
      case 0: chat(); break;
      case 1: {
        String v = cfg.aiUrl.length() ? cfg.aiUrl : "http://192.168.1.";
        if (textInput("Server (http://IP:port)", v, col::AI)) {
          v.trim();
          while (v.endsWith("/")) v.remove(v.length() - 1);
          if (!v.startsWith("http")) v = "http://" + v;
          cfg.aiUrl = v;
          if (!saveAiToConfig()) toast("Saved for now (no SD to store it)", col::WARN, 1200);
        }
        break;
      }
      case 2:
        if (textInput("Model name (blank = default)", cfg.aiModel, col::AI, false, 60)) saveAiToConfig();
        break;
      case 3: testServer(); break;
      case 4: history.clear(); toast("Cleared", col::OK, 600); break;
      case 5: textViewer("Set up Local AI", AI_SETUP, col::AI); break;
      case 6: textViewer("Local AI help", AI_HELP, col::AI); break;
    }
  }
}
