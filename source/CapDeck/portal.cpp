// CapDeck - Portal: the Cardputer's own Wi-Fi hotspot with a phone-friendly web app.
// Chat with your local AI, edit config.json, manage SD files, see mesh nodes - no SD swapping.
// Reachable at http://192.168.4.1 on the hotspot (no login: the Wi-Fi password is the lock),
// and at the Cardputer's home-network IP (login: capdeck / the same password).
#include "core.h"
#include <ArduinoJson.h>
#include <DNSServer.h>
#include <WebServer.h>
#include <esp_random.h>

static WebServer *server = nullptr;
static DNSServer *dns = nullptr;
static bool active = false;
static File uploadFile;
static bool uploadOk = false;

bool portalActive() { return active; }

String portalSsid() {
  uint8_t mac[6];
  WiFi.macAddress(mac);
  char b[20];
  snprintf(b, sizeof b, "CapDeck-%02X%02X", mac[4], mac[5]);
  return b;
}

static bool allowed() {
  // phones on the hotspot already proved they know the Wi-Fi password
  if (server->client().localIP() == WiFi.softAPIP()) return true;
  if (server->authenticate("capdeck", cfg.portalPass.c_str())) return true;
  server->requestAuthentication(BASIC_AUTH, "CapDeck", "Log in as capdeck with the portal password shown on the Cardputer");
  return false;
}

static const char PAGE[] PROGMEM = R"HTML(<!DOCTYPE html><html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1"><title>CapDeck</title>
<style>
:root{--bg:#0b0f17;--p:#141b27;--p2:#1c2534;--t:#e8edf5;--d:#8a96a8;--a:#29d3ff;--m:#ff3dd8;--ok:#3ee07a;--bad:#ff5a6a}
*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--t);font:15px system-ui,sans-serif}
header{display:flex;align-items:center;gap:10px;padding:12px 14px;background:var(--p);border-bottom:2px solid var(--a)}
header b{font-size:18px;letter-spacing:1px}header span{color:var(--d);font-size:12px;margin-left:auto}
nav{display:flex;overflow-x:auto;background:var(--p)}nav button{flex:1;background:none;border:0;color:var(--d);padding:11px 8px;font:inherit;border-bottom:2px solid transparent}
nav button.on{color:var(--a);border-color:var(--a)}main{padding:12px;max-width:760px;margin:auto}section{display:none}section.on{display:block}
.card{background:var(--p);border-radius:10px;padding:12px;margin-bottom:10px}
input,textarea,select{width:100%;background:var(--p2);color:var(--t);border:1px solid #2c3850;border-radius:8px;padding:10px;font:inherit}
textarea{font:13px ui-monospace,monospace;min-height:55vh}
.btn{background:var(--a);color:#001018;border:0;border-radius:8px;padding:10px 14px;font-weight:600;font:inherit}
.btn.m{background:var(--m);color:#fff}.btn.g{background:var(--p2);color:var(--t)}.row{display:flex;gap:8px;align-items:center}
#log{height:58vh;overflow-y:auto;display:flex;flex-direction:column;gap:8px;padding:4px}
.b{max-width:85%;padding:9px 12px;border-radius:12px;white-space:pre-wrap;line-height:1.35}.u{align-self:flex-end;background:var(--a);color:#001018}.r{align-self:flex-start;background:var(--p2)}
.f{display:flex;align-items:center;gap:8px;padding:9px 4px;border-bottom:1px solid #222c3c}.f a{color:var(--t);text-decoration:none;flex:1;word-break:break-all}
.f small{color:var(--d)}.x{background:none;border:0;color:var(--bad);font-size:18px}.k{color:var(--d);font-size:12px}
table{width:100%;border-collapse:collapse;font-size:14px}td,th{padding:7px 4px;border-bottom:1px solid #222c3c;text-align:left}th{color:var(--d);font-weight:500}
.ok{color:var(--ok)}.bad{color:var(--bad)}
</style></head><body>
<header><b>CAP<span style="color:var(--m);margin:0;font-size:18px">DECK</span></b><span id="st">...</span></header>
<nav><button data-t="chat" class="on">AI Chat</button><button data-t="files">Files</button><button data-t="cfg">Config</button><button data-t="mesh">Mesh</button><button data-t="dev">Device</button></nav>
<main>
<section id="chat" class="on"><div class="card"><div id="log"></div></div>
<div class="row"><input id="msg" placeholder="Message your local AI" autocomplete="off"><button class="btn" id="send">Send</button></div>
<p class="k">Goes through the Cardputer to the server set in config.json &rarr; local_ai.url. <a href="#" id="clr" style="color:var(--d)">new chat</a></p></section>
<section id="files"><div class="card"><div class="row"><b id="cwd" style="flex:1">/</b><button class="btn g" id="up">Up</button><button class="btn g" id="mk">New folder</button></div>
<div id="fl"></div></div><div class="card"><input type="file" id="fi" multiple><p class="k">Uploads go into the folder shown above. Firmware .bin files on the SD root show up in M5Launcher.</p>
<button class="btn" id="ub">Upload</button> <span id="us" class="k"></span></div></section>
<section id="cfg"><div class="card"><textarea id="ta" spellcheck="false"></textarea><p class="k" id="cs"></p>
<button class="btn" id="cv">Save &amp; reload</button></div></section>
<section id="mesh"><div class="card"><table><thead><tr><th>Node</th><th>Name</th><th>Seen</th><th>Where</th></tr></thead><tbody id="nt"></tbody></table>
<p class="k">Passive Meshtastic listening. Map links open once your phone is back on the internet.</p></div></section>
<section id="dev"><div class="card"><table id="dt"></table></div><button class="btn m" id="rb">Reboot Cardputer</button></section>
</main>
<script>
const $=s=>document.querySelector(s),E=s=>encodeURIComponent(s);let hist=[],cwd='/';
document.querySelectorAll('nav button').forEach(b=>b.onclick=()=>{document.querySelectorAll('nav button,section').forEach(e=>e.classList.remove('on'));b.classList.add('on');$('#'+b.dataset.t).classList.add('on');({files:ls,cfg:lc,mesh:lm,dev:ld})[b.dataset.t]?.()});
function add(t,u){const d=document.createElement('div');d.className='b '+(u?'u':'r');d.textContent=t;$('#log').appendChild(d);$('#log').scrollTop=1e9;return d}
async function send(){const m=$('#msg').value.trim();if(!m)return;$('#msg').value='';add(m,1);hist.push({role:'user',content:m});const w=add('thinking...',0);
try{const r=await(await fetch('/api/ai',{method:'POST',body:JSON.stringify({messages:hist})})).json();if(r.reply){w.textContent=r.reply;hist.push({role:'assistant',content:r.reply})}else{w.textContent='Error: '+r.error;w.classList.add('bad');hist.pop()}}catch(e){w.textContent='Error: '+e;hist.pop()}}
$('#send').onclick=send;$('#msg').onkeydown=e=>{if(e.key=='Enter')send()};$('#clr').onclick=e=>{e.preventDefault();hist=[];$('#log').innerHTML=''};
function sz(b){return b<1024?b+' B':b<1048576?(b/1024).toFixed(1)+' K':(b/1048576).toFixed(1)+' M'}
async function ls(){$('#cwd').textContent=cwd;const r=await(await fetch('/api/ls?path='+E(cwd))).json();$('#fl').innerHTML='';
r.forEach(f=>{const p=(cwd=='/'?'':cwd)+'/'+f.n,d=document.createElement('div');d.className='f';
d.innerHTML=(f.d?'&#128193; ':'&#128196; ')+'<a href="'+(f.d?'#':'/api/dl?path='+E(p))+'">'+f.n+'</a><small>'+(f.d?'':sz(f.s))+'</small><button class="x">&times;</button>';
if(f.d)d.querySelector('a').onclick=e=>{e.preventDefault();cwd=p;ls()};d.querySelector('.x').onclick=async()=>{if(confirm('Delete '+p+(f.d?' and everything in it':'')+'?')){await fetch('/api/rm?path='+E(p),{method:'POST'});ls()}};$('#fl').appendChild(d)})}
$('#up').onclick=()=>{if(cwd!='/'){cwd=cwd.substring(0,cwd.lastIndexOf('/'))||'/';ls()}};
$('#mk').onclick=async()=>{const n=prompt('Folder name');if(n){await fetch('/api/mkdir?path='+E((cwd=='/'?'':cwd)+'/'+n),{method:'POST'});ls()}};
$('#ub').onclick=async()=>{const fs=$('#fi').files;for(let i=0;i<fs.length;i++){$('#us').textContent='Uploading '+fs[i].name+'...';const fd=new FormData();fd.append('f',fs[i],fs[i].name);
await fetch('/api/upload?dir='+E(cwd),{method:'POST',body:fd})}$('#us').textContent=fs.length?'Done':'';$('#fi').value='';ls()};
async function lc(){$('#ta').value=await(await fetch('/api/config')).text();$('#cs').textContent=''}
$('#cv').onclick=async()=>{try{JSON.parse($('#ta').value)}catch(e){$('#cs').innerHTML='<span class="bad">JSON error: '+e.message+'</span>';return}
const r=await fetch('/api/config',{method:'POST',body:$('#ta').value});$('#cs').innerHTML=r.ok?'<span class="ok">Saved and reloaded on the Cardputer.</span>':'<span class="bad">'+await r.text()+'</span>'};
async function lm(){const r=await(await fetch('/api/nodes')).json();$('#nt').innerHTML=r.length?'':'<tr><td colspan=4 class="k">No nodes heard yet. Turn on listening in MESH.</td></tr>';
r.forEach(n=>{$('#nt').innerHTML+='<tr><td>'+(n.s||n.id)+'</td><td>'+(n.l||'')+'</td><td>'+n.age+'</td><td>'+(n.lat?'<a style="color:var(--a)" href="https://www.openstreetmap.org/?mlat='+n.lat+'&mlon='+n.lon+'#map=15/'+n.lat+'/'+n.lon+'">'+n.dist+'</a>':'-')+'</td></tr>'})}
async function ld(){const r=await(await fetch('/api/status')).json();$('#dt').innerHTML='';for(const k in r)$('#dt').innerHTML+='<tr><th>'+k+'</th><td>'+r[k]+'</td></tr>'}
$('#rb').onclick=()=>{if(confirm('Reboot?'))fetch('/api/reboot',{method:'POST'})};
fetch('/api/status').then(r=>r.json()).then(r=>$('#st').textContent=r.battery+'  '+r.wifi);
</script></body></html>)HTML";

static void sendJson(JsonDocument &d, int code = 200) {
  String s;
  serializeJson(d, s);
  server->send(code, "application/json", s);
}

static String argPath(const char *name) {
  String p = server->arg(name);
  if (!p.startsWith("/")) p = "/" + p;
  p.replace("..", "");
  while (p.length() > 1 && p.endsWith("/")) p.remove(p.length() - 1);
  return p;
}

static bool rmTree(const String &path) {
  File f = SD.open(path);
  if (!f) return false;
  if (!f.isDirectory()) { f.close(); return SD.remove(path); }
  std::vector<String> kids;
  for (File c = f.openNextFile(); c; c = f.openNextFile()) {
    String n = c.name();
    int s = n.lastIndexOf('/');
    kids.push_back((path == "/" ? "" : path) + "/" + (s >= 0 ? n.substring(s + 1) : n));
    c.close();
  }
  f.close();
  for (auto &k : kids) rmTree(k);
  return SD.rmdir(path);
}

static void routes() {
  auto page = []() {
    if (!allowed()) return;
    server->send_P(200, "text/html", PAGE);
  };
  server->on("/", page);
  // captive-portal probes from Android / iOS / Windows -> open the app
  const char *probes[] = {"/generate_204", "/gen_204", "/hotspot-detect.html", "/connecttest.txt", "/ncsi.txt", "/success.txt"};
  for (auto p : probes)
    server->on(p, []() {
      server->sendHeader("Location", "http://" + WiFi.softAPIP().toString() + "/");
      server->send(302, "text/plain", "");
    });
  server->onNotFound([]() {
    if (server->client().localIP() == WiFi.softAPIP()) {
      server->sendHeader("Location", "http://" + WiFi.softAPIP().toString() + "/");
      server->send(302, "text/plain", "");
    } else server->send(404, "text/plain", "not found");
  });

  server->on("/api/status", []() {
    if (!allowed()) return;
    JsonDocument d;
    d["battery"] = String(M5Cardputer.Power.getBatteryLevel()) + "%";
    d["wifi"] = netState() == NetState::UP ? WiFi.SSID() + " " + WiFi.localIP().toString() : String("not on home Wi-Fi");
    d["hotspot"] = portalSsid();
    d["sd card"] = sdOK ? humanSize(SD.usedBytes()) + " / " + humanSize(SD.totalBytes()) : String("none");
    d["lora"] = !loraPresent ? "no cap" : radioMode == RM_MESH ? "Meshtastic listening" : "CapDeck chat";
    d["gps"] = gpsFix() ? String(gpsSats()) + " sats, fix" : gpsSeen ? "searching" : "no data";
    d["mesh nodes"] = meshNodes.size();
    d["ai server"] = cfg.aiUrl.length() ? cfg.aiUrl : String("not set");
    d["home assistant"] = cfg.haUrl.length() ? cfg.haUrl : String("not set");
    d["free memory"] = humanSize(ESP.getFreeHeap());
    d["firmware"] = "CapDeck v" CAPDECK_VERSION;
    sendJson(d);
  });

  server->on("/api/ai", HTTP_POST, []() {
    if (!allowed()) return;
    JsonDocument in, out;
    if (deserializeJson(in, server->arg("plain"))) { out["error"] = "bad request"; sendJson(out, 400); return; }
    std::vector<std::pair<String, String>> h;
    for (JsonObject m : in["messages"].as<JsonArray>()) h.push_back({String(m["role"] | "user"), String(m["content"] | "")});
    if (netState() != NetState::UP) {
      out["error"] = "The Cardputer isn't on your home Wi-Fi, so it can't reach the AI server. Check Config > Wi-Fi.";
      sendJson(out);
      return;
    }
    String err, reply = aiChat(h, err);
    if (reply.length()) out["reply"] = reply;
    else out["error"] = err;
    sendJson(out);
  });

  server->on("/api/ls", []() {
    if (!allowed()) return;
    JsonDocument d;
    JsonArray a = d.to<JsonArray>();
    File dir = SD.open(argPath("path"));
    if (dir && dir.isDirectory())
      for (File f = dir.openNextFile(); f; f = dir.openNextFile()) {
        String n = f.name();
        int s = n.lastIndexOf('/');
        JsonObject o = a.add<JsonObject>();
        o["n"] = s >= 0 ? n.substring(s + 1) : n;
        o["d"] = f.isDirectory();
        o["s"] = f.isDirectory() ? 0 : (uint32_t)f.size();
        f.close();
      }
    sendJson(d);
  });

  server->on("/api/dl", []() {
    if (!allowed()) return;
    String p = argPath("path");
    File f = SD.open(p, FILE_READ);
    if (!f || f.isDirectory()) { server->send(404, "text/plain", "no such file"); return; }
    server->sendHeader("Content-Disposition", "attachment; filename=\"" + p.substring(p.lastIndexOf('/') + 1) + "\"");
    server->streamFile(f, "application/octet-stream");
    f.close();
  });

  server->on("/api/rm", HTTP_POST, []() {
    if (!allowed()) return;
    String p = argPath("path");
    server->send(p != "/" && rmTree(p) ? 200 : 500, "text/plain", "");
  });
  server->on("/api/mkdir", HTTP_POST, []() {
    if (!allowed()) return;
    server->send(SD.mkdir(argPath("path")) ? 200 : 500, "text/plain", "");
  });

  server->on(
      "/api/upload", HTTP_POST,
      []() {
        if (!allowed()) return;
        server->send(uploadOk ? 200 : 500, "text/plain", uploadOk ? "ok" : "upload failed");
      },
      []() {
        HTTPUpload &u = server->upload();
        if (u.status == UPLOAD_FILE_START) {
          bool ok = server->client().localIP() == WiFi.softAPIP() || server->authenticate("capdeck", cfg.portalPass.c_str());
          String dir = argPath("dir");
          String name = u.filename;
          name.replace("/", "_");
          uploadOk = false;
          if (ok && sdOK) {
            uploadFile = SD.open((dir == "/" ? "" : dir) + "/" + name, FILE_WRITE);
            uploadOk = (bool)uploadFile;
          }
        } else if (u.status == UPLOAD_FILE_WRITE) {
          if (uploadFile && uploadFile.write(u.buf, u.currentSize) != u.currentSize) uploadOk = false;
        } else if (u.status == UPLOAD_FILE_END || u.status == UPLOAD_FILE_ABORTED) {
          if (uploadFile) uploadFile.close();
          if (u.status == UPLOAD_FILE_ABORTED) uploadOk = false;
        }
      });

  server->on("/api/config", HTTP_GET, []() {
    if (!allowed()) return;
    File f = SD.open("/capdeck/config.json", FILE_READ);
    if (!f) { server->send(404, "text/plain", "{}"); return; }
    server->streamFile(f, "text/plain");
    f.close();
  });
  server->on("/api/config", HTTP_POST, []() {
    if (!allowed()) return;
    String body = server->arg("plain");
    JsonDocument test;
    if (deserializeJson(test, body)) { server->send(400, "text/plain", "That isn't valid JSON - nothing saved."); return; }
    File f = SD.open("/capdeck/config.json", FILE_WRITE);
    if (!f) { server->send(500, "text/plain", "Couldn't write to the SD card"); return; }
    f.print(body);
    f.close();
    loadConfigFile();
    meshBuildChannels();
    loraApplyParams();
    server->send(200, "text/plain", "ok");
  });

  server->on("/api/nodes", []() {
    if (!allowed()) return;
    JsonDocument d;
    JsonArray a = d.to<JsonArray>();
    double la, lo;
    bool me = myPosition(la, lo);
    for (auto &n : meshNodes) {
      JsonObject o = a.add<JsonObject>();
      char id[12];
      snprintf(id, sizeof id, "!%08lx", (unsigned long)n.num);
      o["id"] = id;
      o["s"] = n.shortName;
      o["l"] = n.longName;
      o["age"] = n.lastMs ? fmtAge(millis() - n.lastMs) : String("earlier");
      if (n.hasPos) {
        o["lat"] = n.lat;
        o["lon"] = n.lon;
        o["dist"] = me ? fmtDistance(distanceM(la, lo, n.lat, n.lon)) : String("map");
      }
    }
    sendJson(d);
  });

  server->on("/api/reboot", HTTP_POST, []() {
    if (!allowed()) return;
    server->send(200, "text/plain", "rebooting");
    delay(300);
    ESP.restart();
  });
}

void portalStart() {
  if (active) return;
  if (cfg.portalPass.length() < 8) {  // Wi-Fi needs 8+ characters; easy to remember, on brand
    cfg.portalPass = "deathcaps";
    saveSettings();
  }
  bool sta = netState() == NetState::UP || netState() == NetState::CONNECTING;
  WiFi.mode(sta ? WIFI_AP_STA : WIFI_AP);
  WiFi.softAP(portalSsid().c_str(), cfg.portalPass.c_str());
  delay(100);
  dns = new DNSServer();
  dns->setErrorReplyCode(DNSReplyCode::NoError);
  dns->start(53, "*", WiFi.softAPIP());
  server = new WebServer(80);
  routes();
  server->begin();
  active = true;
}

void portalStop() {
  if (!active) return;
  active = false;
  server->stop();
  dns->stop();
  delete server;
  delete dns;
  server = nullptr;
  dns = nullptr;
  WiFi.softAPdisconnect(true);
  bool sta = netState() == NetState::UP || netState() == NetState::CONNECTING;
  WiFi.mode(sta ? WIFI_STA : WIFI_OFF);
}

void portalPoll() {
  if (!active) return;
  dns->processNextRequest();
  server->handleClient();
}

// ------------------------------------------------------------------ on-device screen
void appPortal() {
  bool showUrl = false;
  uint32_t lastDraw = 0;
  while (true) {
    Key k = readKey();
    if (k.back) return;
    if (k.enter) {
      if (active) {
        if (confirmBox("Stop the portal hotspot?", col::PORTAL)) portalStop();
      } else {
        drawHeader("Starting portal...", col::PORTAL, ICON_PORTAL);
        pushFrame();
        portalStart();
      }
      lastDraw = 0;
    }
    if (k.ch == 'u' || k.tab) { showUrl = !showUrl; lastDraw = 0; }
    if (k.ch == 'p') {  // set your own hotspot password
      String np = cfg.portalPass;
      if (textInput("Hotspot password (8+)", np, col::PORTAL, false, 32)) {
        if (np.length() < 8) toast("Needs at least 8 characters (Wi-Fi rule)", col::WARN, 1600);
        else {
          cfg.portalPass = np;
          saveSettings();
          if (active) { portalStop(); portalStart(); }
          toast("Password saved. Phones need to re-join.", col::OK, 1400);
        }
      }
      lastDraw = 0;
    }
    if (millis() - lastDraw < 1000) { delay(10); continue; }
    lastDraw = millis();
    drawHeader("Portal", col::PORTAL, ICON_PORTAL);
    if (!active) {
      drawIcon(canvas, ICON_PORTAL, 46, 82, 60, col::PORTAL);
      canvas.setFont(&fonts::Font2);
      canvas.setTextDatum(top_left);
      canvas.setTextColor(col::TEXT);
      canvas.drawString("Phone control panel", 92, 40);
      canvas.setFont(&fonts::Font0);
      canvas.setTextColor(col::DIM);
      const char *lines[] = {"- chat with your local AI", "- edit config.json", "- move files to/from SD",
                             "- see Meshtastic nodes", "ENTER starts the hotspot"};
      for (int i = 0; i < 5; i++) {
        if (i == 4) canvas.setTextColor(col::PORTAL);
        canvas.drawString(lines[i], 92, 62 + i * 13);
      }
      pushFrame();
      continue;
    }
    String ssid = portalSsid();
    String qr = showUrl ? "http://" + WiFi.softAPIP().toString() + "/" : "WIFI:T:WPA;S:" + ssid + ";P:" + cfg.portalPass + ";;";
    canvas.fillRect(4, 36, 96, 96, 0xFFFF);
    canvas.qrcode(qr.c_str(), 6, 38, 92, showUrl ? 2 : 3);
    canvas.setFont(&fonts::Font0);
    canvas.setTextDatum(top_left);
    int x = 106, y = 38;
    auto kv = [&](const char *k, const String &v, uint16_t c) {
      canvas.setTextColor(col::DIM);
      canvas.drawString(k, x, y);
      canvas.setFont(&fonts::Font2);
      canvas.setTextColor(c);
      canvas.drawString(v, x, y + 9);
      canvas.setFont(&fonts::Font0);
      y += 26;
    };
    kv("WI-FI", ssid, col::TEXT);
    kv("PASSWORD", cfg.portalPass, col::PORTAL);
    kv("OPEN", WiFi.softAPIP().toString(), col::TEXT);
    canvas.setTextColor(col::DIM);
    String lan = netState() == NetState::UP ? "home: " + WiFi.localIP().toString() : String("not on home Wi-Fi");
    canvas.drawString(lan, x, y);
    canvas.setTextDatum(bottom_right);
    canvas.setTextColor(col::DIM);
    canvas.drawString(showUrl ? "U wifi QR  P password" : "U link QR  P password", SCR_W - 4, SCR_H - 1);
    canvas.setTextDatum(middle_right);
    canvas.setTextColor(col::OK);
    canvas.drawString(String(WiFi.softAPgetStationNum()) + " connected", SCR_W - 6, BAR_H + 9);
    pushFrame();
  }
}
