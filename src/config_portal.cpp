#include "config_portal.h"
#include "wifi_manager.h"
#include "preferences_store.h"
#include "config.h"

#include <WebServer.h>
#include <Update.h>
#include <ArduinoJson.h>

// ── HTML embebido en flash ────────────────────────────────────────────────
static const char PORTAL_HTML[] PROGMEM = R"rawhtml(<!DOCTYPE html>
<html lang="es"><head>
<meta charset="UTF-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>NEOLINK Config</title>
<style>
*{box-sizing:border-box;margin:0;padding:0}
body{background:#0d1117;color:#e6edf3;font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',sans-serif;padding:16px;max-width:520px;margin:0 auto}
h1{color:#58a6ff;font-size:1.4em;margin-bottom:2px}
.sub{color:#8b949e;font-size:.8em;margin-bottom:20px}
.card{background:#161b22;border:1px solid #30363d;border-radius:10px;padding:16px;margin:12px 0}
.card h2{color:#8b949e;font-size:.75em;text-transform:uppercase;letter-spacing:.08em;margin-bottom:12px}
.row{display:flex;justify-content:space-between;align-items:center;padding:5px 0;border-bottom:1px solid #21262d}
.row:last-child{border:none}
.rl{color:#8b949e;font-size:.85em}
.rv{font-weight:600;font-size:.9em}
.big{font-size:1.5em;font-weight:700;color:#fff}
.badge{display:inline-block;padding:3px 10px;border-radius:20px;font-size:.75em;font-weight:600}
.ok{background:#1c3a2a;color:#3fb950}.err{background:#3a1c1c;color:#f85149}.warn{background:#3a2c0a;color:#d29922}.info{background:#0f2044;color:#58a6ff}
label{font-size:.82em;color:#8b949e;display:block;margin:10px 0 3px}
input[type=text],input[type=password],input[type=number]{width:100%;background:#0d1117;border:1px solid #30363d;border-radius:6px;padding:9px 12px;color:#e6edf3;font-size:.9em}
input:focus{outline:none;border-color:#58a6ff}
.btn{background:#238636;border:none;color:#fff;padding:10px 0;border-radius:6px;cursor:pointer;font-size:.9em;font-weight:600;width:100%;margin-top:8px}
.btn:hover{background:#2ea043}.btn.blue{background:#1158c7}.btn.blue:hover{background:#1a74e8}
.btn.red{background:#b62324}.btn.red:hover{background:#da3633}
.msg{padding:9px 12px;border-radius:6px;font-size:.82em;margin-top:8px;display:none}
.msg.show{display:block}.msg.ok{background:#1c3a2a;color:#3fb950}.msg.err{background:#3a1c1c;color:#f85149}
.prog{background:#21262d;border-radius:4px;height:6px;margin-top:8px;display:none}
.prog.show{display:block}
.bar{background:#58a6ff;height:6px;border-radius:4px;width:0%;transition:width .4s}
.sep{border:none;border-top:1px solid #21262d;margin:8px 0}
.tabs{display:flex;gap:4px;margin-bottom:16px}
.tab{flex:1;padding:8px;background:#161b22;border:1px solid #30363d;border-radius:6px;cursor:pointer;font-size:.82em;color:#8b949e;text-align:center}
.tab.active{background:#0f2044;border-color:#58a6ff;color:#58a6ff}
.section{display:none}.section.active{display:block}
</style></head>
<body>
<h1>&#9881; NEOLINK</h1>
<p class="sub" id="hdr">Conectando...</p>

<div class="tabs">
<div class="tab active" onclick="tab('live')">Lecturas</div>
<div class="tab" onclick="tab('wifi')">WiFi</div>
<div class="tab" onclick="tab('dev')">Dispositivo</div>
<div class="tab" onclick="tab('ota')">OTA</div>
</div>

<!-- LECTURAS -->
<div class="section active" id="s-live">
<div class="card">
<h2>Sensores</h2>
<div class="row"><span class="rl">Temperatura (SHT35)</span><span class="big" id="v-temp">--</span></div>
<div class="row"><span class="rl">Humedad (SHT35)</span><span class="big" id="v-hum">--</span></div>
<div class="row"><span class="rl">PT100</span><span class="big" id="v-pt">--</span></div>
</div>
<div class="card">
<h2>Conectividad</h2>
<div class="row"><span class="rl">WiFi</span><span id="v-wifi"></span></div>
<div class="row"><span class="rl">GSM / GPRS</span><span id="v-gsm"></span></div>
<div class="row"><span class="rl">&#128267; Batería</span><span id="v-bat" class="rv">--</span></div>
<div class="row"><span class="rl">Último envío</span><span id="v-sync" class="rv" style="color:#8b949e">--</span></div>
<div class="row"><span class="rl">Uptime</span><span id="v-up" class="rv" style="color:#8b949e">--</span></div>
</div>
</div>

<!-- WIFI -->
<div class="section" id="s-wifi">
<div class="card">
<h2>Configuración WiFi</h2>
<label>Red (SSID)</label>
<input type="text" id="wifi-ssid" placeholder="Nombre de la red">
<label>Contraseña</label>
<input type="password" id="wifi-pass" placeholder="Contraseña">
<button class="btn blue" onclick="saveWifi()">Conectar</button>
<button class="btn red" onclick="clearWifi()" style="margin-top:4px">Olvidar red guardada</button>
<div class="msg" id="m-wifi"></div>
</div>
</div>

<!-- DISPOSITIVO -->
<div class="section" id="s-dev">
<div class="card">
<h2>Configuración del dispositivo</h2>
<label>Nombre del dispositivo</label>
<input type="text" id="dev-name" placeholder="neolink-v1-001">
<label>Host del servidor backend</label>
<input type="text" id="srv-host" placeholder="3.222.162.34">
<label>Puerto</label>
<input type="number" id="srv-port" value="8080" min="1" max="65535">
<button class="btn" onclick="saveDev()">Guardar</button>
<div class="msg" id="m-dev"></div>
</div>
</div>

<!-- OTA -->
<div class="section" id="s-ota">
<div class="card">
<h2>Actualización de firmware</h2>
<p style="color:#8b949e;font-size:.82em;margin-bottom:12px">Sube un archivo .bin compilado con PlatformIO. El dispositivo se reiniciará automáticamente.</p>
<input type="file" id="fw-file" accept=".bin" style="color:#e6edf3;width:100%;margin-bottom:8px">
<button class="btn" onclick="doOta()">&#8593; Actualizar firmware</button>
<div class="msg" id="m-ota"></div>
<div class="prog" id="ota-prog"><div class="bar" id="ota-bar"></div></div>
</div>
</div>

<script>
var activeTab='live';
function tab(id){
  document.querySelectorAll('.tab').forEach((t,i)=>{t.classList.toggle('active',['live','wifi','dev','ota'][i]===id)});
  document.querySelectorAll('.section').forEach(s=>s.classList.remove('active'));
  document.getElementById('s-'+id).classList.add('active');
  activeTab=id;
}

function badge(cls,txt){return'<span class="badge '+cls+'">'+txt+'</span>';}

async function loadStatus(){
  try{
    var r=await fetch('/api/status');var d=await r.json();
    document.getElementById('hdr').textContent=d.device_id||'NEOLINK';
    document.getElementById('v-temp').textContent=d.sht_ok?(d.temp_sht.toFixed(1)+' \u00b0C'):'N/A';
    document.getElementById('v-hum').textContent=d.sht_ok?(d.humidity.toFixed(1)+'%'):'N/A';
    document.getElementById('v-pt').textContent=d.pt100_ok?(d.temp_pt100.toFixed(1)+' \u00b0C'):'N/A';
    document.getElementById('v-bat').textContent=d.battery+'%';
    var wc=d.wifi_connected;var gc=d.gsm_connected;
    document.getElementById('v-wifi').innerHTML=wc?badge('ok',d.wifi_ssid+' ('+d.wifi_rssi+'dBm)'):badge('err','No conectado');
    document.getElementById('v-gsm').innerHTML=gc?badge('ok','GPRS '+d.gsm_rssi+'dBm'):badge('err','Sin red');
    var ago=d.last_post_s;
    document.getElementById('v-sync').textContent=ago>0?(ago<60?ago+'s':Math.floor(ago/60)+'m'):'Nunca';
    var u=d.uptime_s;document.getElementById('v-up').textContent=
      String(Math.floor(u/3600)).padStart(2,'0')+':'+String(Math.floor(u%3600/60)).padStart(2,'0')+':'+String(u%60).padStart(2,'0');
  }catch(e){}
}

async function loadConfig(){
  try{
    var r=await fetch('/api/config');var d=await r.json();
    if(d.wifi_ssid)document.getElementById('wifi-ssid').value=d.wifi_ssid;
    if(d.device_name)document.getElementById('dev-name').value=d.device_name;
    if(d.server_host)document.getElementById('srv-host').value=d.server_host;
    if(d.server_port)document.getElementById('srv-port').value=d.server_port;
  }catch(e){}
}

function msg(id,ok,txt){
  var el=document.getElementById(id);el.className='msg show '+(ok?'ok':'err');
  el.textContent=txt;setTimeout(()=>el.classList.remove('show'),5000);
}

async function saveWifi(){
  var ssid=document.getElementById('wifi-ssid').value.trim();
  var pass=document.getElementById('wifi-pass').value;
  if(!ssid){msg('m-wifi',false,'Ingresa el nombre de la red');return;}
  try{
    var r=await fetch('/api/config/wifi',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({ssid:ssid,pass:pass})});
    var d=await r.json();msg('m-wifi',d.ok,d.ok?'Guardado. Reconectando en 3s...':'Error al guardar');
  }catch(e){msg('m-wifi',false,'Error de conexión');}
}

async function clearWifi(){
  try{
    var r=await fetch('/api/config/wifi',{method:'DELETE'});
    var d=await r.json();msg('m-wifi',d.ok,'Red olvidada. Reiniciando en AP mode...');
  }catch(e){}
}

async function saveDev(){
  var name=document.getElementById('dev-name').value.trim();
  var host=document.getElementById('srv-host').value.trim();
  var port=parseInt(document.getElementById('srv-port').value)||8080;
  try{
    var r=await fetch('/api/config/device',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({name:name,host:host,port:port})});
    var d=await r.json();msg('m-dev',d.ok,d.ok?'Configuraci\u00f3n guardada':'Error');
  }catch(e){msg('m-dev',false,'Error');}
}

async function doOta(){
  var file=document.getElementById('fw-file').files[0];
  if(!file){msg('m-ota',false,'Selecciona un archivo .bin');return;}
  var bar=document.getElementById('ota-bar');
  var prog=document.getElementById('ota-prog');
  prog.classList.add('show');bar.style.width='0%';
  msg('m-ota',true,'Subiendo firmware...');
  var fd=new FormData();fd.append('firmware',file);
  var xhr=new XMLHttpRequest();
  xhr.upload.onprogress=function(e){if(e.lengthComputable)bar.style.width=(e.loaded/e.total*100).toFixed(0)+'%';};
  xhr.onload=function(){var ok=xhr.status===200;msg('m-ota',ok,ok?'OK! Reiniciando...':'Error en la actualizaci\u00f3n');if(ok)setTimeout(()=>location.reload(),6000);};
  xhr.onerror=function(){msg('m-ota',false,'Error de red');};
  xhr.open('POST','/ota');xhr.send(fd);
}

loadStatus();loadConfig();
setInterval(loadStatus,3000);
</script>
</body></html>)rawhtml";

// ── Servidor ──────────────────────────────────────────────────────────────
static WebServer     s_srv(80);
static PortalSensorData s_data = {};
static portMUX_TYPE  s_mux = portMUX_INITIALIZER_UNLOCKED;

void portal_update(const PortalSensorData &data) {
    portENTER_CRITICAL(&s_mux);
    s_data = data;
    portEXIT_CRITICAL(&s_mux);
}

// ── Handlers ──────────────────────────────────────────────────────────────
static void handle_root() {
    s_srv.sendHeader("Cache-Control", "no-cache");
    s_srv.send_P(200, "text/html", PORTAL_HTML);
}

static void handle_status() {
    PortalSensorData d;
    portENTER_CRITICAL(&s_mux);
    d = s_data;
    portEXIT_CRITICAL(&s_mux);

    WifiInfo winfo = wifi_get_info();

    StaticJsonDocument<512> doc;
    doc["device_id"]    = prefs_device_name();
    doc["temp_sht"]     = serialized(String(d.temp_sht,   2));
    doc["humidity"]     = serialized(String(d.humidity,   2));
    doc["sht_ok"]       = d.sht_ok;
    doc["temp_pt100"]   = serialized(String(d.temp_pt100, 2));
    doc["pt100_ok"]     = d.pt100_ok;
    doc["gsm_rssi"]     = d.gsm_rssi;
    doc["gsm_connected"]= d.gsm_connected;
    doc["wifi_rssi"]    = winfo.rssi;
    doc["wifi_connected"]= (winfo.state == WIFI_CONNECTED);
    doc["wifi_ssid"]    = winfo.ssid;
    doc["battery"]      = d.battery_pct;
    doc["uptime_s"]     = d.uptime_s;
    uint32_t last_s = d.last_post_ms > 0
                        ? (millis() - d.last_post_ms) / 1000
                        : 0;
    doc["last_post_s"]  = last_s;

    String out;
    serializeJson(doc, out);
    s_srv.send(200, "application/json", out);
}

static void handle_get_config() {
    StaticJsonDocument<256> doc;
    doc["wifi_ssid"]    = prefs_wifi_ssid();
    doc["device_name"]  = prefs_device_name();
    doc["server_host"]  = prefs_server_host();
    doc["server_port"]  = prefs_server_port();

    String out;
    serializeJson(doc, out);
    s_srv.send(200, "application/json", out);
}

static void handle_post_wifi() {
    String body = s_srv.arg("plain");
    StaticJsonDocument<128> doc;
    if (deserializeJson(doc, body)) {
        s_srv.send(400, "application/json", "{\"ok\":false,\"err\":\"bad json\"}");
        return;
    }
    const char *ssid = doc["ssid"] | "";
    const char *pass = doc["pass"] | "";
    if (strlen(ssid) == 0) {
        s_srv.send(400, "application/json", "{\"ok\":false,\"err\":\"ssid empty\"}");
        return;
    }
    wifi_save_and_reconnect(ssid, pass);
    s_srv.send(200, "application/json", "{\"ok\":true}");
}

static void handle_delete_wifi() {
    prefs_clear_wifi();
    s_srv.send(200, "application/json", "{\"ok\":true}");
    delay(500);
    ESP.restart();
}

static void handle_post_device() {
    String body = s_srv.arg("plain");
    StaticJsonDocument<256> doc;
    if (deserializeJson(doc, body)) {
        s_srv.send(400, "application/json", "{\"ok\":false}");
        return;
    }
    const char *name = doc["name"] | "";
    const char *host = doc["host"] | "";
    int         port = doc["port"] | BACKEND_PORT;
    if (strlen(name) > 0 && strlen(host) > 0) {
        prefs_save_device(name, host, port);
        s_srv.send(200, "application/json", "{\"ok\":true}");
    } else {
        s_srv.send(400, "application/json", "{\"ok\":false,\"err\":\"missing fields\"}");
    }
}

static void handle_ota_done() {
    s_srv.sendHeader("Connection", "close");
    if (Update.hasError()) {
        s_srv.send(500, "text/plain", "OTA FAILED");
    } else {
        s_srv.send(200, "text/plain", "OTA OK");
        delay(500);
        ESP.restart();
    }
}

static void handle_ota_upload() {
    HTTPUpload &upload = s_srv.upload();
    if (upload.status == UPLOAD_FILE_START) {
        Serial.printf("[OTA] Start: %s (%u bytes)\n",
                      upload.filename.c_str(), upload.totalSize);
        if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
            Serial.println("[OTA] Begin failed");
        }
    } else if (upload.status == UPLOAD_FILE_WRITE) {
        if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
            Serial.println("[OTA] Write error");
        }
    } else if (upload.status == UPLOAD_FILE_END) {
        if (Update.end(true)) {
            Serial.printf("[OTA] Done: %u bytes\n", upload.totalSize);
        } else {
            Serial.println("[OTA] End error");
        }
    }
}

static void handle_not_found() {
    // Captive portal redirect (para cuando está en AP mode)
    s_srv.sendHeader("Location", "/");
    s_srv.send(302, "text/plain", "");
}

// ── API pública ───────────────────────────────────────────────────────────
void portal_begin() {
    s_srv.on("/",                  HTTP_GET,  handle_root);
    s_srv.on("/api/status",        HTTP_GET,  handle_status);
    s_srv.on("/api/config",        HTTP_GET,  handle_get_config);
    s_srv.on("/api/config/wifi",   HTTP_POST, handle_post_wifi);
    s_srv.on("/api/config/wifi",   HTTP_DELETE, handle_delete_wifi);
    s_srv.on("/api/config/device", HTTP_POST, handle_post_device);
    s_srv.on("/ota",               HTTP_POST, handle_ota_done, handle_ota_upload);
    s_srv.onNotFound(handle_not_found);
    s_srv.begin();

    WifiInfo wi = wifi_get_info();
    Serial.printf("[PORTAL] Started on http://%s\n", wi.ip);
}

void portal_loop() {
    s_srv.handleClient();
}
