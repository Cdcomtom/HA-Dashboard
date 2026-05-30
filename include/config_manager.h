#pragma once
/*
 * config_manager.h
 * Správa konfigurace HA Dashboard přes WiFiManager + webový portál
 * Uložení do NVS (Preferences).
 *
 * Použití:
 *   C++ soubory:  #include "config_manager.h"   → plný přístup
 *   C   soubory:  #include "cfg_iface.h"         → jen getter funkce
 */

#ifdef __cplusplus
#include <Arduino.h>

#define CFG_MAX_LIGHTS   6
#define CFG_MAX_ENERGY   6

/* ── Konfigurační struktura ─────────────────────────────────────────────── */
struct AppConfig {
    /* MQTT */
    char     mqtt_server[64];
    uint16_t mqtt_port;
    char     mqtt_user[48];
    char     mqtt_pass[48];
    char     mqtt_client_id[32];
    char     mqtt_base_topic[48];   /* např. "homeassistant" */

    /* HOME senzory */
    char entity_venku_temp[80];    /* sensor.* */
    char entity_obyvak_temp[80];   /* sensor.* */
    char entity_weather[80];       /* weather.* */

    /* KLIMATE */
    char entity_kli_pracovna[80];
    char entity_kli_bojler[80];
    char entity_kli_kotel[80];
    char entity_thermostat[80];    /* climate.* */

    /* SVĚTLA – entity ID (light.* nebo switch.*) a zobrazovaný název */
    char light_entity[CFG_MAX_LIGHTS][80];
    char light_name[CFG_MAX_LIGHTS][32];

    /* ENERGY – připraveno */
    char energy_entity[CFG_MAX_ENERGY][80];
    char energy_name[CFG_MAX_ENERGY][32];
    char energy_unit[CFG_MAX_ENERGY][8];   /* "W", "kWh", "€"… */

    /* HA REST API – pro ovládání entit */
    char ha_url[80];          /* "http://192.168.1.1:8123" */
    char ha_token[256];       /* Long-Lived Access Token   */

    /* Open-Meteo – předpověď počasí bez API klíče */
    char weather_lat[12];     /* "50.0755" */
    char weather_lon[12];     /* "14.4378" */

    uint8_t screensaver_timeout; /* 0=vypnuto, jinak minuty nečinnosti (1,5,10…) */

    bool configured;   /* true = byl alespoň jednou nastaven přes portál */
};

extern AppConfig g_cfg;

/* ── C++ API ────────────────────────────────────────────────────────────── */
void   config_manager_init();     /* volej jako první v setup() – dělá WiFi  */
void   config_manager_save();
void   config_manager_load();
void   config_manager_handle();   /* volej v loop() – obsluhuje web server   */

/* Sestaví MQTT topic z entity ID (dot-form, např. "sensor.venku_temp")
 *   cfg_topic("sensor.venku_temp")          → "homeassistant/sensor/venku_temp/state"
 *   cfg_topic_attr("weather.h", "forecast") → "homeassistant/weather/h/attributes/forecast"
 *   cfg_topic_set("light.obyvak")           → "homeassistant/light/obyvak/set"
 */
String cfg_topic(const char* entity_id);
String cfg_topic_attr(const char* entity_id, const char* attr);
String cfg_topic_set(const char* entity_id);

bool   config_portal_active();
void   config_manager_handle();  /* volej v loop() pro webový portál */

extern "C" {
#endif  /* __cplusplus */

/* ── C-kompatibilní API (screens.c) ────────────────────────────────────── */
#ifndef CFG_MAX_LIGHTS
#  define CFG_MAX_LIGHTS 6
#endif
#ifndef CFG_MAX_ENERGY
#  define CFG_MAX_ENERGY 6
#endif

const char* cfg_get_light_entity(int idx);    /* "light.obyvak"            */
const char* cfg_get_light_name(int idx);      /* "Obývák"                  */
const char* cfg_get_light_topic_set(int idx); /* "ha/light/obyvak/set"     */
const char* cfg_get_thermostat_entity(void);  /* "climate.xyz"             */
uint8_t     cfg_get_screensaver_timeout(void);/* 0=off, else minutes        */

/* ── Energy gettery ────────────────────────────────────────────────────── */
const char* cfg_get_energy_entity(int idx);   /* "sensor.solar_power" */
const char* cfg_get_energy_name(int idx);     /* "Solár"              */
const char* cfg_get_energy_unit(int idx);     /* "W", "kWh"           */

/* ── Systémové info (živá data pro SYSTEM tab) ─────────────────────────── */
const char* sys_get_wifi_ssid(void);   /* aktuální SSID nebo "—"       */
const char* sys_get_wifi_ip(void);     /* IP adresa nebo "—"           */
void        sys_get_heap(uint32_t *free_kb, uint32_t *total_kb);
void        sys_get_uptime(uint32_t *days, uint32_t *hours, uint32_t *mins);

#ifdef __cplusplus
}
#endif
