// CapDeck - Macros: one tap fires a whole chain of actions.
// A macro is a name + a list of steps. Steps are dead simple - each is one little thing:
//   { "ha": "light.toggle", "entity": "light.bedroom" }   call a Home Assistant service
//   { "scene": "scene.movie" }                            turn on a Home Assistant scene
//   { "webhook": "capdeck_test" }                         fire a Home Assistant webhook
//   { "ir": "/capdeck/ir/My_Remotes/TCL_TV/TCL_Roku_TV.ir", "button": "Power" }   blast an IR signal
//   { "rf": "garage" }                                    send a saved 433 MHz code by name
//   { "lora": "on my way" }                               send a LoRa chat message
//   { "wait": 2 }                                         wait 2 seconds
//   { "say": "Movie time!" }                              show a little message on screen
//   { "beep": true }                                      chirp
// It all lives in /capdeck/macros.json, which you can edit right from the Portal on your phone.
#include "core.h"
#include <ArduinoJson.h>

static const char *MACROS_PATH = "/capdeck/macros.json";

static const char *MACROS_TEMPLATE = R"JSON({
  "_help": "Each macro is a name and a list of steps. One tap runs them top to bottom. Edit me from the Portal!",
  "macros": [
    {
      "name": "Movie Night",
      "icon": "movie",
      "steps": [
        { "say": "Lights, camera..." },
        { "ha": "light.turn_off", "entity": "light.living_room" },
        { "scene": "scene.movie" },
        { "wait": 1 },
        { "ir": "/capdeck/ir/My_Remotes/TCL_TV/TCL_Power_Try_All.ir", "button": "Power" },
        { "beep": true }
      ]
    },
    {
      "name": "Good Morning",
      "icon": "morning",
      "steps": [
        { "ha": "light.turn_on", "entity": "light.bedroom", "data": { "brightness_pct": 60 } },
        { "say": "Morning!" }
      ]
    },
    {
      "name": "On My Way",
      "icon": "lora",
      "steps": [
        { "lora": "Heading home now" },
        { "beep": true }
      ]
    }
  ]
})JSON";

void macroEnsureTemplate() {
  if (!sdOK) return;
  if (!SD.exists("/capdeck")) SD.mkdir("/capdeck");
  if (SD.exists(MACROS_PATH)) return;
  File f = SD.open(MACROS_PATH, FILE_WRITE);
  if (f) {
    f.print(MACROS_TEMPLATE);
    f.close();
  }
}

static uint8_t iconFor(const String &word) {
  String w = word;
  w.toLowerCase();
  if (w == "movie" || w == "tv" || w == "ir") return ICON_IR;
  if (w == "light" || w == "lights" || w == "morning" || w == "day") return ICON_DISPLAY;
  if (w == "home" || w == "house" || w == "ha") return ICON_HA;
  if (w == "sound" || w == "music" || w == "night") return ICON_SOUND;
  if (w == "lora" || w == "radio") return ICON_LORA;
  if (w == "mesh") return ICON_MESH;
  if (w == "fan" || w == "power" || w == "off") return ICON_POWER;
  if (w == "ai") return ICON_AI;
  if (w == "clock" || w == "timer") return ICON_CLOCK;
  return ICON_MACRO;
}

// How many macros are defined (for the menu subtitle). Light touch: parse and count.
int macroCount() {
  if (!sdOK) return 0;
  File f = SD.open(MACROS_PATH, FILE_READ);
  if (!f) return 0;
  JsonDocument d;
  JsonDocument filter;
  filter["macros"][0]["name"] = true;
  int n = 0;
  if (!deserializeJson(d, f, DeserializationOption::Filter(filter))) n = d["macros"].as<JsonArray>().size();
  f.close();
  return n;
}

// ------------------------------------------------------------------ running one macro
struct RunStep {
  String label;   // what we show
  int status;     // 0 pending, 1 running, 2 ok, 3 fail, 4 skipped
  String note;    // error or result
};

static void drawRun(const String &name, std::vector<RunStep> &steps, int top) {
  drawHeader(name.c_str(), col::HA, ICON_MACRO);
  canvas.setFont(&fonts::Font2);
  const int rowH = 16;
  int rows = (SCR_H - (BAR_H + 20)) / rowH;
  int scroll = max(0, top - rows + 2);
  for (int r = 0; r < rows && scroll + r < (int)steps.size(); r++) {
    int i = scroll + r;
    int y = BAR_H + 22 + r * rowH;
    RunStep &s = steps[i];
    uint16_t c = s.status == 2 ? col::OK : s.status == 3 ? col::BAD : s.status == 1 ? col::WARN : col::DIM;
    // status bullet
    canvas.setTextDatum(middle_left);
    const char *mark = s.status == 2 ? "+" : s.status == 3 ? "x" : s.status == 1 ? ">" : s.status == 4 ? "-" : ".";
    canvas.setTextColor(c);
    canvas.drawString(mark, 6, y + 7);
    canvas.setTextColor(s.status == 0 ? col::DIM : col::TEXT);
    String line = s.label;
    if (s.note.length() && s.status == 3) line += " - " + s.note;
    while (canvas.textWidth(line) > SCR_W - 24) line.remove(line.length() - 1);
    canvas.drawString(line, 18, y + 7);
  }
  pushFrame();
}

// Run one step. Returns "" on success, or an error. Sets skip=true to mark it skipped (not failed).
static String runStep(JsonObject step, bool &skip) {
  skip = false;
  if (step["ha"].is<const char *>()) {
    if (!haReady()) { skip = true; return "Home Assistant not set up"; }
    if (!wifiWait(15000, "Wi-Fi")) { skip = true; return "no Wi-Fi"; }
    String data;
    if (step["data"].is<JsonObject>()) serializeJson(step["data"], data);
    return haCallService(step["ha"].as<String>(), step["entity"] | "", data);
  }
  if (step["scene"].is<const char *>()) {
    if (!haReady()) { skip = true; return "Home Assistant not set up"; }
    if (!wifiWait(15000, "Wi-Fi")) { skip = true; return "no Wi-Fi"; }
    return haCallService("scene.turn_on", step["scene"].as<String>(), "");
  }
  if (step["webhook"].is<const char *>()) {
    if (!haReady()) { skip = true; return "Home Assistant not set up"; }
    if (!wifiWait(15000, "Wi-Fi")) { skip = true; return "no Wi-Fi"; }
    return haCallWebhook(step["webhook"].as<String>());
  }
  if (step["ir"].is<const char *>()) {
    return irSendSignal(step["ir"].as<String>(), step["button"] | "");
  }
  if (step["rf"].is<const char *>()) {
    return rfSendSaved(step["rf"].as<String>());
  }
  if (step["lora"].is<const char *>()) {
    if (radioMode == RM_MESH) { skip = true; return "radio is in mesh mode"; }
    return loraSend(cfg.nick + ": " + step["lora"].as<String>()) ? "" : "LoRa send failed";
  }
  if (step["wait"].is<float>() || step["wait"].is<int>()) {
    uint32_t ms = (uint32_t)(step["wait"].as<float>() * 1000);
    uint32_t t = millis();
    while (millis() - t < ms) {
      if (readKey().back) return "stopped";
      delay(10);
    }
    return "";
  }
  if (step["say"].is<const char *>()) {
    toast(step["say"].as<String>(), col::HA, 900);
    return "";
  }
  if (step["beep"]) {
    beep(2600, 40);
    return "";
  }
  skip = true;
  return "unknown step";
}

static String stepLabel(JsonObject step) {
  if (step["ha"].is<const char *>()) return String(step["ha"].as<const char *>()) + (step["entity"].is<const char *>() ? " " + String(step["entity"].as<const char *>()) : "");
  if (step["scene"].is<const char *>()) return "scene " + String(step["scene"].as<const char *>());
  if (step["webhook"].is<const char *>()) return "webhook " + String(step["webhook"].as<const char *>());
  if (step["ir"].is<const char *>()) { String p = step["ir"].as<String>(); return "IR " + String(step["button"] | "signal") + " (" + p.substring(p.lastIndexOf('/') + 1) + ")"; }
  if (step["rf"].is<const char *>()) return "RF " + String(step["rf"].as<const char *>());
  if (step["lora"].is<const char *>()) return "LoRa \"" + String(step["lora"].as<const char *>()) + "\"";
  if (step["wait"].is<float>() || step["wait"].is<int>()) return "wait " + String(step["wait"].as<float>(), 1) + "s";
  if (step["say"].is<const char *>()) return "say \"" + String(step["say"].as<const char *>()) + "\"";
  if (step["beep"]) return "beep";
  return "(unknown step)";
}

static void runMacro(JsonObject macro) {
  JsonArray steps = macro["steps"].as<JsonArray>();
  std::vector<RunStep> rs;
  for (JsonObject st : steps) rs.push_back({stepLabel(st), 0, ""});
  String name = macro["name"] | "Macro";
  int ok = 0, fail = 0;
  int i = 0;
  for (JsonObject st : steps) {
    rs[i].status = 1;
    drawRun(name, rs, i);
    bool skip;
    String err = runStep(st, skip);
    if (err == "stopped") { rs[i].status = 4; rs[i].note = "stopped"; break; }
    if (!err.length()) { rs[i].status = 2; ok++; }
    else if (skip) { rs[i].status = 4; rs[i].note = err; }
    else { rs[i].status = 3; rs[i].note = err; fail++; }
    drawRun(name, rs, i);
    delay(180);
    i++;
  }
  // final summary
  beep(fail ? 500 : 3000, fail ? 200 : 40);
  String msg = fail ? String("Done with ") + fail + " problem" + (fail > 1 ? "s" : "") : String("All ") + ok + " step" + (ok != 1 ? "s" : "") + " done!";
  toast(msg, fail ? col::WARN : col::OK, 1400);
}

// ------------------------------------------------------------------ app
static const char *MACRO_HELP =
    "Macros fire a whole list of actions with one tap - like a TV remote's \"activity\" button, "
    "but for your whole setup.\n\n"
    "Each macro has a name and steps. A step can turn on Home Assistant lights or scenes, blast an IR "
    "remote signal, send a saved 433 code, send a LoRa message, wait a moment, show a message or beep.\n\n"
    "Edit them in /capdeck/macros.json - easiest from the Portal on your phone (Files tab). The example "
    "file has three ready to copy. No coding, just fill in the blanks.\n\n"
    "Tip: on this screen, number keys 1-9 run that macro straight away.";

void appMacros() {
  macroEnsureTemplate();
  int sel = 0;
  while (true) {
    if (!sdOK) { toast("Macros live on the SD card - none found", col::WARN, 1500); return; }
    File f = SD.open(MACROS_PATH, FILE_READ);
    JsonDocument doc;
    DeserializationError err;
    if (f) { err = deserializeJson(doc, f); f.close(); }
    else err = DeserializationError::EmptyInput;

    std::vector<ListItem> items;
    JsonArray macros = doc["macros"].as<JsonArray>();
    for (JsonObject m : macros) {
      ListItem it;
      it.label = m["name"] | "Macro";
      it.icon = iconFor(m["icon"] | "");
      int n = m["steps"].as<JsonArray>().size();
      it.right = String(n) + (n == 1 ? " step" : " steps");
      items.push_back(it);
    }
    items.push_back({"Help", "how macros work", ICON_INFO, col::DIM});

    int r = listMenu("Macros", col::HA, items, sel, ICON_MACRO);
    if (r < 0) return;
    if (err && r != (int)items.size() - 1) {
      toast("macros.json has a problem: " + String(err.c_str()) + ". Fix it in Portal > Files.", col::BAD, 2600);
      continue;
    }
    if (r == (int)items.size() - 1) { textViewer("Macros", MACRO_HELP, col::HA); continue; }
    runMacro(macros[r]);
  }
}
