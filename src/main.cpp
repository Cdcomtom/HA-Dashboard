/*
 * main.cpp — ESP32 HA Dashboard  (ESP32-8048S070C, 7″ 800×480)
 * ──────────────────────────────────────────────────────────────
 * Responsibilities:
 *   • Display + LVGL initialisation (via display.h / setup_display())
 *   • WiFi connection managed by WiFiManager (config portal on first boot)
 *   • NTP time sync (Prague timezone, automatic DST)
 *   • MQTT client — subscribes to HA state stream, parses sensor values
 *     into dashboard_data.h globals, drives the SYSTEM debug ring-buffer
 *   • HA REST API — ha_call_service() POSTs to {ha_url}/api/services/…
 *     for light toggle and thermostat control; assigned to g_ha_call
 *   • Open-Meteo forecast — HTTP GET every 30 min, first fetch after 10 s
 *     to avoid blocking display startup
 *
 * Build: PlatformIO, espressif32 platform, arduino framework
 * Partition: huge_app.csv  (3 MB app — required for LVGL + all libraries)
 */

#include "display.h"
#include <time.h>
#include "dashboard_data.h"
#include "config_manager.h"

extern "C" {
    #include "screens.h"
}

#define ENABLE_WIFI_NTP   1
#define ENABLE_MQTT       1

#ifndef NTP_SERVER1
#  define NTP_SERVER1 "tik.cesnet.cz"
#endif
#ifndef NTP_SERVER2
#  define NTP_SERVER2 "pool.ntp.org"
#endif
#define TZ_PRAGUE "CET-1CEST,M3.5.0,M10.5.0/3"

/* === Sdílená data (definice) === */
bool  g_wifi_ok        = false;
bool  g_mqtt_connected = false;
bool  g_data_valid     = false;
mqtt_publish_fn_t g_mqtt_publish = NULL;
ha_call_fn_t      g_ha_call      = NULL;

float g_venku_temp = 0.0f, g_venku_hum   = 0.0f;
float g_obyvak_temp= 0.0f, g_obyvak_hum  = 0.0f;
float g_weather_temp = 0.0f, g_weather_humidity = 0.0f;
float g_weather_pressure = 0.0f, g_weather_wind = 0.0f;
float g_weather_temp_min = 0.0f, g_weather_temp_max = 0.0f;
char  g_weather_state[24]    = "sunny";
char  g_weather_state_cz[32] = "Slune\xc4\x8dno";
forecast_day_t g_forecast[FORECAST_DAYS] = {0};
float g_kli_pracovna_temp = 0.0f, g_kli_bojler_temp = 0.0f, g_kli_kotel_temp = 0.0f;
float g_thermostat_current = 0.0f;
bool  g_light_obyvak = false, g_light_kuchyn  = false, g_light_pocitac  = false;
bool  g_light_koupelna = false, g_light_predsin = false, g_light_zahrada = false;
float g_energy_power[CFG_MAX_ENERGY] = {0};

/* ── MQTT debug ring-buffer ──────────────────────────────────────────────── */
mqtt_dbg_line_t g_mqtt_dbg[MQTT_DBG_LINES] = {};
int             g_mqtt_dbg_count = 0;
char            g_mqtt_base_topic_dbg[48] = {};

/* ===== Hodiny + datum ===== */
static const char* DAY_CZ[] = { "Ne", "Po", "\xc3\x9at", "St", "\xc4\x8ct", "P\xc3\xa1", "So" };
static unsigned long last_update_ms = 0;
static bool g_ntp_synced = false;

static void update_clock_and_date() {
    if (millis() - last_update_ms < 1000) return;
    last_update_ms = millis();
    char time_buf[8], date_buf[16];
    struct tm t;
    if (g_ntp_synced && getLocalTime(&t, 0)) {
        snprintf(time_buf, sizeof(time_buf), "%02d:%02d", t.tm_hour, t.tm_min);
        snprintf(date_buf, sizeof(date_buf), "%s %d.%d.",
                 DAY_CZ[t.tm_wday], t.tm_mday, t.tm_mon + 1);
        lv_label_set_text(objects.lbl_cas, time_buf);
        lv_label_set_text(objects.lbl_datum, date_buf);
        return;
    }
    static int fake_hour = 14, fake_min = 32;
    static unsigned long last_minute_tick = 0;
    if (millis() - last_minute_tick > 60000UL) {
        last_minute_tick = millis();
        if (++fake_min >= 60) { fake_min = 0; fake_hour = (fake_hour + 1) % 24; }
        snprintf(time_buf, sizeof(time_buf), "%02d:%02d", fake_hour, fake_min);
        lv_label_set_text(objects.lbl_cas, time_buf);
    }
}

/* ===== HA REST API + Open-Meteo ===== */
#include <HTTPClient.h>
#include <ArduinoJson.h>

static void ha_call_service(const char *service_path, const char *json_body) {
    if (!g_wifi_ok || !g_cfg.ha_url[0] || !g_cfg.ha_token[0]) return;
    HTTPClient http;
    String url = String(g_cfg.ha_url) + "/api/services/" + service_path;
    http.begin(url);
    http.setTimeout(5000);
    http.addHeader("Authorization", String("Bearer ") + g_cfg.ha_token);
    http.addHeader("Content-Type", "application/json");
    int rc = http.POST(json_body);
    http.end();
    Serial.printf("HA API: %s %s → %d\n", service_path, json_body, rc);
}

/* Refresh stavů světel přes HA REST API.
 * Voláno po každém MQTT připojení — MQTT retain nemusí dodat stav světel
 * která se od posledního připojení nezměnila. */
static void refresh_light_states() {
    if (!g_wifi_ok || !g_cfg.ha_url[0] || !g_cfg.ha_token[0]) return;
    Serial.println("Refreshing light states via REST...");
    bool *state_ptrs[CFG_MAX_LIGHTS] = {
        &g_light_obyvak, &g_light_kuchyn, &g_light_pocitac,
        &g_light_koupelna, &g_light_predsin, &g_light_zahrada
    };
    for (int i = 0; i < CFG_MAX_LIGHTS; i++) {
        if (!g_cfg.light_entity[i][0]) continue;
        HTTPClient http;
        String url = String(g_cfg.ha_url) + "/api/states/" + g_cfg.light_entity[i];
        http.begin(url);
        http.setTimeout(3000);
        http.addHeader("Authorization", String("Bearer ") + g_cfg.ha_token);
        int rc = http.GET();
        if (rc == 200) {
            String body = http.getString();
            JsonDocument doc;
            if (!deserializeJson(doc, body)) {
                const char *st = doc["state"] | "off";
                *state_ptrs[i] = (strcmp(st, "on") == 0);
                Serial.printf("  light[%d] %s = %s\n", i, g_cfg.light_entity[i], st);
            }
        } else {
            Serial.printf("  light[%d] REST err %d\n", i, rc);
        }
        http.end();
    }
}

static const char* wmo_to_condition(int code) {
    if (code == 0)               return "sunny";
    if (code <= 3)               return "partlycloudy";
    if (code <= 48)              return "fog";
    if (code <= 57)              return "rainy";
    if (code <= 67)              return "rainy";
    if (code <= 77)              return "snowy";
    if (code <= 82)              return "pouring";
    if (code <= 86)              return "snowy";
    if (code <= 99)              return "lightning-rainy";
    return "cloudy";
}

static void fetch_forecast_openmeteo() {
    if (!g_wifi_ok || !g_cfg.weather_lat[0] || !g_cfg.weather_lon[0]) return;
    HTTPClient http;
    /* Open-Meteo podporuje HTTP i HTTPS – použijeme HTTP (šetří ~150KB firmware) */
    String url = String("http://api.open-meteo.com/v1/forecast?latitude=") +
                 g_cfg.weather_lat + "&longitude=" + g_cfg.weather_lon +
                 "&daily=temperature_2m_max,temperature_2m_min,weathercode"
                 "&timezone=auto&forecast_days=7";
    http.begin(url);
    http.setTimeout(8000);   /* max 8 s – nepozastaví loop na dlouho */
    int code = http.GET();
    if (code == 200) {
        String body = http.getString();
        JsonDocument doc;
        if (!deserializeJson(doc, body)) {
            JsonObject daily = doc["daily"];
            JsonArray tmax  = daily["temperature_2m_max"];
            JsonArray tmin  = daily["temperature_2m_min"];
            JsonArray wcod  = daily["weathercode"];
            struct tm now = {}; int twday = 0;
            if (g_ntp_synced && getLocalTime(&now, 0)) twday = now.tm_wday;
            static const char* DAY_CZ[] = { "Ne","Po","\xc3\x9at","St","\xc4\x8ct","P\xc3\xa1","So" };
            for (int i = 0; i < FORECAST_DAYS && i + 1 < (int)tmax.size(); i++) {
                g_forecast[i].temp_max = tmax[i + 1] | 0.0f;
                g_forecast[i].temp_min = tmin[i + 1] | 0.0f;
                strlcpy(g_forecast[i].condition, wmo_to_condition(wcod[i + 1] | 0),
                        sizeof(g_forecast[i].condition));
                strlcpy(g_forecast[i].day, DAY_CZ[(twday + 1 + i) % 7],
                        sizeof(g_forecast[i].day));
                g_forecast[i].valid = true;
            }
            Serial.println("Open-Meteo forecast OK");
        }
    } else {
        Serial.printf("Open-Meteo HTTP err: %d\n", code);
    }
    http.end();
}

/* ===== MQTT ===== */
#if ENABLE_MQTT
#include <PubSubClient.h>
#include <WiFi.h>

WiFiClient   wifi_client;
PubSubClient mqtt_client(wifi_client);

/* Topic cache — sestaví se po načtení konfigurace */
static String t_venku_temp, t_obyvak_temp;
static String t_weather, t_weather_temp_a, t_weather_hum_a;
static String t_weather_press_a, t_weather_wind_a, t_weather_fc_a;
static String t_kli_pracovna, t_kli_bojler, t_kli_kotel;
static String t_thermostat_curr;
static String t_light[CFG_MAX_LIGHTS];
static String t_energy[CFG_MAX_ENERGY];

static void build_topic_cache() {
    t_venku_temp    = cfg_topic(g_cfg.entity_venku_temp);
    t_obyvak_temp   = cfg_topic(g_cfg.entity_obyvak_temp);
    t_weather       = cfg_topic(g_cfg.entity_weather);
    t_weather_temp_a  = cfg_topic_attr(g_cfg.entity_weather, "temperature");
    t_weather_hum_a   = cfg_topic_attr(g_cfg.entity_weather, "humidity");
    t_weather_press_a = cfg_topic_attr(g_cfg.entity_weather, "pressure");
    t_weather_wind_a  = cfg_topic_attr(g_cfg.entity_weather, "wind_speed");
    t_weather_fc_a    = cfg_topic_attr(g_cfg.entity_weather, "forecast");
    t_kli_pracovna  = cfg_topic(g_cfg.entity_kli_pracovna);
    t_kli_bojler    = cfg_topic(g_cfg.entity_kli_bojler);
    t_kli_kotel     = cfg_topic(g_cfg.entity_kli_kotel);
    t_thermostat_curr = cfg_topic_attr(g_cfg.entity_thermostat, "current_temperature");
    for (int i = 0; i < CFG_MAX_LIGHTS;  i++) t_light[i]  = cfg_topic(g_cfg.light_entity[i]);
    for (int i = 0; i < CFG_MAX_ENERGY; i++) t_energy[i] = cfg_topic(g_cfg.energy_entity[i]);
    // Zkopiruj base topic do C-pristupsneho globalu
    strlcpy(g_mqtt_base_topic_dbg, g_cfg.mqtt_base_topic, sizeof(g_mqtt_base_topic_dbg));
    // --- DEBUG: vypis vsech nakonfigurovanych MQTT topic ---
    Serial.println("=== MQTT topic cache ===");
    Serial.printf("  venku_temp  : %s\n", t_venku_temp.c_str());
    Serial.printf("  obyvak_temp : %s\n", t_obyvak_temp.c_str());
    Serial.printf("  weather     : %s\n", t_weather.c_str());
    Serial.printf("  weather_temp: %s\n", t_weather_temp_a.c_str());
    Serial.printf("  weather_fc  : %s\n", t_weather_fc_a.c_str());
    Serial.printf("  thermostat  : %s\n", t_thermostat_curr.c_str());
    for (int i = 0; i < CFG_MAX_LIGHTS;  i++) if (t_light[i].length())  Serial.printf("  light[%d]    : %s\n", i, t_light[i].c_str());
    for (int i = 0; i < CFG_MAX_ENERGY; i++) if (t_energy[i].length()) Serial.printf("  energy[%d]   : %s\n", i, t_energy[i].c_str());
    Serial.println("========================");
}

static const char* weather_state_to_cz(const char* st) {
    if (!st) return "?";
    if (!strcmp(st,"sunny")||!strcmp(st,"clear")||!strcmp(st,"clear-night")) return "Slune\xc4\x8dno";
    if (!strcmp(st,"partlycloudy")) return "Polojasno";
    if (!strcmp(st,"cloudy"))       return "Obla\xc4\x8dno";
    if (!strcmp(st,"fog"))          return "Mlha";
    if (!strcmp(st,"rainy"))        return "D\xc3\xa9\xc5\xa1\xc5\xa5";
    if (!strcmp(st,"pouring"))      return "Lij\xc3\xa1k";
    if (!strcmp(st,"snowy"))        return "Sn\xc3\xadh";
    if (!strcmp(st,"snowy-rainy"))  return "D\xc3\xa9\xc5\xa1\xc5\xa5 se sn\xc4\x9bhem";
    if (!strcmp(st,"hail"))         return "Kroupy";
    if (!strcmp(st,"lightning"))    return "Bou\xc5\x99ka";
    if (!strcmp(st,"lightning-rainy")) return "Bou\xc5\x99ka s de\xc5\xa1t\xc4\x9bm";
    if (!strcmp(st,"windy"))        return "V\xc4\x9btrno";
    return st;
}

static void parse_forecast_json(const char *json, size_t json_len) {
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, json, json_len);
    if (err) { Serial.printf("Forecast JSON err: %s\n", err.c_str()); return; }
    JsonArray arr = doc.as<JsonArray>();
    if (arr.isNull()) { Serial.println("Forecast: not an array"); return; }

    int today_wday = 0;
    struct tm now;
    if (getLocalTime(&now, 0)) today_wday = now.tm_wday;

    size_t start = 0;
    if (arr.size() > 0) {
        const char *first_dt = arr[0]["datetime"] | "";
        if (first_dt[0]) {
            int y=0, mo=0, d=0;
            sscanf(first_dt, "%d-%d-%d", &y, &mo, &d);
            if (now.tm_year+1900==y && now.tm_mon+1==mo && now.tm_mday==d) start=1;
        }
    }
    int filled = 0;
    for (size_t i = start; i < arr.size() && filled < FORECAST_DAYS; i++, filled++) {
        JsonObject day = arr[i];
        const char *cond = day["condition"] | "";
        float tmax = day["temperature"] | 0.0f;
        float tmin = day["templow"]     | 0.0f;
        int wday = (today_wday + 1 + filled) % 7;
        strlcpy(g_forecast[filled].day,       DAY_CZ[wday], sizeof(g_forecast[filled].day));
        strlcpy(g_forecast[filled].condition, cond,         sizeof(g_forecast[filled].condition));
        g_forecast[filled].temp_max = tmax;
        g_forecast[filled].temp_min = tmin;
        g_forecast[filled].valid    = true;
    }
    if (start > 0) {
        g_weather_temp_max = arr[0]["temperature"] | 0.0f;
        g_weather_temp_min = arr[0]["templow"]     | 0.0f;
    }
    Serial.printf("Forecast parsed: %d days\n", filled);
}

void mqtt_callback(const char* topic, byte* payload, unsigned int length) {
    g_data_valid = true;

    /* Debug ring-buffer — uloz posledni MQTT_DBG_LINES zprav pro SYSTEM tab */
    {
        int idx = g_mqtt_dbg_count % MQTT_DBG_LINES;
        strlcpy(g_mqtt_dbg[idx].topic, topic, MQTT_DBG_TOPIC_LEN);
        uint16_t vl = length < (MQTT_DBG_VAL_LEN - 1) ? (uint16_t)length : (MQTT_DBG_VAL_LEN - 1);
        memcpy(g_mqtt_dbg[idx].value, payload, vl);
        g_mqtt_dbg[idx].value[vl] = '\0';
        g_mqtt_dbg_count++;
    }
    /* Serial log — prvnich 30 zprav */
    static int dbg_cnt = 0;
    if (dbg_cnt < 30) {
        Serial.printf("MQTT[%d] %s = %.*s\n", dbg_cnt++, topic, (int)(length < 60 ? length : 60), (char*)payload);
    }

    /* Forecast — velký JSON, parsuj přímo */
    if (t_weather_fc_a.length() && String(topic) == t_weather_fc_a) {
        parse_forecast_json((const char*)payload, length);
        return;
    }

    char msg[256] = {};
    if (length >= sizeof(msg)) length = sizeof(msg) - 1;
    memcpy(msg, payload, length);

    String t(topic);

    if      (t == t_venku_temp)       g_venku_temp       = atof(msg);
    else if (t == t_obyvak_temp)      g_obyvak_temp      = atof(msg);
    else if (t == t_weather) {
        strlcpy(g_weather_state,    msg, sizeof(g_weather_state));
        strlcpy(g_weather_state_cz, weather_state_to_cz(msg), sizeof(g_weather_state_cz));
    }
    else if (t == t_weather_temp_a)   g_weather_temp     = atof(msg);
    else if (t == t_weather_hum_a)    g_weather_humidity = atof(msg);
    else if (t == t_weather_press_a)  g_weather_pressure = atof(msg);
    else if (t == t_weather_wind_a)   g_weather_wind     = atof(msg);
    else if (t == t_kli_pracovna)     g_kli_pracovna_temp = atof(msg);
    else if (t == t_kli_bojler)       g_kli_bojler_temp   = atof(msg);
    else if (t == t_kli_kotel)        g_kli_kotel_temp    = atof(msg);
    else if (t == t_thermostat_curr)  g_thermostat_current = atof(msg);
    else {
        /* Světla */
        for (int i = 0; i < CFG_MAX_LIGHTS; i++) {
            if (t_light[i].length() && t == t_light[i]) {
                bool on = !strcmp(msg, "on");
                switch (i) {
                    case 0: g_light_obyvak   = on; break;
                    case 1: g_light_kuchyn   = on; break;
                    case 2: g_light_pocitac  = on; break;
                    case 3: g_light_koupelna = on; break;
                    case 4: g_light_predsin  = on; break;
                    case 5: g_light_zahrada  = on; break;
                }
                return;
            }
        }
        /* Energy */
        for (int i = 0; i < CFG_MAX_ENERGY; i++) {
            if (t_energy[i].length() && t == t_energy[i]) {
                g_energy_power[i] = atof(msg);
                return;
            }
        }
    }
}

static void do_mqtt_publish(const char *topic, const char *payload) {
    if (mqtt_client.connected()) {
        mqtt_client.publish(topic, payload);
        Serial.printf("MQTT pub: %s = %s\n", topic, payload);
    }
}

static void mqtt_reconnect() {
    bool ok;
    if (strlen(g_cfg.mqtt_user) > 0)
        ok = mqtt_client.connect(g_cfg.mqtt_client_id, g_cfg.mqtt_user, g_cfg.mqtt_pass);
    else
        ok = mqtt_client.connect(g_cfg.mqtt_client_id);
    if (ok) {
        Serial.println("MQTT: connected");
        mqtt_client.subscribe((String(g_cfg.mqtt_base_topic) + "/#").c_str());
        g_mqtt_connected = true;
        refresh_light_states();   /* dotáhni aktuální stavy — MQTT retain nestačí */
    } else {
        Serial.printf("MQTT: failed, rc=%d\n", mqtt_client.state());
        g_mqtt_connected = false;
    }
}
#endif /* ENABLE_MQTT */

/* ===== SETUP ===== */
void setup() {
    setup_display();   /* inicializuje display, LVGL, dotyk, Serial.begin */
    Serial.println("HA Dashboard boot");
    create_screens();

#if ENABLE_WIFI_NTP
    config_manager_init();   /* WiFiManager – připojí se nebo spustí portál */
    g_wifi_ok = (WiFi.status() == WL_CONNECTED);

    if (g_wifi_ok) {
        configTzTime(TZ_PRAGUE, NTP_SERVER1, NTP_SERVER2);
        /* Počkej max 5 s na sync */
        struct tm t;
        for (int i = 0; i < 10 && !getLocalTime(&t, 0); i++) delay(500);
        g_ntp_synced = getLocalTime(&t, 0);
        Serial.printf("NTP synced: %s\n", g_ntp_synced ? "yes" : "no");
    }
#endif

#if ENABLE_MQTT
    build_topic_cache();
    mqtt_client.setServer(g_cfg.mqtt_server, g_cfg.mqtt_port);
    mqtt_client.setCallback(mqtt_callback);
    mqtt_client.setBufferSize(8192);   /* forecast JSON může být velký */
    g_mqtt_publish = do_mqtt_publish;
    g_ha_call      = ha_call_service;
    if (g_wifi_ok) mqtt_reconnect();
    /* fetch_forecast_openmeteo() se zavolá v loop() po 10 s – neblokuje setup() */
#endif
}

/* ===== LOOP ===== */
void loop() {
    lv_task_handler();
    delay(5);

    update_clock_and_date();

#if ENABLE_WIFI_NTP
    g_wifi_ok = (WiFi.status() == WL_CONNECTED);
#endif

#if ENABLE_MQTT
    if (g_wifi_ok) {
        if (!mqtt_client.connected()) {
            static unsigned long last_reconnect = 0;
            if (millis() - last_reconnect > 5000) {
                last_reconnect = millis();
                mqtt_reconnect();
            }
        } else {
            mqtt_client.loop();
        }
    }
    config_manager_handle();

    /* Open-Meteo předpověď – první fetch po 10 s od bootu, pak každých 30 minut */
    static unsigned long last_forecast_ms = 0;
    static bool forecast_done = false;
    unsigned long now_ms = millis();
    bool do_fetch = g_wifi_ok && (
        (!forecast_done && now_ms > 10000UL) ||
        (forecast_done  && now_ms - last_forecast_ms > 30UL * 60UL * 1000UL)
    );
    if (do_fetch) {
        last_forecast_ms = now_ms;
        forecast_done    = true;
        fetch_forecast_openmeteo();
    }
#endif
}
