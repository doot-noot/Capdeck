// CapDeck - Duck: look things up with zero setup.
// No API key, no account, no phone. DuckDuckGo Instant Answers first (facts, definitions,
// summaries), then Wikipedia as a fallback when DDG has nothing - which is often, because the
// Instant Answer API is NOT a web-results list. Both are free and keyless.
#include "core.h"
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

static String urlEncode(const String &s) {
  static const char *hex = "0123456789ABCDEF";
  String o;
  o.reserve(s.length() * 3);
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    bool safe = (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '-' || c == '_' ||
                c == '.' || c == '~';
    if (safe) o += c;
    else if (c == ' ') o += '+';
    else { o += '%'; o += hex[(c >> 4) & 0xF]; o += hex[c & 0xF]; }
  }
  return o;
}

static bool httpGet(const String &url, String &body, String &err) {
  WiFiClientSecure tls;
  tls.setInsecure();  // no cert store on device; these are public read-only endpoints
  HTTPClient http;
  http.setTimeout(15000);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.setUserAgent("CapDeck/" CAPDECK_VERSION);
  if (!http.begin(tls, url)) { err = "Couldn't open the connection"; return false; }
  int code = http.GET();
  if (code != 200) {
    err = code > 0 ? "Server said HTTP " + String(code) : "No reply - check Wi-Fi";
    http.end();
    return false;
  }
  body = http.getString();
  http.end();
  if (body.length() > 90000) body = body.substring(0, 90000);  // keep the JSON parse off the heap ceiling
  return true;
}

// DuckDuckGo Instant Answers: abstracts, definitions, computed answers. Empty for most "find pages" queries.
static String ddgLookup(const String &q, String &src, String &err) {
  String body;
  String url = "https://api.duckduckgo.com/?q=" + urlEncode(q) + "&format=json&no_html=1&skip_disambig=1";
  if (!httpGet(url, body, err)) return "";
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
    src = (s.length() ? s : String("DuckDuckGo"));
    if (u.length()) src += "\n" + u;
  }
  return out;
}

// Wikipedia: search for the closest title, then pull that page's summary extract.
static String wikiLookup(const String &q, String &src, String &err) {
  String body;
  if (!httpGet("https://en.wikipedia.org/w/api.php?action=query&list=search&srlimit=1&format=json&srsearch=" +
                   urlEncode(q),
               body, err))
    return "";
  JsonDocument d;
  if (deserializeJson(d, body)) { err = "Couldn't read Wikipedia's reply"; return ""; }
  String title = d["query"]["search"][0]["title"] | "";
  if (!title.length()) return "";
  String body2;
  if (!httpGet("https://en.wikipedia.org/api/rest_v1/page/summary/" + urlEncode(title), body2, err)) return "";
  JsonDocument d2;
  if (deserializeJson(d2, body2)) { err = "Couldn't read Wikipedia's reply"; return ""; }
  String ex = d2["extract"] | "";
  if (ex.length()) src = "Wikipedia - " + title;
  return ex;
}

void appDuck() {
  String q;
  while (true) {
    if (!textInput("Ask the duck", q, col::AI, false, 100)) return;
    q.trim();
    if (!q.length()) continue;
    if (!wifiWait(15000, "Duck needs Wi-Fi")) { toast("No Wi-Fi - lookups need a connection", col::BAD, 1600); continue; }

    drawHeader("Duck", col::AI, ICON_DUCK);
    drawIcon(canvas, ICON_DUCK, SCR_W / 2, 62, 44, col::AI);
    canvas.setFont(&fonts::Font2);
    canvas.setTextDatum(middle_center);
    canvas.setTextColor(col::DIM);
    canvas.drawString(fitText("looking up " + q, SCR_W - 20), SCR_W / 2, 110);
    pushFrame();

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
    String title = fitText(q, 150);
    textViewer(title.c_str(), ans + "\n\n-- " + (src.length() ? src : String("DuckDuckGo")), col::AI);
  }
}
