/*
 * entities.h — TVOJE OSOBNÍ verze.  V .gitignore, nesdílej!
 *
 * Topic format v HA mqtt_statestream:
 *     ha/<domain>/<object_id>/state          (state value)
 *     ha/<domain>/<object_id>/<attribute>    (atributy, pokud publish_attributes: true)
 *
 * Příklad pro entity_id "sensor.tempvenku_ds18b20_temperature":
 *     - domain = "sensor"
 *     - object_id = "tempvenku_ds18b20_temperature"
 *     - state topic = "ha/sensor/tempvenku_ds18b20_temperature/state"
 *
 * Proto v defines píšeme "domain/object_id" (s lomítkem).
 */
#pragma once

#define MQTT_BASE_TOPIC   "ha"
#define HA_TOPIC(entity)  MQTT_BASE_TOPIC "/" entity "/state"
#define HA_TOPIC_ATTR(entity, attr)  MQTT_BASE_TOPIC "/" entity "/" attr

/* HOME — VENKU */
#define HA_ENTITY_VENKU_TEMP      "sensor/tempvenku_ds18b20_temperature"

/* HOME — OBÝVÁK */
#define HA_ENTITY_OBYVAK_TEMP     "sensor/nspanel_termostat_temperature"

/* HOME — POČASÍ */
#define HA_ENTITY_WEATHER         "weather/forecast_domov"

/* KLIMATE */
#define HA_ENTITY_KLI_VENKU       "sensor/tempvenku_ds18b20_temperature"
#define HA_ENTITY_KLI_PRACOVNA    "sensor/termostat_tasmota_am2301_temperature"
#define HA_ENTITY_KLI_OBYVAK      "sensor/nspanel_termostat_temperature"
/* #define HA_ENTITY_KLI_BOJLER   "sensor/bojler_temp"   // až budeš mít */
/* #define HA_ENTITY_KLI_KOTEL    "sensor/kotel_temp"    // až budeš mít */

/* Termostat */
#define HA_ENTITY_THERMOSTAT      "climate/nspanel_termostat_thermostat"

/* LIGHTS */
#define HA_ENTITY_LIGHT_OBYVAK    "light/obyvak"
#define HA_ENTITY_LIGHT_KUCHYN    "light/bulb_rgbcw_19a5f2"
#define HA_ENTITY_LIGHT_POCITAC   "light/ambientni_osvetleni_wifi_2"
#define HA_ENTITY_LIGHT_KOUPELNA  "light/led_garaz"
#define HA_ENTITY_LIGHT_PREDSIN   "switch/sonoff_1000e1a0f1"
#define HA_ENTITY_LIGHT_ZAHRADA   "switch/sonoff_1000e1a18c"

/* ENERGY — po domluvě doplníš */
