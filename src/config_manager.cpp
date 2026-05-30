/*
 * config_manager.cpp – HA Dashboard
 *
 * První boot:  ESP32 spustí AP "HA-Dashboard" (heslo: ha-dashboard).
 *              Uživatel se připojí, nakonfiguruje WiFi + MQTT v captive portálu.
 * Další boty:  Automaticky se připojí k uložené WiFi.
 * Vždy:        Webový portál na http://<IP>/ umožňuje konfiguraci MQTT + entit.
 */

#include "config_manager.h"
#include <Preferences.h>
#include <WiFiManager.h>   /* tzapu/WiFiManager */
#include <WebServer.h>
#include <ArduinoJson.h>
#include <WiFi.h>

/* ── Globals ──────────────────────────────────────────────────────────── */
AppConfig  g_cfg;
static Preferences prefs;
static WebServer   web_server(80);
static bool        s_portal_active = false;

/* ── Defaults ─────────────────────────────────────────────────────────── */
static void set_defaults() {
    memset(&g_cfg, 0, sizeof(g_cfg));

    strlcpy(g_cfg.mqtt_server,     "192.168.1.1",   sizeof(g_cfg.mqtt_server));
    g_cfg.mqtt_port = 1883;
    strlcpy(g_cfg.mqtt_client_id,  "ha-dashboard",  sizeof(g_cfg.mqtt_client_id));
    strlcpy(g_cfg.mqtt_base_topic, "homeassistant", sizeof(g_cfg.mqtt_base_topic));

    strlcpy(g_cfg.entity_venku_temp,  "sensor.venku_temperature",  sizeof(g_cfg.entity_venku_temp));
    strlcpy(g_cfg.entity_obyvak_temp, "sensor.obyvak_temperature", sizeof(g_cfg.entity_obyvak_temp));
    strlcpy(g_cfg.entity_weather,     "weather.forecast_home",     sizeof(g_cfg.entity_weather));
    strlcpy(g_cfg.entity_kli_pracovna,"sensor.pracovna_temperature",sizeof(g_cfg.entity_kli_pracovna));
    strlcpy(g_cfg.entity_thermostat,  "climate.termostat",         sizeof(g_cfg.entity_thermostat));

    const char* ldef[CFG_MAX_LIGHTS][2] = {
        {"light.svetlo_1",   "Světlo 1"},
        {"light.svetlo_2",   "Světlo 2"},
        {"light.svetlo_3",   "Světlo 3"},
        {"light.svetlo_4",   "Světlo 4"},
        {"switch.zasuvka_1", "Zásuvka 1"},
        {"switch.zasuvka_2", "Zásuvka 2"},
    };
    for (int i = 0; i < CFG_MAX_LIGHTS; i++) {
        strlcpy(g_cfg.light_entity[i], ldef[i][0], sizeof(g_cfg.light_entity[i]));
        strlcpy(g_cfg.light_name[i],   ldef[i][1], sizeof(g_cfg.light_name[i]));
    }
    strlcpy(g_cfg.ha_url,      "http://192.168.1.1:8123", sizeof(g_cfg.ha_url));
    g_cfg.ha_token[0] = '\0';
    strlcpy(g_cfg.weather_lat, "50.0755", sizeof(g_cfg.weather_lat));
    strlcpy(g_cfg.weather_lon, "14.4378", sizeof(g_cfg.weather_lon));
    g_cfg.configured = false;
    g_cfg.screensaver_timeout = 5;  /* default: 5 minut */
}

/* ── NVS ──────────────────────────────────────────────────────────────── */
void config_manager_save() {
    prefs.begin("ha-dash", false);
    prefs.putBytes("cfg", &g_cfg, sizeof(g_cfg));
    prefs.end();
    Serial.println("[CFG] Saved to NVS");
}

void config_manager_load() {
    prefs.begin("ha-dash", true);
    size_t sz = prefs.getBytesLength("cfg");
    if (sz == sizeof(g_cfg)) {
        prefs.getBytes("cfg", &g_cfg, sizeof(g_cfg));
        Serial.println("[CFG] Loaded from NVS");
    } else {
        Serial.println("[CFG] No valid config – using defaults");
        set_defaults();
    }
    prefs.end();
}

/* ── Topic helpers ────────────────────────────────────────────────────── */
static String entity_to_path(const char* entity_id) {
    // "sensor.venku_temp" → "sensor/venku_temp"
    String s(entity_id);
    int dot = s.indexOf('.');
    if (dot < 0) return s;
    return s.substring(0, dot) + "/" + s.substring(dot + 1);
}

String cfg_topic(const char* entity_id) {
    if (!entity_id || !entity_id[0]) return "";
    return String(g_cfg.mqtt_base_topic) + "/" + entity_to_path(entity_id) + "/state";
}

String cfg_topic_attr(const char* entity_id, const char* attr) {
    if (!entity_id || !entity_id[0]) return "";
    // HA MQTT state stream publikuje atributy primo pod entitou (NE /attributes/)
    // ha/weather/forecast_home/temperature  ← spravne
    // ha/weather/forecast_home/attributes/temperature  ← SPATNE
    return String(g_cfg.mqtt_base_topic) + "/" + entity_to_path(entity_id) + "/" + attr;
}

String cfg_topic_set(const char* entity_id) {
    if (!entity_id || !entity_id[0]) return "";
    return String(g_cfg.mqtt_base_topic) + "/" + entity_to_path(entity_id) + "/set";
}

bool config_portal_active()   { return s_portal_active; }
void config_portal_handle()   { if (s_portal_active) web_server.handleClient(); }
void config_manager_handle()  { if (s_portal_active) web_server.handleClient(); }

/* ── C-kompatibilní gettery (pro screens.c) ──────────────────────────── */
extern "C" {
    const char* cfg_get_light_entity(int idx) {
        if (idx < 0 || idx >= CFG_MAX_LIGHTS) return "";
        return g_cfg.light_entity[idx];
    }
    const char* cfg_get_light_name(int idx) {
        if (idx < 0 || idx >= CFG_MAX_LIGHTS) return "";
        return g_cfg.light_name[idx];
    }
    const char* cfg_get_thermostat_entity(void) {
        return g_cfg.entity_thermostat;
    }

    const char* cfg_get_light_topic_set(int idx) {
        if (idx < 0 || idx >= CFG_MAX_LIGHTS || !g_cfg.light_entity[idx][0]) return "";
        static char buf[128];
        String s = cfg_topic_set(g_cfg.light_entity[idx]);
        strlcpy(buf, s.c_str(), sizeof(buf));
        return buf;
    }
    const char* cfg_get_thermostat_topic_temp(void) {
        static char buf[128];
        if (!g_cfg.entity_thermostat[0]) return "";
        String s = String(g_cfg.mqtt_base_topic) + "/" +
                   String(g_cfg.entity_thermostat).substring(0, String(g_cfg.entity_thermostat).indexOf('.')) + "/" +
                   String(g_cfg.entity_thermostat).substring(String(g_cfg.entity_thermostat).indexOf('.')+1) +
                   "/set";
        strlcpy(buf, s.c_str(), sizeof(buf));
        return buf;
    }

    const char* cfg_get_energy_entity(int idx) {
        if (idx < 0 || idx >= CFG_MAX_ENERGY) return "";
        return g_cfg.energy_entity[idx];
    }
    const char* cfg_get_energy_name(int idx) {
        if (idx < 0 || idx >= CFG_MAX_ENERGY) return "";
        return g_cfg.energy_name[idx];
    }
    const char* cfg_get_energy_unit(int idx) {
        if (idx < 0 || idx >= CFG_MAX_ENERGY) return "";
        return g_cfg.energy_unit[idx];
    }

    uint8_t cfg_get_screensaver_timeout(void) {
        return g_cfg.screensaver_timeout;
    }

    const char* sys_get_wifi_ssid(void) {
        static char buf[33];
        if (WiFi.status() == WL_CONNECTED)
            strlcpy(buf, WiFi.SSID().c_str(), sizeof(buf));
        else
            strlcpy(buf, "â", sizeof(buf));  /* — */
        return buf;
    }

    const char* sys_get_wifi_ip(void) {
        static char buf[16];
        if (WiFi.status() == WL_CONNECTED)
            strlcpy(buf, WiFi.localIP().toString().c_str(), sizeof(buf));
        else
            strlcpy(buf, "â", sizeof(buf));  /* — */
        return buf;
    }

    void sys_get_heap(uint32_t *free_kb, uint32_t *total_kb) {
        *free_kb  = ESP.getFreeHeap()  / 1024;
        *total_kb = ESP.getHeapSize()  / 1024;
    }

    void sys_get_uptime(uint32_t *days, uint32_t *hours, uint32_t *mins) {
        uint32_t s = millis() / 1000;
        *days  = s / 86400; s %= 86400;
        *hours = s / 3600;  s %= 3600;
        *mins  = s / 60;
    }

}

/* ── HTML portál ──────────────────────────────────────────────────────── */
static const char CONFIG_HTML[] PROGMEM = R"HTML(<!DOCTYPE html>
<html lang="cs">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>HA Dashboard – Konfigurace</title>
<style>
body{font-family:sans-serif;background:#050d1a;color:#c8d8f0;margin:0;padding:16px}
h1{color:#4fc3f7;margin-bottom:4px}
h2{color:#81d4fa;border-bottom:1px solid #1a3a5c;padding-bottom:4px;margin-top:24px}
.row{display:flex;gap:8px;margin-bottom:8px;flex-wrap:wrap}
label{display:block;font-size:12px;color:#90a4ae;margin-bottom:2px}
input{background:#0d1f35;border:1px solid #1a4a7a;border-radius:4px;color:#c8d8f0;
      padding:6px 10px;font-size:13px;width:100%;box-sizing:border-box}
input:focus{outline:none;border-color:#4fc3f7}
.f{flex:1;min-width:180px} .fs{flex:0 0 80px}
.btn{background:#1565c0;color:#fff;border:none;border-radius:6px;
     padding:10px 28px;font-size:15px;cursor:pointer;margin-top:16px}
.btn:hover{background:#1976d2}
.btn-r{background:#5c1a1a;margin-left:12px}
.btn-r:hover{background:#7c2020}
.ok{color:#81c784;font-weight:bold;display:none;margin-left:16px}
.grid{display:grid;grid-template-columns:1fr 1fr;gap:8px}
@media(max-width:600px){.grid{grid-template-columns:1fr}}
table{width:100%;border-collapse:collapse}
th{text-align:left;font-size:12px;color:#90a4ae;padding:4px 8px;border-bottom:1px solid #1a3a5c}
td{padding:4px 8px} td input{margin:0}
.note{color:#546e7a;font-size:12px}
</style>
</head>
<body>
<h1>HA Dashboard</h1>
<p class="note">Pro změnu WiFi sítě restartujte zařízení a připojte se k AP <b>HA-Dashboard</b> (heslo: ha-dashboard).</p>
<form id="frm">
<h2>MQTT</h2>
<div class="row">
  <div class="f"><label>Server (IP nebo hostname)</label><input id="mqtt_server"></div>
  <div class="fs"><label>Port</label><input id="mqtt_port" type="number" min="1" max="65535"></div>
</div>
<div class="row">
  <div class="f"><label>Uživatel</label><input id="mqtt_user"></div>
  <div class="f"><label>Heslo</label><input id="mqtt_pass" type="password"></div>
</div>
<div class="row">
  <div class="f"><label>Client ID</label><input id="mqtt_client_id"></div>
  <div class="f"><label>Base topic</label><input id="mqtt_base_topic"></div>
</div>
<h2>HA REST API <small class="note">(pro ovládání světel a termostatu)</small></h2>
<div class="row">
  <div class="f"><label>HA URL (např. http://192.168.1.1:8123)</label><input id="ha_url"></div>
</div>
<div class="row">
  <div class="f"><label>Long-Lived Access Token <small class="note">(Profil → Zabezpečení → Tokeny)</small></label><input id="ha_token" type="password"></div>
</div>
<h2>Počasí – Open-Meteo <small class="note">(předpověď z webu, zdarma)</small></h2>
<div class="row">
  <div class="fs"><label>Zeměpisná šířka</label><input id="weather_lat"></div>
  <div class="fs"><label>Zeměpisná délka</label><input id="weather_lon"></div>
</div>
<h2>HOME – senzory</h2>
<div class="grid">
  <div><label>Venku teplota (entity ID)</label><input id="entity_venku_temp"></div>
  <div><label>Obývák teplota (entity ID)</label><input id="entity_obyvak_temp"></div>
  <div><label>Počasí (entity ID)</label><input id="entity_weather"></div>
</div>
<h2>KLIMATE</h2>
<div class="grid">
  <div><label>Pracovna teplota</label><input id="entity_kli_pracovna"></div>
  <div><label>Bojler teplota</label><input id="entity_kli_bojler"></div>
  <div><label>Kotel teplota</label><input id="entity_kli_kotel"></div>
  <div><label>Termostat (climate.*)</label><input id="entity_thermostat"></div>
</div>
<h2>SVĚTLA</h2>
<table>
<tr><th>#</th><th>Entity ID (light.* / switch.*)</th><th>Název na displeji</th></tr>
<tr><td>1</td><td><input id="l0e"></td><td><input id="l0n"></td></tr>
<tr><td>2</td><td><input id="l1e"></td><td><input id="l1n"></td></tr>
<tr><td>3</td><td><input id="l2e"></td><td><input id="l2n"></td></tr>
<tr><td>4</td><td><input id="l3e"></td><td><input id="l3n"></td></tr>
<tr><td>5</td><td><input id="l4e"></td><td><input id="l4n"></td></tr>
<tr><td>6</td><td><input id="l5e"></td><td><input id="l5n"></td></tr>
</table>
<h2>Screensaver</h2>
<div class="row">
  <div class="f"><label>Aktivovat po nečinnosti</label>
  <select id="screensaver_timeout" style="background:#0d1f35;border:1px solid #1a4a7a;border-radius:4px;color:#c8d8f0;padding:6px 10px;font-size:13px;width:100%">
    <option value="0">Vypnuto</option>
    <option value="1">1 minuta</option>
    <option value="5">5 minut</option>
    <option value="10">10 minut</option>
    <option value="30">30 minut</option>
  </select></div>
</div>
<h2>ENERGY <small class="note">(připraveno – entity se aktivují po doplnění)</small></h2>
<table>
<tr><th>#</th><th>Entity ID</th><th>Název</th><th>Jednotka</th></tr>
<tr><td>1</td><td><input id="e0e"></td><td><input id="e0n"></td><td><input id="e0u" style="width:60px"></td></tr>
<tr><td>2</td><td><input id="e1e"></td><td><input id="e1n"></td><td><input id="e1u" style="width:60px"></td></tr>
<tr><td>3</td><td><input id="e2e"></td><td><input id="e2n"></td><td><input id="e2u" style="width:60px"></td></tr>
<tr><td>4</td><td><input id="e3e"></td><td><input id="e3n"></td><td><input id="e3u" style="width:60px"></td></tr>
<tr><td>5</td><td><input id="e4e"></td><td><input id="e4n"></td><td><input id="e4u" style="width:60px"></td></tr>
<tr><td>6</td><td><input id="e5e"></td><td><input id="e5n"></td><td><input id="e5u" style="width:60px"></td></tr>
</table>
<div style="margin-top:24px">
  <button type="submit" class="btn">&#128190; Uložit</button>
  <button type="button" class="btn btn-r" onclick="doReset()">&#9851; Reset</button>
  <span class="ok" id="ok_msg">&#10003; Uloženo! Zařízení se restartuje...</span>
</div>
</form>
<script>
function sv(id,v){var e=document.getElementById(id);if(e)e.value=v||'';}
fetch('/api/config').then(r=>r.json()).then(d=>{
  sv('mqtt_server',d.mqtt_server);sv('mqtt_port',d.mqtt_port);
  sv('mqtt_user',d.mqtt_user);sv('mqtt_pass',d.mqtt_pass);
  sv('mqtt_client_id',d.mqtt_client_id);sv('mqtt_base_topic',d.mqtt_base_topic);
  sv('ha_url',d.ha_url);sv('ha_token',d.ha_token);
  sv('weather_lat',d.weather_lat);sv('weather_lon',d.weather_lon);
  sv('entity_venku_temp',d.entity_venku_temp);sv('entity_obyvak_temp',d.entity_obyvak_temp);
  sv('entity_weather',d.entity_weather);sv('entity_kli_pracovna',d.entity_kli_pracovna);
  sv('entity_kli_bojler',d.entity_kli_bojler);sv('entity_kli_kotel',d.entity_kli_kotel);
  sv('entity_thermostat',d.entity_thermostat);
  if(d.screensaver_timeout!==undefined){var s=document.getElementById('screensaver_timeout');if(s)s.value=d.screensaver_timeout;}
  if(d.lights)d.lights.forEach((l,i)=>{sv('l'+i+'e',l.entity);sv('l'+i+'n',l.name);});
  if(d.energy)d.energy.forEach((e,i)=>{sv('e'+i+'e',e.entity);sv('e'+i+'n',e.name);sv('e'+i+'u',e.unit);});
});
document.getElementById('frm').addEventListener('submit',ev=>{
  ev.preventDefault();
  var g=id=>document.getElementById(id).value;
  var data={
    mqtt_server:g('mqtt_server'),mqtt_port:parseInt(g('mqtt_port'))||1883,
    mqtt_user:g('mqtt_user'),mqtt_pass:g('mqtt_pass'),
    mqtt_client_id:g('mqtt_client_id'),mqtt_base_topic:g('mqtt_base_topic'),
    ha_url:g('ha_url'),ha_token:g('ha_token'),
    weather_lat:g('weather_lat'),weather_lon:g('weather_lon'),
    entity_venku_temp:g('entity_venku_temp'),entity_obyvak_temp:g('entity_obyvak_temp'),
    entity_weather:g('entity_weather'),entity_kli_pracovna:g('entity_kli_pracovna'),
    entity_kli_bojler:g('entity_kli_bojler'),entity_kli_kotel:g('entity_kli_kotel'),
    entity_thermostat:g('entity_thermostat'),
    screensaver_timeout:parseInt(document.getElementById('screensaver_timeout').value)||0,
    lights:[],energy:[]
  };
  for(var i=0;i<6;i++){
    data.lights.push({entity:g('l'+i+'e'),name:g('l'+i+'n')});
    data.energy.push({entity:g('e'+i+'e'),name:g('e'+i+'n'),unit:g('e'+i+'u')});
  }
  fetch('/api/config',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(data)})
    .then(r=>r.json()).then(d=>{if(d.ok)document.getElementById('ok_msg').style.display='inline';});
});
function doReset(){if(confirm('Resetovat na výchozí?'))fetch('/api/reset',{method:'POST'}).then(()=>location.reload());}
</script>
</body>
</html>)HTML";

/* ── Web handlery ─────────────────────────────────────────────────────── */
static void handle_root() {
    web_server.send_P(200, "text/html; charset=utf-8", CONFIG_HTML);
}

static void handle_api_get() {
    JsonDocument doc;
    doc["mqtt_server"]     = g_cfg.mqtt_server;
    doc["mqtt_port"]       = g_cfg.mqtt_port;
    doc["mqtt_user"]       = g_cfg.mqtt_user;
    doc["mqtt_pass"]       = g_cfg.mqtt_pass;
    doc["mqtt_client_id"]  = g_cfg.mqtt_client_id;
    doc["mqtt_base_topic"] = g_cfg.mqtt_base_topic;
    doc["ha_url"]          = g_cfg.ha_url;
    doc["ha_token"]        = g_cfg.ha_token;
    doc["weather_lat"]     = g_cfg.weather_lat;
    doc["weather_lon"]     = g_cfg.weather_lon;
    doc["entity_venku_temp"]   = g_cfg.entity_venku_temp;
    doc["entity_obyvak_temp"]  = g_cfg.entity_obyvak_temp;
    doc["entity_weather"]      = g_cfg.entity_weather;
    doc["entity_kli_pracovna"] = g_cfg.entity_kli_pracovna;
    doc["entity_kli_bojler"]   = g_cfg.entity_kli_bojler;
    doc["entity_kli_kotel"]    = g_cfg.entity_kli_kotel;
    doc["entity_thermostat"]   = g_cfg.entity_thermostat;
    doc["screensaver_timeout"] = g_cfg.screensaver_timeout;

    JsonArray lights = doc["lights"].to<JsonArray>();
    for (int i = 0; i < CFG_MAX_LIGHTS; i++) {
        JsonObject l = lights.add<JsonObject>();
        l["entity"] = g_cfg.light_entity[i];
        l["name"]   = g_cfg.light_name[i];
    }
    JsonArray energy = doc["energy"].to<JsonArray>();
    for (int i = 0; i < CFG_MAX_ENERGY; i++) {
        JsonObject e = energy.add<JsonObject>();
        e["entity"] = g_cfg.energy_entity[i];
        e["name"]   = g_cfg.energy_name[i];
        e["unit"]   = g_cfg.energy_unit[i];
    }
    String out;
    serializeJson(doc, out);
    web_server.send(200, "application/json", out);
}

static void handle_api_post() {
    if (!web_server.hasArg("plain")) {
        web_server.send(400, "application/json", "{\"ok\":false}");
        return;
    }
    JsonDocument doc;
    if (deserializeJson(doc, web_server.arg("plain"))) {
        web_server.send(400, "application/json", "{\"ok\":false}");
        return;
    }
#define CP(field, src) strlcpy(g_cfg.field, doc[src] | g_cfg.field, sizeof(g_cfg.field))
    CP(mqtt_server,     "mqtt_server");
    g_cfg.mqtt_port = doc["mqtt_port"] | g_cfg.mqtt_port;
    CP(mqtt_user,       "mqtt_user");
    CP(mqtt_pass,       "mqtt_pass");
    CP(mqtt_client_id,  "mqtt_client_id");
    CP(mqtt_base_topic, "mqtt_base_topic");
    CP(ha_url,          "ha_url");
    CP(ha_token,        "ha_token");
    CP(weather_lat,     "weather_lat");
    CP(weather_lon,     "weather_lon");
    CP(entity_venku_temp,   "entity_venku_temp");
    CP(entity_obyvak_temp,  "entity_obyvak_temp");
    CP(entity_weather,      "entity_weather");
    CP(entity_kli_pracovna, "entity_kli_pracovna");
    CP(entity_kli_bojler,   "entity_kli_bojler");
    CP(entity_kli_kotel,    "entity_kli_kotel");
    CP(entity_thermostat,   "entity_thermostat");
    g_cfg.screensaver_timeout = (uint8_t)(doc["screensaver_timeout"] | (int)g_cfg.screensaver_timeout);
#undef CP
    JsonArray lights = doc["lights"];
    if (!lights.isNull())
        for (int i = 0; i < CFG_MAX_LIGHTS && i < (int)lights.size(); i++) {
            strlcpy(g_cfg.light_entity[i], lights[i]["entity"] | g_cfg.light_entity[i], 80);
            strlcpy(g_cfg.light_name[i],   lights[i]["name"]   | g_cfg.light_name[i],   32);
        }
    JsonArray energy = doc["energy"];
    if (!energy.isNull())
        for (int i = 0; i < CFG_MAX_ENERGY && i < (int)energy.size(); i++) {
            strlcpy(g_cfg.energy_entity[i], energy[i]["entity"] | g_cfg.energy_entity[i], 80);
            strlcpy(g_cfg.energy_name[i],   energy[i]["name"]   | g_cfg.energy_name[i],   32);
            strlcpy(g_cfg.energy_unit[i],   energy[i]["unit"]   | g_cfg.energy_unit[i],    8);
        }
    g_cfg.configured = true;
    config_manager_save();
    web_server.send(200, "application/json", "{\"ok\":true,\"restart\":true}");
    Serial.println("[CFG] Updated via web portal – restarting...");
    delay(500);
    ESP.restart();
}

static void handle_reset() {
    set_defaults();
    config_manager_save();
    web_server.send(200, "application/json", "{\"ok\":true}");
    Serial.println("[CFG] Reset to defaults");
}

static void handle_not_found() {
    web_server.sendHeader("Location", "/", true);
    web_server.send(302, "text/plain", "");
}

/* ── Inicializace ─────────────────────────────────────────────────────── */
void config_manager_init() {
    config_manager_load();

    WiFiManager wm;
    wm.setConnectTimeout(20);
    wm.setConfigPortalTimeout(180);   /* 3 min na portál, pak boot bez WiFi */
 

    /* MQTT parametry přímo v captive portálu */
    WiFiManagerParameter p_srv  ("mqtt_srv",  "MQTT Server",  g_cfg.mqtt_server,     63);
    WiFiManagerParameter p_port ("mqtt_prt",  "MQTT Port",    String(g_cfg.mqtt_port).c_str(), 5);
    WiFiManagerParameter p_user ("mqtt_usr",  "MQTT Uzivatel",g_cfg.mqtt_user,       47);
    WiFiManagerParameter p_pass ("mqtt_pw",   "MQTT Heslo",   g_cfg.mqtt_pass,       47);
    WiFiManagerParameter p_topic("base_tp",   "Base topic",   g_cfg.mqtt_base_topic, 47);
    wm.addParameter(&p_srv);
    wm.addParameter(&p_port);
    wm.addParameter(&p_user);
    wm.addParameter(&p_pass);
    wm.addParameter(&p_topic);

    bool connected;
    if (g_cfg.configured) {
        connected = wm.autoConnect("HA-Dashboard", "ha-dashboard");
    } else {
        /* první boot – vždy zobraz portál */
        connected = wm.startConfigPortal("HA-Dashboard", "ha-dashboard");
    }

    if (connected) {
        /* Uloz WiFi parametry z captive portálu */
        if (strlen(p_srv.getValue())   > 0) strlcpy(g_cfg.mqtt_server,     p_srv.getValue(),   sizeof(g_cfg.mqtt_server));
        if (strlen(p_port.getValue())  > 0) g_cfg.mqtt_port = atoi(p_port.getValue());
        if (strlen(p_user.getValue())  > 0) strlcpy(g_cfg.mqtt_user,       p_user.getValue(),  sizeof(g_cfg.mqtt_user));
        if (strlen(p_pass.getValue())  > 0) strlcpy(g_cfg.mqtt_pass,       p_pass.getValue(),  sizeof(g_cfg.mqtt_pass));
        if (strlen(p_topic.getValue()) > 0) strlcpy(g_cfg.mqtt_base_topic, p_topic.getValue(), sizeof(g_cfg.mqtt_base_topic));
        g_cfg.configured = true;
        config_manager_save();
    }

    /* Spust webovy konfiguracni portal na pozadi */
    s_portal_active = true;
    web_server.on("/",            HTTP_GET,  handle_root);
    web_server.on("/api/config",  HTTP_GET,  handle_api_get);
    web_server.on("/api/config",  HTTP_POST, handle_api_post);
    web_server.on("/api/reset",   HTTP_POST, handle_reset);
    web_server.onNotFound(handle_not_found);

     // ← PŘIDAT TADY (před web_server.begin):
    WiFi.mode(WIFI_AP_STA);
    WiFi.softAP("HA-Dashboard", "ha-dashboard");
    Serial.printf("[CFG] AP spuštěn: 192.168.4.1\n");
    
    web_server.begin();
    Serial.printf("[CFG] Web portal at http://%s/\n", WiFi.localIP().toString().c_str());
}
