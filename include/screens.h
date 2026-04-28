/*
 * screens.h – HA Dashboard, ESP32-8048S070C
 * Pojmenované UI objekty pro live update + prototypy
 */
#ifndef SCREENS_H
#define SCREENS_H

#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl.h"

/* ── Pojmenovaná identita každé obrazovky ──────────────────────────── */
enum ScreensEnum {
    SCREEN_ID_MAIN = 1,
};

/* ── UI objekty s globálním přístupem (pro tick / live update) ─────── */
typedef struct _objects_t {
    /* Hlavní screen */
    lv_obj_t *main;

    /* Header */
    lv_obj_t *lbl_cas;
    lv_obj_t *lbl_datum;
    lv_obj_t *lbl_mqtt_status;
    lv_obj_t *lbl_wifi_status;

    /* Karta VENKU */
    lv_obj_t *lbl_venku_temp;
    lv_obj_t *lbl_venku_vlhk;
    lv_obj_t *pb_venku_vlhk;

    /* Karta OBÝVÁK */
    lv_obj_t *lbl_obyvak_temp;
    lv_obj_t *lbl_obyvak_vlhk;
    lv_obj_t *pb_obyvak_vlhk;

    /* Karta POČASÍ (z HA weather entity) */
    lv_obj_t *lbl_weather_temp;
    lv_obj_t *lbl_weather_state;
    lv_obj_t *icon_weather;
    lv_obj_t *lbl_weather_min;
    lv_obj_t *lbl_weather_max;
    lv_obj_t *lbl_weather_humidity;
    lv_obj_t *lbl_weather_wind;
    lv_obj_t *lbl_weather_pressure;

    /* Karta SVĚTLA na HOME (3 řádky: Obývák, Kuchyň, Počítač) */
    lv_obj_t *lbl_light_obyvak;
    lv_obj_t *lbl_light_kuchyn;
    lv_obj_t *lbl_light_pocitac;

    /* Karta SYSTÉM (legacy, momentálně se na HOME nezobrazuje) */
    lv_obj_t *lbl_sys_cpu;
    lv_obj_t *lbl_sys_ram;
    lv_obj_t *lbl_sys_stav;

    /* KLIMATE — 5 teplotních karet + thermostat current temp */
    lv_obj_t *lbl_kli_venku;
    lv_obj_t *lbl_kli_pracovna;
    lv_obj_t *lbl_kli_obyvak;
    lv_obj_t *lbl_kli_bojler;
    lv_obj_t *lbl_kli_kotel;
    lv_obj_t *lbl_thermostat_current;

    /* Nav buttons */
    lv_obj_t *btn_home;
    lv_obj_t *btn_klimate;
    lv_obj_t *btn_lights;
    lv_obj_t *btn_energy;
    lv_obj_t *btn_system;
} objects_t;

extern objects_t objects;
extern lv_obj_t *tick_value_change_obj;

/* ── Funkce ─────────────────────────────────────────────────────────── */
void create_screens(void);
void create_screen_main(void);
void tick_screen_main(void);
void tick_screen(int screen_index);
void tick_screen_by_id(enum ScreensEnum screenId);

void draw_weather_icon(lv_obj_t *cont, const char *state);

#ifdef __cplusplus
}
#endif

#endif /* SCREENS_H */