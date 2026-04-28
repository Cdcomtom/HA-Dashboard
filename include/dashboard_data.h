/*
 * dashboard_data.h — Shared global state for ESP32 HA Dashboard
 * ──────────────────────────────────────────────────────────────
 * All live data is written by main.cpp (MQTT callbacks, REST responses)
 * and read by screens.c (LVGL UI). This header is the only bridge
 * between the C++ firmware layer and the C UI layer.
 *
 * Inclusion rules:
 *   C++ files  →  #include "dashboard_data.h"   (full access)
 *   C files    →  #include "dashboard_data.h"   (safe — guarded with stdbool.h)
 *
 * Thread safety: all writes happen on the Arduino main task; LVGL
 * reads happen in lv_task_handler() on the same task → no mutex needed.
 */
#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── Connection status ───────────────────────────────────────────────────────
 * Set in main.cpp loop(). Read by SYSTEM tab and MQTT reconnect logic.     */
extern bool g_wifi_ok;          /* true = WiFi connected                    */
extern bool g_mqtt_connected;   /* true = MQTT broker connected             */

/* ── HOME — temperature cards ───────────────────────────────────────────────
 * Updated via MQTT state topics for the configured sensor entities.        */
extern float g_venku_temp;    /* outdoor temperature  [°C]                  */
extern float g_venku_hum;     /* outdoor humidity     [%]                   */
extern float g_obyvak_temp;   /* living-room temperature [°C]               */
extern float g_obyvak_hum;    /* living-room humidity    [%]                */

/* ── HOME — current weather ──────────────────────────────────────────────────
 * Sourced from the HA weather.* entity via MQTT attribute topics.          */
extern float g_weather_temp;       /* current temperature [°C]              */
extern float g_weather_humidity;   /* current humidity    [%]               */
extern float g_weather_pressure;   /* pressure            [hPa]             */
extern float g_weather_wind;       /* wind speed          [km/h or m/s]     */
extern float g_weather_temp_min;   /* today min temperature [°C]            */
extern float g_weather_temp_max;   /* today max temperature [°C]            */
extern char  g_weather_state[24];  /* HA condition string, e.g. "sunny"     */
extern char  g_weather_state_cz[32]; /* Czech translation, e.g. "Slunečno" */

/* ── HOME — 5-day forecast ───────────────────────────────────────────────────
 * Fetched from Open-Meteo (free, no API key). Updated every 30 minutes.
 * Index 0 = tomorrow, index 4 = 5 days from now.                          */
#define FORECAST_DAYS 5
typedef struct {
    char  day[6];        /* Short Czech day name: "Po", "Út", … (UTF-8 + \0) */
    char  condition[24]; /* HA condition string: "cloudy", "sunny", …         */
    float temp_max;      /* Day high [°C]                                     */
    float temp_min;      /* Day low  [°C]                                     */
    bool  valid;         /* false = data not yet available, skip rendering    */
} forecast_day_t;
extern forecast_day_t g_forecast[FORECAST_DAYS];

/* ── KLIMATE — heating / climate sensors ────────────────────────────────────
 * Updated via MQTT state topics. Entity IDs configured in config portal.  */
extern float g_kli_pracovna_temp;  /* study / workroom temperature  [°C]   */
extern float g_kli_bojler_temp;    /* boiler temperature             [°C]   */
extern float g_kli_kotel_temp;     /* boiler/furnace temperature     [°C]   */
extern float g_thermostat_current; /* thermostat current temperature [°C]   */

/* ── ENERGY ──────────────────────────────────────────────────────────────────
 * Up to CFG_MAX_ENERGY slots. Values in W or kWh depending on entity.     */
#ifndef CFG_MAX_ENERGY
#  define CFG_MAX_ENERGY 6
#endif
extern float g_energy_power[CFG_MAX_ENERGY];

/* ── LIGHTS — on/off state ───────────────────────────────────────────────────
 * Written by MQTT callback (real state), also updated optimistically when
 * the user taps a toggle (before MQTT echo arrives).                       */
extern bool g_light_obyvak;    /* Living room                               */
extern bool g_light_kuchyn;    /* Kitchen                                   */
extern bool g_light_pocitac;   /* PC desk lamp                              */
extern bool g_light_koupelna;  /* Bathroom                                  */
extern bool g_light_predsin;   /* Hallway / entrance                        */
extern bool g_light_zahrada;   /* Garden                                    */

/* ── Data validity ───────────────────────────────────────────────────────────
 * Set to true after the first MQTT message arrives. Used by the UI to
 * distinguish "not yet received" from "genuinely zero".                    */
extern bool g_data_valid;

/* ── MQTT publish hook ───────────────────────────────────────────────────────
 * Assigned in main.cpp after MQTT is connected.
 * Call from screens.c to publish any MQTT topic.
 * topic   — full MQTT topic string
 * payload — null-terminated string payload                                 */
typedef void (*mqtt_publish_fn_t)(const char *topic, const char *payload);
extern mqtt_publish_fn_t g_mqtt_publish;

/* ── HA REST API hook ────────────────────────────────────────────────────────
 * Assigned in main.cpp after WiFi connects.
 * Used by screens.c to control any HA entity without knowing HTTP details.
 *
 * service_path — HA service domain/service, e.g.:
 *                  "homeassistant/turn_on"
 *                  "homeassistant/turn_off"
 *                  "climate/set_temperature"
 *                  "climate/set_hvac_mode"
 * json_body    — JSON payload, e.g.:
 *                  "{\"entity_id\":\"light.obyvak\"}"
 *                  "{\"entity_id\":\"climate.xyz\",\"temperature\":21.5}"
 *
 * The implementation in main.cpp POSTs to:
 *   {ha_url}/api/services/{service_path}
 * with Bearer token authentication.                                        */
typedef void (*ha_call_fn_t)(const char *service_path, const char *json_body);
extern ha_call_fn_t g_ha_call;

/* ── MQTT debug ring-buffer ──────────────────────────────────────────────────
 * Stores the last MQTT_DBG_LINES received messages for the SYSTEM tab.
 * Written by mqtt_callback(), read by show_system_content() in screens.c. */
#define MQTT_DBG_LINES     4
#define MQTT_DBG_TOPIC_LEN 80
#define MQTT_DBG_VAL_LEN   24

typedef struct {
    char topic[MQTT_DBG_TOPIC_LEN]; /* Full MQTT topic of the message       */
    char value[MQTT_DBG_VAL_LEN];   /* Truncated payload (first 23 chars)   */
} mqtt_dbg_line_t;

extern mqtt_dbg_line_t g_mqtt_dbg[MQTT_DBG_LINES];
extern int             g_mqtt_dbg_count;          /* total messages received */
extern char            g_mqtt_base_topic_dbg[48]; /* copy of cfg base topic  */

#ifdef __cplusplus
}
#endif
