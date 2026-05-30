/*
 * screens.c – HA Dashboard, ESP32-8048S070C, 800×480
 * LVGL 9.x | dark theme | tap navigation
 *
 * Layout:
 *   [0..49]   Header   – DOMA, datum, hodiny, MQTT/WiFi pily
 *   [55..420] Content  – swappable area (HOME / KLIMATE / LIGHTS / ENERGY / SYSTEM)
 *   [430..480] Nav     – HOME / KLIMATE / LIGHTS / ENERGY / SYSTEM tap buttons
 */

#include "screens.h"
#include "fonts_cz.h"


#include "dashboard_data.h"
#include "cfg_iface.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#ifdef __has_include
#  if __has_include("images.h")
#    include "images.h"
#  endif
#endif

/* Forward declarations – static funkce volané před definicí */
static void publish_thermostat_target(void);
static void publish_thermostat_mode(int mode_idx);
static void screensaver_hide(void);
static void screensaver_show(lv_obj_t *parent);

objects_t objects;
lv_obj_t *tick_value_change_obj;

/* state pro nav switching */
static lv_obj_t *g_content_container = NULL;
static lv_obj_t *g_nav_buttons[5] = {0};
static int g_current_screen = 0;  /* 0=HOME 1=KLIMATE 2=LIGHTS 3=ENERGY 4=SYSTEM */
static uint32_t g_last_activity_tick = 0;
#define AUTO_HOME_TIMEOUT_MS  30000  /* 30 s na non-home → zpět HOME */

/* ── Screensaver ─────────────────────────────────────────────────────────── */
static lv_obj_t *g_screensaver_overlay = NULL;
static bool      g_screensaver_active  = false;

static lv_obj_t *g_ss_lbl_time  = NULL;
static lv_obj_t *g_ss_lbl_date  = NULL;
static lv_obj_t *g_ss_lbl_temp  = NULL;
static lv_obj_t *g_ss_icon      = NULL;

static void screensaver_hide(void) {
    if (!g_screensaver_active) return;
    if (g_screensaver_overlay) {
        lv_obj_del(g_screensaver_overlay);
        g_screensaver_overlay = NULL;
        g_ss_lbl_time = NULL;
        g_ss_lbl_date = NULL;
        g_ss_lbl_temp = NULL;
        g_ss_icon     = NULL;
    }
    g_screensaver_active = false;
    g_last_activity_tick = lv_tick_get();
}

static void screensaver_tap_cb(lv_event_t *e) {
    (void)e;
    screensaver_hide();
}

static void screensaver_show(lv_obj_t *parent) {
    if (g_screensaver_active) return;
    g_screensaver_active = true;

    /* Tmavý overlay přes celou obrazovku */
    g_screensaver_overlay = lv_obj_create(parent);
    lv_obj_set_pos(g_screensaver_overlay, 0, 0);
    lv_obj_set_size(g_screensaver_overlay, 800, 480);
    lv_obj_set_style_bg_color(g_screensaver_overlay, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(g_screensaver_overlay, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(g_screensaver_overlay, 0, LV_PART_MAIN);
    lv_obj_clear_flag(g_screensaver_overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(g_screensaver_overlay, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(g_screensaver_overlay, screensaver_tap_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_move_foreground(g_screensaver_overlay);

    /* Hodiny – velké, uprostřed nahoře */
    /* Hodiny – montserrat_28_cz zvětšený přes font_size override na 48px */
    g_ss_lbl_time = lv_label_create(g_screensaver_overlay);
    lv_obj_set_style_text_font(g_ss_lbl_time, &lv_font_montserrat_48, LV_PART_MAIN);
    lv_obj_set_style_text_color(g_ss_lbl_time, lv_color_hex(0xe0f0ff), LV_PART_MAIN);
    lv_obj_set_style_text_letter_space(g_ss_lbl_time, 10, LV_PART_MAIN);
    lv_obj_set_size(g_ss_lbl_time, 400, 80);
    lv_obj_set_style_text_align(g_ss_lbl_time, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_label_set_text(g_ss_lbl_time, "00:00");
    lv_obj_align(g_ss_lbl_time, LV_ALIGN_CENTER, 0, -80);

    /* Datum */
    g_ss_lbl_date = lv_label_create(g_screensaver_overlay);
    lv_obj_set_style_text_font(g_ss_lbl_date, &montserrat_20_cz, LV_PART_MAIN);
    lv_obj_set_style_text_color(g_ss_lbl_date, lv_color_hex(0x4a7aaa), LV_PART_MAIN);
    lv_label_set_text(g_ss_lbl_date, "Po 1.1.");
    lv_obj_align(g_ss_lbl_date, LV_ALIGN_CENTER, 0, 20);

    /* Ikona počasí – větší container */
    g_ss_icon = lv_obj_create(g_screensaver_overlay);
    lv_obj_set_size(g_ss_icon, 80, 80);
    lv_obj_set_style_bg_opa(g_ss_icon, 0, LV_PART_MAIN);
    lv_obj_set_style_border_width(g_ss_icon, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(g_ss_icon, 0, LV_PART_MAIN);
    lv_obj_align(g_ss_icon, LV_ALIGN_CENTER, -50, 90);

    /* Teplota počasí */
    g_ss_lbl_temp = lv_label_create(g_screensaver_overlay);
    lv_obj_set_style_text_font(g_ss_lbl_temp, &montserrat_28_cz, LV_PART_MAIN);
    lv_obj_set_style_text_color(g_ss_lbl_temp, lv_color_hex(0x00c8b4), LV_PART_MAIN);
    lv_label_set_text(g_ss_lbl_temp, "-- \xc2\xb0\x43");
    lv_obj_align(g_ss_lbl_temp, LV_ALIGN_CENTER, 40, 100);
}

/* Static refs pro live update na non-HOME obrazovkách
 * (HOME používá objects.lbl_*; LIGHTS/KLIMATE drží refy zde, protože widgety
 *  se znovu vytvářejí při každém přepnutí screenu)
 */
typedef struct { lv_obj_t *pill; lv_obj_t *dot; lv_obj_t *lbl; bool last_state; } light_pill_t;
static light_pill_t s_lights_pills[6] = {0};   /* LIGHTS detail page (6 pillů) */

/* HOME – pointery na light dots a state labels (deklarovány tady kvůli switch_to_screen) */
static lv_obj_t *s_home_ldot[6] = {NULL,NULL,NULL,NULL,NULL,NULL};
static lv_obj_t *s_home_lst[6]  = {NULL,NULL,NULL,NULL,NULL,NULL};

typedef struct {
    lv_obj_t *lbl_day;
    lv_obj_t *icon;       /* container 40x40 */
    lv_obj_t *lbl_max;
    lv_obj_t *lbl_min;
    char last_cond[24];
} forecast_widget_t;
static forecast_widget_t s_forecast_widgets[5] = {0};   /* PŘEDPOVĚĎ na HOME */


/* ── Barvy ─────────────────────────────────────────────────────────────────── */
#define C_BG_SCREEN     0x050d1a
#define C_BG_HEADER     0x0a1628
#define C_BG_CARD       0x0d1f38
#define C_BORDER        0x1a3a6a
#define C_ACCENT_BLUE   0x4a9eff
#define C_ACCENT_TEAL   0x00c8b4
#define C_ACCENT_ORANGE 0xffaa00
#define C_ACCENT_GREEN  0x22d46a
#define C_ACCENT_PURPLE 0xa060ff
#define C_TEXT_PRIMARY  0xe0f0ff
#define C_TEXT_DIM      0x4a7aaa
#define C_TEXT_ON       0x22d46a
#define C_TEXT_OFF      0x4a7aaa
#define C_PB_TRACK      0x051020
#define C_ONLINE        0x00ff88

/* ── Helpery: karta, title, progress bar ──────────────────────────────────── */
static lv_obj_t *make_card(lv_obj_t *parent, int x, int y, int w, int h, uint32_t accent_color) {
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_pos(card, x, y);
    lv_obj_set_size(card, w, h);
    lv_obj_set_style_bg_color(card, lv_color_hex(C_BG_CARD), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(card, lv_color_hex(C_BORDER), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(card, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(card, 8, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *accent = lv_obj_create(card);
    lv_obj_set_pos(accent, 0, 0);
    lv_obj_set_size(accent, w, 3);
    lv_obj_set_style_bg_color(accent, lv_color_hex(accent_color), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(accent, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(accent, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_clear_flag(accent, LV_OBJ_FLAG_SCROLLABLE);
    return card;
}

static void card_title(lv_obj_t *card, const char *text, uint32_t color) {
    lv_obj_t *lbl = lv_label_create(card);
    lv_obj_set_pos(lbl, 10, 12);
    lv_obj_set_style_text_color(lbl, lv_color_hex(color), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(lbl, &montserrat_14_cz, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_label_set_text(lbl, text);
}

static lv_obj_t *make_pb(lv_obj_t *parent, int x, int y, int w, int fill_w, uint32_t fill_color) {
    lv_obj_t *track = lv_obj_create(parent);
    lv_obj_set_pos(track, x, y);
    lv_obj_set_size(track, w, 6);
    lv_obj_set_style_bg_color(track, lv_color_hex(C_PB_TRACK), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(track, 3, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(track, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(track, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_clear_flag(track, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *fill = lv_obj_create(track);
    lv_obj_set_pos(fill, 0, 0);
    lv_obj_set_size(fill, fill_w, 6);
    lv_obj_set_style_bg_color(fill, lv_color_hex(fill_color), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(fill, 3, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(fill, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(fill, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_clear_flag(fill, LV_OBJ_FLAG_SCROLLABLE);
    return fill;
}

/* ── Weather icon helpers ─────────────────────────────────────────────────── */
static lv_obj_t *icon_circle(lv_obj_t *parent, int x, int y, int d, uint32_t color) {
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, d, d);
    lv_obj_set_style_radius(o, LV_RADIUS_CIRCLE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(o, lv_color_hex(color), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(o, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(o, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(o, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    return o;
}

static lv_obj_t *icon_rect(lv_obj_t *parent, int x, int y, int w, int h, uint32_t color, int radius) {
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_radius(o, radius, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(o, lv_color_hex(color), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(o, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(o, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(o, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    return o;
}

#define IC_SUN        0xffd24a
#define IC_SUN_LIGHT  0xfff5b8
#define IC_CLOUD      0xc0d8f0
#define IC_CLOUD_HI   0xffffff
#define IC_DARK       0x6a7a90
#define IC_DARK_HI    0x96a4b8
#define IC_RAIN       0x4ab8ff
#define IC_SNOW       0xffffff
#define IC_BOLT       0xffd24a
#define IC_FOG        0xc0d8f0

/* Mini weather icon (40×40) — pro forecast card */
static void draw_weather_icon_mini(lv_obj_t *parent, int x, int y, const char *state) {
    lv_obj_t *cont = lv_obj_create(parent);
    lv_obj_set_pos(cont, x, y);
    lv_obj_set_size(cont, 40, 40);
    lv_obj_set_style_bg_opa(cont, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(cont, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(cont, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_clear_flag(cont, LV_OBJ_FLAG_SCROLLABLE);
    if (!state) state = "cloudy";

    if (strcmp(state, "sunny") == 0 || strcmp(state, "clear") == 0 || strcmp(state, "clear-night") == 0) {
        icon_rect(cont, 18, 0, 4, 5, IC_SUN, 2);
        icon_rect(cont, 18, 35, 4, 5, IC_SUN, 2);
        icon_rect(cont, 0, 18, 5, 4, IC_SUN, 2);
        icon_rect(cont, 35, 18, 5, 4, IC_SUN, 2);
        icon_circle(cont, 11, 11, 18, IC_SUN);
        icon_circle(cont, 14, 14, 7, IC_SUN_LIGHT);
        return;
    }
    if (strcmp(state, "partlycloudy") == 0) {
        icon_circle(cont, 2, 2, 12, IC_SUN);
        icon_circle(cont, 4, 4, 6, IC_SUN_LIGHT);
        icon_circle(cont, 14, 18, 14, IC_CLOUD);
        icon_circle(cont, 22, 14, 16, IC_CLOUD);
        icon_circle(cont, 14, 22, 14, IC_CLOUD);
        icon_circle(cont, 22, 18, 8, IC_CLOUD_HI);
        return;
    }
    if (strcmp(state, "cloudy") == 0 || strcmp(state, "fog") == 0) {
        icon_circle(cont, 2, 14, 14, IC_CLOUD);
        icon_circle(cont, 10, 6, 20, IC_CLOUD);
        icon_circle(cont, 22, 12, 16, IC_CLOUD);
        icon_circle(cont, 24, 18, 14, IC_CLOUD);
        icon_circle(cont, 14, 6, 8, IC_CLOUD_HI);
        return;
    }
    if (strcmp(state, "rainy") == 0 || strcmp(state, "pouring") == 0) {
        icon_circle(cont, 2, 4, 14, IC_DARK);
        icon_circle(cont, 10, 0, 18, IC_DARK);
        icon_circle(cont, 22, 4, 14, IC_DARK);
        icon_circle(cont, 14, 8, 14, IC_DARK);
        icon_rect(cont, 8, 26, 3, 10, IC_RAIN, 2);
        icon_rect(cont, 18, 28, 3, 10, IC_RAIN, 2);
        icon_rect(cont, 28, 26, 3, 10, IC_RAIN, 2);
        return;
    }
    if (strcmp(state, "snowy") == 0 || strcmp(state, "snowy-rainy") == 0 || strcmp(state, "hail") == 0) {
        icon_circle(cont, 2, 4, 14, IC_CLOUD);
        icon_circle(cont, 10, 0, 18, IC_CLOUD);
        icon_circle(cont, 22, 4, 14, IC_CLOUD);
        icon_circle(cont, 14, 8, 14, IC_CLOUD);
        icon_circle(cont, 8, 30, 5, IC_SNOW);
        icon_circle(cont, 18, 32, 5, IC_SNOW);
        icon_circle(cont, 28, 30, 5, IC_SNOW);
        return;
    }
    if (strcmp(state, "lightning") == 0 || strcmp(state, "lightning-rainy") == 0) {
        icon_circle(cont, 2, 4, 14, IC_DARK);
        icon_circle(cont, 10, 0, 18, IC_DARK);
        icon_circle(cont, 22, 4, 14, IC_DARK);
        icon_circle(cont, 14, 8, 14, IC_DARK);
        icon_rect(cont, 18, 20, 4, 10, IC_BOLT, 1);
        icon_rect(cont, 14, 24, 12, 3, IC_BOLT, 1);
        icon_rect(cont, 18, 27, 4, 12, IC_BOLT, 1);
        return;
    }
    /* fallback - cloudy */
    icon_circle(cont, 10, 6, 20, IC_CLOUD);
    icon_circle(cont, 22, 12, 16, IC_CLOUD);
}

void draw_weather_icon(lv_obj_t *cont, const char *state) {
    lv_obj_clean(cont);
    if (!state) state = "cloudy";

    if (strcmp(state, "sunny") == 0 || strcmp(state, "clear") == 0 || strcmp(state, "clear-night") == 0) {
        icon_rect(cont, 28,  2, 4, 8, IC_SUN, 2);
        icon_rect(cont, 28, 50, 4, 8, IC_SUN, 2);
        icon_rect(cont,  2, 28, 8, 4, IC_SUN, 2);
        icon_rect(cont, 50, 28, 8, 4, IC_SUN, 2);
        icon_rect(cont, 10,  9, 5, 5, IC_SUN, 2);
        icon_rect(cont, 45,  9, 5, 5, IC_SUN, 2);
        icon_rect(cont, 10, 46, 5, 5, IC_SUN, 2);
        icon_rect(cont, 45, 46, 5, 5, IC_SUN, 2);
        icon_circle(cont, 17, 17, 26, IC_SUN);
        icon_circle(cont, 21, 21, 10, IC_SUN_LIGHT);
        return;
    }
    if (strcmp(state, "partlycloudy") == 0) {
        icon_rect(cont, 13,  0, 4, 5, IC_SUN, 2);
        icon_rect(cont,  0, 13, 5, 4, IC_SUN, 2);
        icon_rect(cont, 22,  6, 4, 4, IC_SUN, 2);
        icon_rect(cont,  6, 22, 4, 4, IC_SUN, 2);
        icon_circle(cont, 4, 4, 18, IC_SUN);
        icon_circle(cont, 7, 7, 8, IC_SUN_LIGHT);
        icon_circle(cont, 18, 28, 18, IC_CLOUD);
        icon_circle(cont, 30, 22, 24, IC_CLOUD);
        icon_circle(cont, 36, 32, 20, IC_CLOUD);
        icon_circle(cont, 22, 36, 18, IC_CLOUD);
        icon_circle(cont, 32, 24, 10, IC_CLOUD_HI);
        return;
    }
    if (strcmp(state, "cloudy") == 0 || strcmp(state, "fog") == 0) {
        icon_rect(cont,  6, 40, 48, 8, IC_DARK_HI, 4);
        icon_circle(cont,  4, 22, 22, IC_CLOUD);
        icon_circle(cont, 16, 14, 28, IC_CLOUD);
        icon_circle(cont, 32, 18, 24, IC_CLOUD);
        icon_circle(cont, 38, 28, 20, IC_CLOUD);
        icon_circle(cont, 12, 30, 20, IC_CLOUD);
        icon_circle(cont, 22, 14, 10, IC_CLOUD_HI);
        icon_circle(cont, 38, 22, 8, IC_CLOUD_HI);
        return;
    }
    if (strcmp(state, "rainy") == 0 || strcmp(state, "pouring") == 0) {
        icon_circle(cont,  4,  6, 22, IC_DARK);
        icon_circle(cont, 18,  2, 26, IC_DARK);
        icon_circle(cont, 36,  6, 20, IC_DARK);
        icon_circle(cont, 12, 14, 24, IC_DARK);
        icon_circle(cont, 32, 14, 22, IC_DARK);
        icon_circle(cont, 22,  6, 12, IC_DARK_HI);
        icon_rect(cont, 12, 38, 4, 12, IC_RAIN, 2);
        icon_rect(cont, 28, 36, 4, 14, IC_RAIN, 2);
        icon_rect(cont, 44, 38, 4, 12, IC_RAIN, 2);
        icon_circle(cont, 11, 47, 6, IC_RAIN);
        icon_circle(cont, 27, 45, 6, IC_RAIN);
        icon_circle(cont, 43, 47, 6, IC_RAIN);
        return;
    }
    if (strcmp(state, "snowy") == 0 || strcmp(state, "snowy-rainy") == 0 || strcmp(state, "hail") == 0) {
        icon_circle(cont,  4,  6, 22, IC_CLOUD);
        icon_circle(cont, 18,  2, 26, IC_CLOUD);
        icon_circle(cont, 36,  6, 20, IC_CLOUD);
        icon_circle(cont, 12, 14, 24, IC_CLOUD);
        icon_circle(cont, 32, 14, 22, IC_CLOUD);
        icon_circle(cont, 22,  6, 12, IC_CLOUD_HI);
        for (int i = 0; i < 3; i++) {
            int cx = 12 + i * 18;
            int cy = (i == 1) ? 50 : 46;
            icon_rect(cont, cx - 1, cy - 4, 2, 10, IC_SNOW, 1);
            icon_rect(cont, cx - 4, cy - 1, 10, 2, IC_SNOW, 1);
            icon_circle(cont, cx - 2, cy - 2, 4, IC_SNOW);
        }
        return;
    }
    if (strcmp(state, "lightning") == 0 || strcmp(state, "lightning-rainy") == 0) {
        icon_circle(cont,  4,  6, 22, IC_DARK);
        icon_circle(cont, 18,  2, 26, IC_DARK);
        icon_circle(cont, 36,  6, 20, IC_DARK);
        icon_circle(cont, 12, 14, 24, IC_DARK);
        icon_circle(cont, 32, 14, 22, IC_DARK);
        icon_rect(cont, 30, 26, 6, 7, IC_BOLT, 1);
        icon_rect(cont, 24, 32, 6, 7, IC_BOLT, 1);
        icon_rect(cont, 30, 38, 6, 7, IC_BOLT, 1);
        icon_rect(cont, 24, 44, 6, 7, IC_BOLT, 1);
        icon_rect(cont, 24, 32, 12, 3, IC_BOLT, 1);
        icon_rect(cont, 24, 38, 12, 3, IC_BOLT, 1);
        icon_rect(cont, 24, 44, 12, 3, IC_BOLT, 1);
        return;
    }
    /* fallback / windy */
    icon_circle(cont, 14, 14, 24, IC_CLOUD);
    icon_circle(cont, 30, 20, 22, IC_CLOUD);
    icon_rect(cont,  4, 42, 30, 3, IC_CLOUD, 1);
    icon_rect(cont, 12, 50, 24, 3, IC_CLOUD, 1);
}

/* ╔═══════════════════════════════════════════════════════════════════════════╗
   ║ NAV BUTTON + EVENT HANDLER                                                ║
   ╚═══════════════════════════════════════════════════════════════════════════╝ */

/* fwd decl */
static void show_home_content(void);
static void show_klimate_content(void);
static void show_lights_content(void);
static void show_energy_content(void);
static void show_system_content(void);

static void update_nav_highlight(int active_idx) {
    for (int i = 0; i < 5; i++) {
        lv_obj_t *btn = g_nav_buttons[i];
        if (!btn) continue;
        bool active = (i == active_idx);
        lv_obj_t *lbl = lv_obj_get_child(btn, 0);
        if (active) {
            lv_obj_set_style_bg_color(btn, lv_color_hex(0x0a3a6a), LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_border_color(btn, lv_color_hex(C_ACCENT_BLUE), LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_border_width(btn, 2, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_border_side(btn, LV_BORDER_SIDE_TOP, LV_PART_MAIN | LV_STATE_DEFAULT);
            if (lbl) lv_obj_set_style_text_color(lbl, lv_color_hex(C_ACCENT_BLUE), LV_PART_MAIN | LV_STATE_DEFAULT);
        } else {
            lv_obj_set_style_bg_color(btn, lv_color_hex(C_BG_HEADER), LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_border_width(btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            if (lbl) lv_obj_set_style_text_color(lbl, lv_color_hex(0x4a7aaa), LV_PART_MAIN | LV_STATE_DEFAULT);
        }
    }
}

static void switch_to_screen(int idx) {
    if (idx == g_current_screen) return;
    /* NULL out stale widget pointers BEFORE lv_obj_clean, aby timer nesahal na smazané objekty */
    if (g_current_screen == 0) {
        memset(s_home_ldot, 0, sizeof(s_home_ldot));
        memset(s_home_lst,  0, sizeof(s_home_lst));
    } else if (g_current_screen == 1) {
        objects.lbl_thermostat_current = NULL;
        objects.lbl_kli_venku = NULL; objects.lbl_kli_pracovna = NULL;
        objects.lbl_kli_obyvak = NULL; objects.lbl_kli_bojler = NULL;
        objects.lbl_kli_kotel = NULL;
    } else if (g_current_screen == 2) {
        memset(s_lights_pills, 0, sizeof(s_lights_pills));
    } else if (g_current_screen == 4) {
        memset(s_forecast_widgets, 0, sizeof(s_forecast_widgets));
    }
    g_current_screen = idx;
    g_last_activity_tick = lv_tick_get();  /* reset timer při přepnutí */
    if (g_content_container) lv_obj_clean(g_content_container);
    update_nav_highlight(idx);
    switch (idx) {
        case 0: show_home_content(); break;
        case 1: show_klimate_content(); break;
        case 2: show_lights_content(); break;
        case 3: show_energy_content(); break;
        case 4: show_system_content(); break;
    }
}

static void nav_btn_event_cb(lv_event_t *e) {
    int target = (int)(intptr_t)lv_event_get_user_data(e);
    switch_to_screen(target);
}

static lv_obj_t *make_nav_btn(lv_obj_t *parent, int x, int idx, const char *text, bool active) {
    lv_obj_t *btn = lv_button_create(parent);
    lv_obj_set_pos(btn, x, 430);
    lv_obj_set_size(btn, 160, 50);
    lv_obj_set_style_radius(btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(btn, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    if (active) {
        lv_obj_set_style_bg_color(btn, lv_color_hex(0x0a3a6a), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_border_color(btn, lv_color_hex(C_ACCENT_BLUE), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_border_width(btn, 2, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_border_side(btn, LV_BORDER_SIDE_TOP, LV_PART_MAIN | LV_STATE_DEFAULT);
    } else {
        lv_obj_set_style_bg_color(btn, lv_color_hex(C_BG_HEADER), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_border_width(btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    }
    lv_obj_t *lbl = lv_label_create(btn);
    lv_obj_set_style_align(lbl, LV_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(lbl, lv_color_hex(active ? C_ACCENT_BLUE : C_TEXT_DIM), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(lbl, &montserrat_14_cz, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_label_set_text(lbl, text);
    lv_obj_add_event_cb(btn, nav_btn_event_cb, LV_EVENT_CLICKED, (void*)(intptr_t)idx);
    return btn;
}

/* ╔═══════════════════════════════════════════════════════════════════════════╗
   ║ HEADER (DOMA, datum, hodiny, MQTT/WiFi pily)                              ║
   ╚═══════════════════════════════════════════════════════════════════════════╝ */
static void build_header(lv_obj_t *parent) {
    lv_obj_t *hdr = lv_obj_create(parent);
    lv_obj_set_pos(hdr, 0, 0);
    lv_obj_set_size(hdr, 800, 50);
    lv_obj_set_style_bg_color(hdr, lv_color_hex(C_BG_HEADER), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(hdr, lv_color_hex(C_BORDER), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(hdr, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(hdr, LV_BORDER_SIDE_BOTTOM, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(hdr, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(hdr, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_clear_flag(hdr, LV_OBJ_FLAG_SCROLLABLE);

    /* DOMA */
    lv_obj_t *logo = lv_label_create(parent);
    lv_obj_set_pos(logo, 16, 11);
    lv_obj_set_style_text_color(logo, lv_color_hex(C_ACCENT_BLUE), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(logo, &montserrat_20_cz, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_label_set_text(logo, "DOMA");

    /* Datum */
    lv_obj_t *lbl_datum = lv_label_create(parent);
    objects.lbl_datum = lbl_datum;
    lv_obj_set_pos(lbl_datum, 110, 18);
    lv_obj_set_style_text_color(lbl_datum, lv_color_hex(0x4a7aaa), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(lbl_datum, &montserrat_14_cz, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_label_set_text(lbl_datum, "...");

    /* Hodiny - 28px zlatá */
    lv_obj_t *lbl_cas = lv_label_create(parent);
    objects.lbl_cas = lbl_cas;
    lv_obj_set_pos(lbl_cas, 310, 4);
    lv_obj_set_size(lbl_cas, 180, 42);
    lv_obj_set_style_text_color(lbl_cas, lv_color_hex(0xfff5b8), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(lbl_cas, &montserrat_28_cz, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(lbl_cas, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_label_set_text(lbl_cas, "--:--");

    /* MQTT pill */
    lv_obj_t *pill_mqtt = lv_obj_create(parent);
    lv_obj_set_pos(pill_mqtt, 600, 12);
    lv_obj_set_size(pill_mqtt, 76, 26);
    lv_obj_set_style_radius(pill_mqtt, 13, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(pill_mqtt, lv_color_hex(0x0d3a2a), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(pill_mqtt, lv_color_hex(C_ONLINE), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(pill_mqtt, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(pill_mqtt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_clear_flag(pill_mqtt, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *dot_mqtt = lv_obj_create(pill_mqtt);
    lv_obj_set_pos(dot_mqtt, 8, 9);
    lv_obj_set_size(dot_mqtt, 8, 8);
    lv_obj_set_style_radius(dot_mqtt, LV_RADIUS_CIRCLE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(dot_mqtt, lv_color_hex(C_ONLINE), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(dot_mqtt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_clear_flag(dot_mqtt, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *lbl_mqtt = lv_label_create(pill_mqtt);
    objects.lbl_mqtt_status = lbl_mqtt;
    lv_obj_set_pos(lbl_mqtt, 22, 5);
    lv_obj_set_style_text_color(lbl_mqtt, lv_color_hex(C_ONLINE), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(lbl_mqtt, &montserrat_14_cz, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_label_set_text(lbl_mqtt, "MQTT");

    /* WiFi pill */
    lv_obj_t *pill_wifi = lv_obj_create(parent);
    lv_obj_set_pos(pill_wifi, 686, 12);
    lv_obj_set_size(pill_wifi, 100, 26);
    lv_obj_set_style_radius(pill_wifi, 13, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(pill_wifi, lv_color_hex(0x0d3a2a), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(pill_wifi, lv_color_hex(C_ONLINE), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(pill_wifi, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(pill_wifi, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_clear_flag(pill_wifi, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *dot_wifi = lv_obj_create(pill_wifi);
    lv_obj_set_pos(dot_wifi, 8, 9);
    lv_obj_set_size(dot_wifi, 8, 8);
    lv_obj_set_style_radius(dot_wifi, LV_RADIUS_CIRCLE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(dot_wifi, lv_color_hex(C_ONLINE), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(dot_wifi, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_clear_flag(dot_wifi, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *lbl_wifi = lv_label_create(pill_wifi);
    objects.lbl_wifi_status = lbl_wifi;
    lv_obj_set_pos(lbl_wifi, 22, 5);
    lv_obj_set_style_text_color(lbl_wifi, lv_color_hex(C_ONLINE), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(lbl_wifi, &montserrat_14_cz, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_label_set_text(lbl_wifi, "WiFi");
}

static void build_nav_bar(lv_obj_t *parent) {
    lv_obj_t *nav = lv_obj_create(parent);
    lv_obj_set_pos(nav, 0, 430);
    lv_obj_set_size(nav, 800, 50);
    lv_obj_set_style_bg_color(nav, lv_color_hex(C_BG_HEADER), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(nav, lv_color_hex(C_BORDER), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(nav, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(nav, LV_BORDER_SIDE_TOP, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(nav, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_clear_flag(nav, LV_OBJ_FLAG_SCROLLABLE);

    g_nav_buttons[0] = make_nav_btn(parent,   0, 0, "HOME",    true);
    g_nav_buttons[1] = make_nav_btn(parent, 160, 1, "KLIMATE", false);
    g_nav_buttons[2] = make_nav_btn(parent, 320, 2, "LIGHTS",  false);
    g_nav_buttons[3] = make_nav_btn(parent, 480, 3, "ENERGY",  false);
    g_nav_buttons[4] = make_nav_btn(parent, 640, 4, "SYSTEM",  false);
    objects.btn_home    = g_nav_buttons[0];
    objects.btn_klimate = g_nav_buttons[1];
    objects.btn_lights  = g_nav_buttons[2];
    objects.btn_energy  = g_nav_buttons[3];
    objects.btn_system  = g_nav_buttons[4];
}

/* ╔═══════════════════════════════════════════════════════════════════════════╗
   ║ HOME content (3 karty + 2 karty)                                          ║
   ╚═══════════════════════════════════════════════════════════════════════════╝ */
static void show_home_content(void) {
    lv_obj_t *parent = g_content_container;
    const int R1_Y = 5, R1_H = 185, R1_W = 250;
    const int PB_W = 220;

    /* VENKU */
    {
        lv_obj_t *c = make_card(parent, 10, R1_Y, R1_W, R1_H, C_ACCENT_BLUE);
        card_title(c, "VENKU", C_ACCENT_BLUE);
        lv_obj_t *lbl_t = lv_label_create(c);
        objects.lbl_venku_temp = lbl_t;
        lv_obj_set_pos(lbl_t, 10, 32);
        lv_obj_set_style_text_color(lbl_t, lv_color_hex(C_ACCENT_BLUE), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_font(lbl_t, &montserrat_20_cz, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_label_set_text(lbl_t, "18°C");
        lv_obj_t *lbl_v = lv_label_create(c);
        objects.lbl_venku_vlhk = lbl_v;
        lv_obj_set_pos(lbl_v, 10, 76);
        lv_obj_set_style_text_color(lbl_v, lv_color_hex(0x4a7aaa), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_font(lbl_v, &montserrat_14_cz, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_label_set_text(lbl_v, "Vlhkost: 62%");
        objects.pb_venku_vlhk = make_pb(c, 10, 110, PB_W, (PB_W * 62) / 100, C_ACCENT_BLUE);
        lv_obj_t *ico = lv_label_create(c);
        lv_obj_set_pos(ico, 180, 30);
        lv_obj_set_style_text_color(ico, lv_color_hex(0x5abeff), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_font(ico, &montserrat_20_cz, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_label_set_text(ico, ":)");
    }

    /* OBÝVÁK */
    {
        lv_obj_t *c = make_card(parent, 275, R1_Y, R1_W, R1_H, C_ACCENT_TEAL);
        card_title(c, "OBÝVÁK", C_ACCENT_TEAL);
        lv_obj_t *lbl_t = lv_label_create(c);
        objects.lbl_obyvak_temp = lbl_t;
        lv_obj_set_pos(lbl_t, 10, 32);
        lv_obj_set_style_text_color(lbl_t, lv_color_hex(0x00c8b4), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_font(lbl_t, &montserrat_20_cz, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_label_set_text(lbl_t, "22°C");
        lv_obj_t *lbl_v = lv_label_create(c);
        objects.lbl_obyvak_vlhk = lbl_v;
        lv_obj_set_pos(lbl_v, 10, 76);
        lv_obj_set_style_text_color(lbl_v, lv_color_hex(0x4a7aaa), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_font(lbl_v, &montserrat_14_cz, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_label_set_text(lbl_v, "Vlhkost: 45%");
        objects.pb_obyvak_vlhk = make_pb(c, 10, 110, PB_W, (PB_W * 45) / 100, C_ACCENT_TEAL);
        lv_obj_t *tag = lv_label_create(c);
        lv_obj_set_pos(tag, 160, 76);
        lv_obj_set_style_text_color(tag, lv_color_hex(0x4a7aaa), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_font(tag, &montserrat_14_cz, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_label_set_text(tag, "uvnitř");
    }

    /* POČASÍ */
    {
        lv_obj_t *c = make_card(parent, 540, R1_Y, R1_W, R1_H, C_ACCENT_ORANGE);
        card_title(c, "POČASÍ", C_ACCENT_ORANGE);
        lv_obj_t *lbl_temp = lv_label_create(c);
        objects.lbl_weather_temp = lbl_temp;
        lv_obj_set_pos(lbl_temp, 10, 28);
        lv_obj_set_style_text_color(lbl_temp, lv_color_hex(C_ACCENT_ORANGE), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_font(lbl_temp, &montserrat_20_cz, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_label_set_text(lbl_temp, "18°C");
        lv_obj_t *lbl_state = lv_label_create(c);
        objects.lbl_weather_state = lbl_state;
        lv_obj_set_pos(lbl_state, 10, 60);
        lv_obj_set_style_text_color(lbl_state, lv_color_hex(0xe0f0ff), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_font(lbl_state, &montserrat_14_cz, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_label_set_text(lbl_state, "Slunečno");
        lv_obj_t *icon = lv_obj_create(c);
        objects.icon_weather = icon;
        lv_obj_set_pos(icon, 175, 22);
        lv_obj_set_size(icon, 60, 60);
        lv_obj_set_style_bg_opa(icon, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_border_width(icon, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_pad_all(icon, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_clear_flag(icon, LV_OBJ_FLAG_SCROLLABLE);
        draw_weather_icon(icon, "sunny");
        const int META_Y0 = 92, META_DY = 20, COL_L = 10, COL_R = 130;
        lv_obj_t *lbl_min = lv_label_create(c);
        objects.lbl_weather_min = lbl_min;
        lv_obj_set_pos(lbl_min, COL_L, META_Y0);
        lv_obj_set_style_text_color(lbl_min, lv_color_hex(0x4a7aaa), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_font(lbl_min, &montserrat_14_cz, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_label_set_text(lbl_min, "min: 12°");
        lv_obj_t *lbl_max = lv_label_create(c);
        objects.lbl_weather_max = lbl_max;
        lv_obj_set_pos(lbl_max, COL_L, META_Y0 + META_DY);
        lv_obj_set_style_text_color(lbl_max, lv_color_hex(0x4a7aaa), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_font(lbl_max, &montserrat_14_cz, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_label_set_text(lbl_max, "max: 24°");
        lv_obj_t *lbl_press = lv_label_create(c);
        objects.lbl_weather_pressure = lbl_press;
        lv_obj_set_pos(lbl_press, COL_L, META_Y0 + 2 * META_DY);
        lv_obj_set_style_text_color(lbl_press, lv_color_hex(0x4a7aaa), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_font(lbl_press, &montserrat_14_cz, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_label_set_text(lbl_press, "1013 hPa");
        lv_obj_t *lbl_hum = lv_label_create(c);
        objects.lbl_weather_humidity = lbl_hum;
        lv_obj_set_pos(lbl_hum, COL_R, META_Y0);
        lv_obj_set_style_text_color(lbl_hum, lv_color_hex(0x4a7aaa), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_font(lbl_hum, &montserrat_14_cz, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_label_set_text(lbl_hum, "vlh: 65%");
        lv_obj_t *lbl_wind = lv_label_create(c);
        objects.lbl_weather_wind = lbl_wind;
        lv_obj_set_pos(lbl_wind, COL_R, META_Y0 + META_DY);
        lv_obj_set_style_text_color(lbl_wind, lv_color_hex(0x4a7aaa), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_font(lbl_wind, &montserrat_14_cz, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_label_set_text(lbl_wind, "vítr: 5 km/h");
    }

    /* SVĚTLA + SYSTÉM (řada 2) */
    const int R2_Y = 198, R2_H = 160, R2_W = 380;

    {
        lv_obj_t *c = make_card(parent, 10, R2_Y, R2_W, R2_H, C_ACCENT_GREEN);
        card_title(c, "SVĚTLA", C_ACCENT_GREEN);

        /* 6 dlaždic 3×2: každá 118×52, mezery 5px, levý okraj 8px */
        for (int i = 0; i < CFG_MAX_LIGHTS; i++) {
            int col = i % 3, row = i / 3;
            int tx  = 8  + col * 123;
            int ty  = 36 + row * 59;

            lv_obj_t *tile = lv_obj_create(c);
            lv_obj_set_pos(tile, tx, ty);
            lv_obj_set_size(tile, 118, 52);
            lv_obj_set_style_bg_color(tile,     lv_color_hex(0x07111e), 0);
            lv_obj_set_style_border_color(tile, lv_color_hex(0x1a3a6a), 0);
            lv_obj_set_style_border_width(tile, 1, 0);
            lv_obj_set_style_radius(tile, 6, 0);
            lv_obj_clear_flag(tile, LV_OBJ_FLAG_SCROLLABLE);

            /* Barevná tečka – indikátor stavu */
            lv_obj_t *dot = lv_obj_create(tile);
            lv_obj_set_pos(dot, 8, 10);
            lv_obj_set_size(dot, 14, 14);
            lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
            lv_obj_set_style_bg_color(dot, lv_color_hex(0x3a2a2a), 0);
            lv_obj_set_style_border_width(dot, 0, 0);
            lv_obj_clear_flag(dot, LV_OBJ_FLAG_SCROLLABLE);
            s_home_ldot[i] = dot;

            /* Název světla */
            lv_obj_t *lname = lv_label_create(tile);
            lv_obj_set_pos(lname, 28, 8);
            lv_obj_set_style_text_font(lname,  &montserrat_14_cz, 0);
            lv_obj_set_style_text_color(lname, lv_color_hex(0xe0f0ff), 0);
            const char *nm = cfg_get_light_name(i);
            lv_label_set_text(lname, (nm && nm[0]) ? nm : "Světlo");

            /* Stav ON/OFF */
            lv_obj_t *lst = lv_label_create(tile);
            lv_obj_set_pos(lst, 28, 26);
            lv_obj_set_style_text_font(lst,  &montserrat_14_cz, 0);
            lv_obj_set_style_text_color(lst, lv_color_hex(C_TEXT_OFF), 0);
            lv_label_set_text(lst, "OFF");
            s_home_lst[i] = lst;
        }
        /* Okamžitě nastavit aktuální stav světel (bez čekání na timer) */
        {
            const bool cur[6] = { g_light_obyvak, g_light_kuchyn, g_light_pocitac,
                                  g_light_koupelna, g_light_predsin, g_light_zahrada };
            for (int j = 0; j < 6; j++) {
                if (!s_home_ldot[j] || !s_home_lst[j]) continue;
                lv_obj_set_style_bg_color(s_home_ldot[j],
                    lv_color_hex(cur[j] ? C_ONLINE : 0x3a2a2a), 0);
                lv_label_set_text(s_home_lst[j], cur[j] ? "ON" : "OFF");
                lv_obj_set_style_text_color(s_home_lst[j],
                    lv_color_hex(cur[j] ? C_TEXT_ON : C_TEXT_OFF), 0);
            }
        }
    }

    /* Karta PŘEDPOVĚĎ — 5 dní z weather.forecast_domov */
    {
        lv_obj_t *c = make_card(parent, 405, R2_Y, R2_W, R2_H, C_ACCENT_BLUE);
        card_title(c, "PŘEDPOVĚĎ", C_ACCENT_BLUE);

        /* Mock data - reflektuje strukturu HA weather forecast atribut */
        struct { const char *day; const char *cond; const char *tmax; const char *tmin; } fc[] = {
            { "Po", "cloudy",       "16°", "4°"  },
            { "Út", "partlycloudy", "18°", "6°"  },
            { "St", "rainy",        "13°", "7°"  },
            { "Čt", "partlycloudy", "14°", "5°"  },
            { "Pá", "sunny",        "15°", "6°"  },
        };
        /* 5 sloupců: každý 72×140, gap 4 px - centrování v 380px kartě */
        for (int i = 0; i < 5; i++) {
            int col_x = 8 + i * 73;
            /* Den */
            lv_obj_t *d = lv_label_create(c);
            lv_obj_set_pos(d, col_x, 30);
            lv_obj_set_size(d, 72, 18);
            lv_obj_set_style_text_color(d, lv_color_hex(0x4a7aaa), LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_text_font(d, &montserrat_14_cz, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_text_align(d, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_label_set_text(d, fc[i].day);
            /* Ikona container - draw_weather_icon_mini ji vytvoří uvnitř c */
            lv_obj_t *icon_c = lv_obj_create(c);
            lv_obj_set_pos(icon_c, col_x + 16, 50);
            lv_obj_set_size(icon_c, 40, 40);
            lv_obj_set_style_bg_opa(icon_c, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_border_width(icon_c, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_pad_all(icon_c, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_clear_flag(icon_c, LV_OBJ_FLAG_SCROLLABLE);
            draw_weather_icon_mini(icon_c, 0, 0, fc[i].cond);
            /* Max teplota */
            lv_obj_t *mx = lv_label_create(c);
            lv_obj_set_pos(mx, col_x, 96);
            lv_obj_set_size(mx, 72, 18);
            lv_obj_set_style_text_color(mx, lv_color_hex(C_ACCENT_ORANGE), LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_text_font(mx, &montserrat_14_cz, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_text_align(mx, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_label_set_text(mx, fc[i].tmax);
            /* Min teplota */
            lv_obj_t *mn = lv_label_create(c);
            lv_obj_set_pos(mn, col_x, 118);
            lv_obj_set_size(mn, 72, 18);
            lv_obj_set_style_text_color(mn, lv_color_hex(C_ACCENT_BLUE), LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_text_font(mn, &montserrat_14_cz, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_text_align(mn, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_label_set_text(mn, fc[i].tmin);
            /* store refs pro live update */
            s_forecast_widgets[i].lbl_day = d;
            s_forecast_widgets[i].icon   = icon_c;
            s_forecast_widgets[i].lbl_max = mx;
            s_forecast_widgets[i].lbl_min = mn;
            s_forecast_widgets[i].last_cond[0] = 0;
        }
    }
}

/* ╔═══════════════════════════════════════════════════════════════════════════╗
   ║ Placeholder content – KLIMATE / LIGHTS / ENERGY / SYSTEM                  ║
   ╚═══════════════════════════════════════════════════════════════════════════╝ */
/* Helper: malá hodnota + popis pro grid metric */
static void mk_metric(lv_obj_t *parent, int x, int y, int w, int h, const char *label, const char *value, uint32_t accent) {
    lv_obj_t *c = make_card(parent, x, y, w, h, accent);
    lv_obj_t *l = lv_label_create(c);
    lv_obj_set_pos(l, 12, 14);
    lv_obj_set_style_text_color(l, lv_color_hex(0x4a7aaa), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(l, &montserrat_14_cz, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_label_set_text(l, label);
    lv_obj_t *v = lv_label_create(c);
    lv_obj_set_pos(v, 12, 38);
    lv_obj_set_style_text_color(v, lv_color_hex(accent), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(v, &montserrat_28_cz, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_label_set_text(v, value);
}

/* Termostat state - sdíleno mezi event handlerem a UI */
static float g_thermostat_target = 21.5f;
static lv_obj_t *g_thermostat_target_lbl = NULL;
static int g_thermostat_mode = 0; /* 0=Topení 1=Chlazení 2=Auto 3=Vyp */
static lv_obj_t *g_thermostat_mode_btns[4] = {0};

static void thermostat_pm_cb(lv_event_t *e) {
    int delta = (int)(intptr_t)lv_event_get_user_data(e);  /* +1 or -1 (× 0.5°C) */
    g_thermostat_target += delta * 0.5f;
    if (g_thermostat_target < 5.0f)  g_thermostat_target = 5.0f;
    if (g_thermostat_target > 30.0f) g_thermostat_target = 30.0f;
    if (g_thermostat_target_lbl) {
        char buf[16];
        snprintf(buf, sizeof(buf), "%.1f°C", g_thermostat_target);
        lv_label_set_text(g_thermostat_target_lbl, buf);
    }
    publish_thermostat_target();
}

static void thermostat_mode_cb(lv_event_t *e) {
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    g_thermostat_mode = idx;
    publish_thermostat_mode(idx);
    /* refresh barvy pillů */
    static const uint32_t mode_colors[] = { 0xff6644, 0x44aaff, 0xa060ff, 0x4a7aaa };
    for (int i = 0; i < 4; i++) {
        if (!g_thermostat_mode_btns[i]) continue;
        bool active = (i == g_thermostat_mode);
        lv_obj_set_style_bg_color(g_thermostat_mode_btns[i],
            lv_color_hex(active ? mode_colors[i] : C_BG_HEADER),
            LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_border_color(g_thermostat_mode_btns[i],
            lv_color_hex(active ? mode_colors[i] : C_BORDER),
            LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_t *lbl = lv_obj_get_child(g_thermostat_mode_btns[i], 0);
        if (lbl) lv_obj_set_style_text_color(lbl,
            lv_color_hex(active ? 0xffffff : C_TEXT_DIM),
            LV_PART_MAIN | LV_STATE_DEFAULT);
    }
}

/* KLIMATE: 5 teplot + termostat */
static void show_klimate_content(void) {
    lv_obj_t *p = g_content_container;

    /* Řada 1: 5 teplotních karet, každá 156×130, gap 5 */
    struct { const char *name; const char *temp; uint32_t color; } sensors[] = {
        { "VENKU",    "8.4°C",  C_ACCENT_BLUE   },
        { "PRACOVNA", "21.7°C", C_ACCENT_TEAL   },
        { "OBÝVÁK",   "22.4°C", C_ACCENT_TEAL   },
        { "BOJLER",   "55°C",   C_ACCENT_ORANGE },
        { "KOTEL",    "62°C",   0xff6644        },
    };
    /* Refs pro live update z timer cb */
    lv_obj_t **kli_label_slots[5] = {
        &objects.lbl_kli_venku, &objects.lbl_kli_pracovna, &objects.lbl_kli_obyvak,
        &objects.lbl_kli_bojler, &objects.lbl_kli_kotel
    };
    for (int i = 0; i < 5; i++) {
        int x = 5 + i * 161;  /* 156 + 5 gap */
        lv_obj_t *c = make_card(p, x, 5, 156, 130, sensors[i].color);
        card_title(c, sensors[i].name, sensors[i].color);
        lv_obj_t *t = lv_label_create(c);
        *(kli_label_slots[i]) = t;  /* store pro timer */
        lv_obj_set_pos(t, 0, 50);
        lv_obj_set_size(t, 156, 40);
        lv_obj_set_style_text_color(t, lv_color_hex(sensors[i].color), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_font(t, &montserrat_28_cz, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_align(t, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_label_set_text(t, sensors[i].temp);
    }

    /* Řada 2: termostat - velký panel 790×220, y=145 */
    lv_obj_t *th = make_card(p, 5, 145, 790, 220, C_ACCENT_GREEN);
    card_title(th, "TERMOSTAT - OBÝVÁK", C_ACCENT_GREEN);

    /* Vlevo: aktuální teplota label */
    lv_obj_t *cur_lbl = lv_label_create(th);
    lv_obj_set_pos(cur_lbl, 30, 50);
    lv_obj_set_style_text_color(cur_lbl, lv_color_hex(0x4a7aaa), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(cur_lbl, &montserrat_14_cz, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_label_set_text(cur_lbl, "Aktuální:");
    lv_obj_t *cur_val = lv_label_create(th);
    objects.lbl_thermostat_current = cur_val;
    lv_obj_set_pos(cur_val, 30, 75);
    lv_obj_set_style_text_color(cur_val, lv_color_hex(0xe0f0ff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(cur_val, &montserrat_28_cz, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_label_set_text(cur_val, "--°C");

    /* Střed: nastavená teplota - velká */
    lv_obj_t *set_lbl = lv_label_create(th);
    lv_obj_set_pos(set_lbl, 0, 50);
    lv_obj_set_size(set_lbl, 790, 24);
    lv_obj_set_style_text_color(set_lbl, lv_color_hex(0x4a7aaa), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(set_lbl, &montserrat_14_cz, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(set_lbl, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_label_set_text(set_lbl, "NASTAVENÁ TEPLOTA");

    lv_obj_t *target = lv_label_create(th);
    g_thermostat_target_lbl = target;
    lv_obj_set_pos(target, 0, 75);
    lv_obj_set_size(target, 790, 50);
    lv_obj_set_style_text_color(target, lv_color_hex(C_ACCENT_GREEN), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(target, &montserrat_28_cz, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(target, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    char buf[16];
    snprintf(buf, sizeof(buf), "%.1f°C", g_thermostat_target);
    lv_label_set_text(target, buf);

    /* − button vlevo od středu */
    lv_obj_t *btn_minus = lv_button_create(th);
    lv_obj_set_pos(btn_minus, 240, 70);
    lv_obj_set_size(btn_minus, 60, 60);
    lv_obj_set_style_radius(btn_minus, 30, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(btn_minus, lv_color_hex(0x2a3050), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(btn_minus, lv_color_hex(C_ACCENT_GREEN), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(btn_minus, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_t *lm = lv_label_create(btn_minus);
    lv_obj_set_style_align(lm, LV_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(lm, lv_color_hex(C_ACCENT_GREEN), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(lm, &montserrat_28_cz, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_label_set_text(lm, "-");
    lv_obj_add_event_cb(btn_minus, thermostat_pm_cb, LV_EVENT_CLICKED, (void*)(intptr_t)-1);

    /* + button vpravo od středu */
    lv_obj_t *btn_plus = lv_button_create(th);
    lv_obj_set_pos(btn_plus, 490, 70);
    lv_obj_set_size(btn_plus, 60, 60);
    lv_obj_set_style_radius(btn_plus, 30, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(btn_plus, lv_color_hex(0x2a3050), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(btn_plus, lv_color_hex(C_ACCENT_GREEN), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(btn_plus, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_t *lp = lv_label_create(btn_plus);
    lv_obj_set_style_align(lp, LV_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(lp, lv_color_hex(C_ACCENT_GREEN), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(lp, &montserrat_28_cz, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_label_set_text(lp, "+");
    lv_obj_add_event_cb(btn_plus, thermostat_pm_cb, LV_EVENT_CLICKED, (void*)(intptr_t)+1);

    /* Mode pills - dole */
    static const char *mode_names[] = { "Topení", "Chlazení", "Auto", "Vyp." };
    static const uint32_t mode_colors[] = { 0xff6644, 0x44aaff, 0xa060ff, 0x4a7aaa };
    for (int i = 0; i < 4; i++) {
        lv_obj_t *btn = lv_button_create(th);
        g_thermostat_mode_btns[i] = btn;
        lv_obj_set_pos(btn, 30 + i * 185, 160);
        lv_obj_set_size(btn, 170, 38);
        lv_obj_set_style_radius(btn, 19, LV_PART_MAIN | LV_STATE_DEFAULT);
        bool active = (i == g_thermostat_mode);
        lv_obj_set_style_bg_color(btn,
            lv_color_hex(active ? mode_colors[i] : C_BG_HEADER),
            LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_border_color(btn,
            lv_color_hex(active ? mode_colors[i] : C_BORDER),
            LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_border_width(btn, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_t *lbl = lv_label_create(btn);
        lv_obj_set_style_align(lbl, LV_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_color(lbl,
            lv_color_hex(active ? 0xffffff : C_TEXT_DIM),
            LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_font(lbl, &montserrat_14_cz, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_label_set_text(lbl, mode_names[i]);
        lv_obj_add_event_cb(btn, thermostat_mode_cb, LV_EVENT_CLICKED, (void*)(intptr_t)i);
    }
}

/* === MQTT command publish helpers === */
/* Entity ID čte za běhu z config_manager (cfg_iface.h) */

static void publish_light_toggle(int idx) {
    if (idx < 0 || idx >= CFG_MAX_LIGHTS) return;
    const char *eid = cfg_get_light_entity(idx);
    if (!eid || !eid[0]) return;
    /* Aktuální stav */
    bool cur = false;
    switch (idx) {
        case 0: cur = g_light_obyvak;   break;
        case 1: cur = g_light_kuchyn;   break;
        case 2: cur = g_light_pocitac;  break;
        case 3: cur = g_light_koupelna; break;
        case 4: cur = g_light_predsin;  break;
        case 5: cur = g_light_zahrada;  break;
        default: return;
    }
    /* Ovládání přes HA REST API (funguje pro jakoukoliv entitu) */
    if (g_ha_call) {
        char body[128];
        snprintf(body, sizeof(body), "{\"entity_id\":\"%s\"}", eid);
        g_ha_call(cur ? "homeassistant/turn_off" : "homeassistant/turn_on", body);
    }
    /* Optimistický update — okamžitá odezva UI bez čekání na MQTT echo */
    switch (idx) {
        case 0: g_light_obyvak   = !cur; break;
        case 1: g_light_kuchyn   = !cur; break;
        case 2: g_light_pocitac  = !cur; break;
        case 3: g_light_koupelna = !cur; break;
        case 4: g_light_predsin  = !cur; break;
        case 5: g_light_zahrada  = !cur; break;
    }
}

static void publish_thermostat_target(void) {
    if (!g_ha_call) return;
    const char *eid = cfg_get_thermostat_entity();
    if (!eid || !eid[0]) return;
    char body[128];
    snprintf(body, sizeof(body), "{\"entity_id\":\"%s\",\"temperature\":%.1f}", eid, g_thermostat_target);
    g_ha_call("climate/set_temperature", body);
}

static void publish_thermostat_mode(int mode_idx) {
    if (!g_ha_call) return;
    static const char *hvac_modes[4] = { "heat", "cool", "auto", "off" };
    if (mode_idx < 0 || mode_idx >= 4) return;
    const char *eid = cfg_get_thermostat_entity();
    if (!eid || !eid[0]) return;
    char body[128];
    snprintf(body, sizeof(body), "{\"entity_id\":\"%s\",\"hvac_mode\":\"%s\"}", eid, hvac_modes[mode_idx]);
    g_ha_call("climate/set_hvac_mode", body);
}

static void light_pill_clicked_cb(lv_event_t *e) {
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    publish_light_toggle(idx);
}

/* LIGHTS: seznam místností s ON/OFF stavy */
static void show_lights_content(void) {
    lv_obj_t *p = g_content_container;
    lv_obj_t *card = make_card(p, 50, 5, 700, 350, C_ACCENT_GREEN);
    card_title(card, "OVLÁDÁNÍ SVĚTEL", C_ACCENT_GREEN);
    for (int i = 0; i < CFG_MAX_LIGHTS; i++) {
        int y = 40 + i * 48;
        lv_obj_t *n = lv_label_create(card);
        lv_obj_set_pos(n, 20, y);
        lv_obj_set_style_text_color(n, lv_color_hex(0xe0f0ff), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_font(n, &montserrat_20_cz, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_label_set_text(n, cfg_get_light_name(i));
        /* pill - default OFF, timer cb to opraví dle real stavu */
        lv_obj_t *sw = lv_obj_create(card);
        lv_obj_set_pos(sw, 580, y + 2);
        lv_obj_set_size(sw, 90, 28);
        lv_obj_set_style_radius(sw, 14, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_color(sw, lv_color_hex(0x2a1a1a), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_border_color(sw, lv_color_hex(0x6a4040), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_border_width(sw, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_pad_all(sw, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_clear_flag(sw, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_t *dot = lv_obj_create(sw);
        lv_obj_set_pos(dot, 6, 8);
        lv_obj_set_size(dot, 12, 12);
        lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_color(dot, lv_color_hex(0x6a4040), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_border_width(dot, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_clear_flag(dot, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_t *st = lv_label_create(sw);
        lv_obj_set_pos(st, 26, 6);
        lv_obj_set_style_text_color(st, lv_color_hex(0x6a4040), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_font(st, &montserrat_14_cz, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_label_set_text(st, "OFF");
        /* store pro timer cb */
        s_lights_pills[i].pill = sw;
        s_lights_pills[i].dot  = dot;
        s_lights_pills[i].lbl  = st;
        s_lights_pills[i].last_state = false;
        /* Klik na pill -> publish toggle */
        lv_obj_add_flag(sw, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(sw, light_pill_clicked_cb, LV_EVENT_CLICKED, (void*)(intptr_t)i);
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * ENERGY — Power flow diagram (3 kruhové ukazatele + domeček + šipky)
 * Slot 0 = Solár (nahoře), Slot 1 = Spotřeba (vlevo dole), Slot 2 = Síť (vpravo dole)
 * Slot 3-5 = textové metriky v rozích (kWh, V, ...)
 * ═══════════════════════════════════════════════════════════════════════════ */


/* Středy kruhů v souřadnicích content containeru */

/* Barvy: solár=žlutooranžová, spotřeba=zelená, síť=modrá */

/* Statické pole bodů pro lv_line (musí přežít funkci) */
static lv_point_precise_t s_ept[6][2]; /* 0=solar-house, 1=house-cons, 2=house-grid,
                                           3-5 = šipky (krátké) */

/* Ukazatelé pro timer update */
static lv_obj_t *s_eval[3]  = {NULL, NULL, NULL};
static lv_obj_t *s_emtx[3]  = {NULL, NULL, NULL}; /* text metriky sl. 3-5 */

/* --- Pomocná: jeden kruhový ukazatel ------------------------------------ */
/* --- Pomocná: čára + šipková značka uprostřed --------------------------- */
static void make_e_line(lv_obj_t *par, int idx,
                         int x1, int y1, int x2, int y2,
                         uint32_t col, const char *arrow) {
    s_ept[idx][0].x = x1; s_ept[idx][0].y = y1;
    s_ept[idx][1].x = x2; s_ept[idx][1].y = y2;
    lv_obj_t *ln = lv_line_create(par);
    lv_line_set_points(ln, s_ept[idx], 2);
    lv_obj_set_style_line_color(ln, lv_color_hex(col), 0);
    lv_obj_set_style_line_width(ln, 2, 0);
    /* šipka uprostřed */
    lv_obj_t *la = lv_label_create(par);
    lv_obj_set_style_text_color(la, lv_color_hex(col), 0);
    lv_obj_set_style_text_font(la,  &montserrat_14_cz, 0);
    lv_label_set_text(la, arrow);
    lv_obj_set_pos(la, (x1+x2)/2 - 6, (y1+y2)/2 - 9);
}

/* Helper: node box — vraci pointer na value label */
static lv_obj_t *make_e_node(lv_obj_t *par,
                              int x, int y, int w, int h,
                              uint32_t col,
                              const char *name, const char *val_str,
                              const char *unit_str) {
    lv_obj_t *box = lv_obj_create(par);
    lv_obj_set_pos(box, x, y);
    lv_obj_set_size(box, w, h);
    lv_obj_set_style_bg_color(box,     lv_color_hex(0x080c14), 0);
    lv_obj_set_style_border_color(box, lv_color_hex(col),      0);
    lv_obj_set_style_border_width(box, 2, 0);
    lv_obj_set_style_radius(box, 8, 0);
    lv_obj_set_style_pad_all(box, 0, 0);
    lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);
    /* Barevny pruh nahore */
    lv_obj_t *stripe = lv_obj_create(box);
    lv_obj_set_pos(stripe, 0, 0);
    lv_obj_set_size(stripe, w, 4);
    lv_obj_set_style_bg_color(stripe, lv_color_hex(col), 0);
    lv_obj_set_style_border_width(stripe, 0, 0);
    lv_obj_set_style_radius(stripe, 0, 0);
    lv_obj_clear_flag(stripe, LV_OBJ_FLAG_SCROLLABLE);
    /* Nazev */
    lv_obj_t *lnm = lv_label_create(box);
    lv_obj_set_pos(lnm, 0, 7);
    lv_obj_set_width(lnm, w);
    lv_obj_set_style_text_align(lnm, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(lnm,  &montserrat_14_cz, 0);
    lv_obj_set_style_text_color(lnm, lv_color_hex(0x4a7aaa), 0);
    lv_label_set_text(lnm, name);
    /* Hodnota — val_str jiz obsahuje jednotku (napr. "0 W", "1.5 kW") */
    (void)unit_str;
    lv_obj_t *lval = lv_label_create(box);
    lv_obj_set_pos(lval, 0, (h >= 65) ? 30 : 24);
    lv_obj_set_width(lval, w);
    lv_obj_set_style_text_align(lval, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(lval, &montserrat_20_cz, 0);
    lv_obj_set_style_text_color(lval, lv_color_hex(col), 0);
    lv_label_set_text(lval, val_str);
    return lval;
}

static void show_energy_content(void) {
    lv_obj_t *p = g_content_container;
    for (int i = 0; i < 3; i++) { s_eval[i]=NULL; s_emtx[i]=NULL; }
    char buf[28];

    /* === DESIGN A: tok energie, 4 uzly + centralni inverter ===
     *          [  SOLAR  ]          <- top center
     *               |
     * [SIT]---[INVERTER]---[DUM]   <- middle row
     *               |
     *         [ BATERIE ]          <- bottom center
     *  -------- stats bar -------  <- dolni pruh
     */

#define ENODE_BUF(slot) do { \
        float _v = g_energy_power[slot]; \
        float _a = _v < 0 ? -_v : _v; \
        if (_a >= 1000.0f) snprintf(buf, sizeof(buf), "%.2f kW", _v/1000.0f); \
        else               snprintf(buf, sizeof(buf), "%.0f W",  _v); \
    } while(0)

    /* Solar (nahozu, slot 0) */
    ENODE_BUF(0);
    { const char *u = cfg_get_energy_unit(0); if (!u||!u[0]) u="W";
      const char *n = cfg_get_energy_name(0); if (!n||!n[0]) n="SOL\xc3\x81R";
      s_eval[0] = make_e_node(p, 310,  6, 180, 85, 0xFFAA00, n, buf, u); }

    /* Sit (vlevo, slot 2) */
    ENODE_BUF(2);
    { const char *u = cfg_get_energy_unit(2); if (!u||!u[0]) u="W";
      const char *n = cfg_get_energy_name(2); if (!n||!n[0]) n="S\xc3\x8d\xc5\xa4";
      s_eval[2] = make_e_node(p,  5, 160, 138, 70, 0x4FC3F7, n, buf, u); }

    /* Dum (vpravo, slot 1) */
    ENODE_BUF(1);
    { const char *u = cfg_get_energy_unit(1); if (!u||!u[0]) u="W";
      const char *n = cfg_get_energy_name(1); if (!n||!n[0]) n="D\xc5\xafM";
      s_eval[1] = make_e_node(p, 657, 160, 138, 70, 0xFF8A65, n, buf, u); }

    /* Baterie (dole, slot 3) */
    ENODE_BUF(3);
    { const char *u = cfg_get_energy_unit(3); if (!u||!u[0]) u="W";
      const char *n = cfg_get_energy_name(3); if (!n||!n[0]) n="BATERIE";
      s_emtx[0] = make_e_node(p, 310, 262, 180, 85, 0x00C87A, n, buf, u); }

    /* Hub — centralni inverter */
    {
        lv_obj_t *hub = lv_obj_create(p);
        lv_obj_set_pos(hub, 345, 163);
        lv_obj_set_size(hub, 110, 66);
        lv_obj_set_style_bg_color(hub,     lv_color_hex(0x0d1825), 0);
        lv_obj_set_style_border_color(hub, lv_color_hex(0x2a5a8a), 0);
        lv_obj_set_style_border_width(hub, 2, 0);
        lv_obj_set_style_radius(hub, 6, 0);
        lv_obj_set_style_pad_all(hub, 0, 0);
        lv_obj_clear_flag(hub, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_t *hl = lv_label_create(hub);
        lv_obj_set_pos(hl, 0, 24);
        lv_obj_set_width(hl, 110);
        lv_obj_set_style_text_align(hl, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_font(hl,  &montserrat_14_cz, 0);
        lv_obj_set_style_text_color(hl, lv_color_hex(0x4a8acc), 0);
        lv_label_set_text(hl, "INVERTER");
    }

    /* Spojovaci cary se sipkami */
    /* solar bottom(400,84) -> hub top(400,168)  */
    make_e_line(p, 0, 400, 91,  400, 163, 0xFFAA00, "v");
    /* grid right(125,193) -> hub left(352,193)  */
    make_e_line(p, 1, 143, 195, 345, 195, 0x4FC3F7, ">");
    /* hub right(448,193) -> house left(675,193) */
    make_e_line(p, 2, 455, 195, 657, 195, 0xFF8A65, ">");
    /* hub bottom(400,222) -> bat top(400,268)   */
    make_e_line(p, 3, 400, 229, 400, 262, 0x00C87A, "v");

    /* Stats pruh dole: Vyroba dnes (slot 4) + Baterie % (slot 5) */
    {
        /* Tmave pozadi — explicitni bg_opa + zadny shadow/border */
        lv_obj_t *sb = lv_obj_create(p);
        lv_obj_set_pos(sb, 0, 347);
        lv_obj_set_size(sb, 800, 23);
        lv_obj_set_style_bg_color(sb,     lv_color_hex(0x040b14), 0);
        lv_obj_set_style_bg_opa(sb,       LV_OPA_COVER, 0);
        lv_obj_set_style_border_color(sb, lv_color_hex(0x1a3a5a), 0);
        lv_obj_set_style_border_width(sb, 1, 0);
        lv_obj_set_style_border_side(sb,  LV_BORDER_SIDE_TOP, 0);
        lv_obj_set_style_radius(sb,       0, 0);
        lv_obj_set_style_pad_all(sb,      0, 0);
        lv_obj_set_style_shadow_width(sb, 0, 0);
        lv_obj_clear_flag(sb, LV_OBJ_FLAG_SCROLLABLE);

        /* Slot 4 — Vyroba dnes (denni energie, kWh) */
        {
            const char *sn = cfg_get_energy_name(4);
            if (!sn || !sn[0]) sn = "V\xc3\xbdroba dnes";
            const char *su = cfg_get_energy_unit(4);
            if (!su || !su[0]) su = "kWh";
            snprintf(buf, sizeof(buf), "%.1f %s", g_energy_power[4], su);
            lv_obj_t *lk4 = lv_label_create(p);
            lv_obj_set_pos(lk4, 20, 352);
            lv_obj_set_style_text_color(lk4, lv_color_hex(0x4a7aaa), 0);
            lv_obj_set_style_text_font(lk4,  &montserrat_14_cz, 0);
            lv_label_set_text(lk4, sn);
            lv_obj_t *lv4 = lv_label_create(p);
            lv_obj_set_pos(lv4, 148, 352);
            lv_obj_set_style_text_color(lv4, lv_color_hex(0xFFAA00), 0);
            lv_obj_set_style_text_font(lv4,  &montserrat_14_cz, 0);
            lv_label_set_text(lv4, buf);
            s_emtx[1] = lv4;
        }
        /* Slot 5 — Baterie % (SOC baterie) */
        {
            const char *sn = cfg_get_energy_name(5);
            if (!sn || !sn[0]) sn = "Baterie %";
            const char *su = cfg_get_energy_unit(5);
            if (!su || !su[0]) su = "%";
            snprintf(buf, sizeof(buf), "%.0f %s", g_energy_power[5], su);
            lv_obj_t *lk5 = lv_label_create(p);
            lv_obj_set_pos(lk5, 430, 352);
            lv_obj_set_style_text_color(lk5, lv_color_hex(0x4a7aaa), 0);
            lv_obj_set_style_text_font(lk5,  &montserrat_14_cz, 0);
            lv_label_set_text(lk5, sn);
            lv_obj_t *lv5 = lv_label_create(p);
            lv_obj_set_pos(lv5, 530, 352);
            lv_obj_set_style_text_color(lv5, lv_color_hex(0x00C87A), 0);
            lv_obj_set_style_text_font(lv5,  &montserrat_14_cz, 0);
            lv_label_set_text(lv5, buf);
            s_emtx[2] = lv5;
        }
    }

#undef ENODE_BUF
}

/* SYSTEM: info o ESP32 */
/* Pointery na MQTT debug labely — updatuji se v timeru */
static lv_obj_t *s_sys_dbg_lbl[MQTT_DBG_LINES] = {NULL,NULL,NULL,NULL};
static lv_obj_t *s_sys_dbg_cnt = NULL;

static void show_system_content(void) {
    lv_obj_t *p = g_content_container;

    /* ── Systémové info (horní karta) ── */
    lv_obj_t *card = make_card(p, 10, 5, 780, 175, C_ACCENT_PURPLE);
    card_title(card, "ESP32 SYSTEM INFO", C_ACCENT_PURPLE);

    char heap_buf[28], uptime_buf[28];
    uint32_t free_kb = 0, total_kb = 0;
    sys_get_heap(&free_kb, &total_kb);
    snprintf(heap_buf,   sizeof(heap_buf),   "%lu KB / %lu KB",
             (unsigned long)free_kb, (unsigned long)total_kb);
    uint32_t ud = 0, uh = 0, um = 0;
    sys_get_uptime(&ud, &uh, &um);
    snprintf(uptime_buf, sizeof(uptime_buf), "%lud %luh %lum",
             (unsigned long)ud, (unsigned long)uh, (unsigned long)um);

    struct { const char *k; const char *v; } rows[] = {
        { "Chip",      "ESP32-S3  240 MHz" },
        { "Heap free", heap_buf            },
        { "WiFi SSID", sys_get_wifi_ssid() },
        { "IP adresa", sys_get_wifi_ip()   },
        { "Uptime",    uptime_buf          },
    };
    for (int i = 0; i < 5; i++) {
        int col = i < 3 ? 0 : 1;
        int row = i < 3 ? i : (i - 3);
        int x = col == 0 ?  20 : 400;
        int y = 38 + row * 40;
        lv_obj_t *k = lv_label_create(card);
        lv_obj_set_pos(k, x, y);
        lv_obj_set_style_text_color(k, lv_color_hex(0x4a7aaa), 0);
        lv_obj_set_style_text_font(k, &montserrat_14_cz, 0);
        lv_label_set_text(k, rows[i].k);
        lv_obj_t *v = lv_label_create(card);
        lv_obj_set_pos(v, x + 100, y);
        lv_obj_set_style_text_color(v, lv_color_hex(0xe0f0ff), 0);
        lv_obj_set_style_text_font(v, &montserrat_14_cz, 0);
        lv_label_set_text(v, rows[i].v);
    }

    /* ── MQTT debug (dolní karta) ── */
    lv_obj_t *dcard = make_card(p, 10, 187, 780, 178, 0x007acc);
    card_title(dcard, "MQTT DEBUG", 0x007acc);

    /* Radek: "Base topic: ha" */
    {
        lv_obj_t *lb = lv_label_create(dcard);
        lv_obj_set_pos(lb, 15, 30);
        lv_obj_set_style_text_color(lb, lv_color_hex(0x4a7aaa), 0);
        lv_obj_set_style_text_font(lb, &montserrat_14_cz, 0);
        lv_label_set_text(lb, "Base topic:");
        lv_obj_t *lv = lv_label_create(dcard);
        lv_obj_set_pos(lv, 120, 30);
        lv_obj_set_style_text_color(lv, lv_color_hex(0x4FC3F7), 0);
        lv_obj_set_style_text_font(lv, &montserrat_14_cz, 0);
        lv_label_set_text(lv, g_mqtt_base_topic_dbg);
    }

    /* Radek: "Prijato: X zprav" */
    {
        lv_obj_t *lb = lv_label_create(dcard);
        lv_obj_set_pos(lb, 300, 30);
        lv_obj_set_style_text_color(lb, lv_color_hex(0x4a7aaa), 0);
        lv_obj_set_style_text_font(lb, &montserrat_14_cz, 0);
        lv_label_set_text(lb, "Prij.:");
        s_sys_dbg_cnt = lv_label_create(dcard);
        lv_obj_set_pos(s_sys_dbg_cnt, 355, 30);
        lv_obj_set_style_text_color(s_sys_dbg_cnt, lv_color_hex(C_ACCENT_GREEN), 0);
        lv_obj_set_style_text_font(s_sys_dbg_cnt, &montserrat_14_cz, 0);
        lv_label_set_text(s_sys_dbg_cnt, "0");
    }

    /* 4 radky poslednich topiců */
    for (int i = 0; i < MQTT_DBG_LINES; i++) {
        int y = 52 + i * 28;
        s_sys_dbg_lbl[i] = lv_label_create(dcard);
        lv_obj_set_pos(s_sys_dbg_lbl[i], 15, y);
        lv_obj_set_size(s_sys_dbg_lbl[i], 750, 26);
        lv_obj_set_style_text_color(s_sys_dbg_lbl[i],
            lv_color_hex(i == MQTT_DBG_LINES-1 ? (uint32_t)C_TEXT_PRIMARY : (uint32_t)C_TEXT_DIM), 0);
        lv_obj_set_style_text_font(s_sys_dbg_lbl[i], &montserrat_14_cz, 0);
        lv_label_set_text(s_sys_dbg_lbl[i], "---");
    }
}

/* ╔═══════════════════════════════════════════════════════════════════════════╗
   ║ MAIN SCREEN BUILD                                                         ║
   ╚═══════════════════════════════════════════════════════════════════════════╝ */
/* Reset activity tick na jakýkoliv touch (screen-level event) */
static void screen_activity_cb(lv_event_t *e) {
    (void)e;
    if (g_screensaver_active) {
        screensaver_hide();
        return;  /* první tap pouze zruší screensaver, neinteraguje s UI */
    }
    g_last_activity_tick = lv_tick_get();
}

/* LVGL timer 1×/s — propíše hodnoty z dashboard_data.h do UI labelů.
 * Pily MQTT/WiFi se přebarví podle stavu připojení.
 * Karty se updatují jen na HOME (g_current_screen == 0) — jinde nemá smysl.
 */
static void ui_data_update_timer_cb(lv_timer_t *t) {
    (void)t;
    char buf[40];

    /* === Status pily (vždycky, na všech obrazovkách) === */
    if (objects.lbl_wifi_status) {
        lv_obj_set_style_text_color(objects.lbl_wifi_status,
            lv_color_hex(g_wifi_ok ? C_ONLINE : 0xff4444),
            LV_PART_MAIN | LV_STATE_DEFAULT);
    }
    if (objects.lbl_mqtt_status) {
        lv_obj_set_style_text_color(objects.lbl_mqtt_status,
            lv_color_hex(g_mqtt_connected ? C_ONLINE : 0xff4444),
            LV_PART_MAIN | LV_STATE_DEFAULT);
    }

    /* === SYSTEM tab: update MQTT debug (kazdu sekundu) === */
    if (g_current_screen == 4) {
        if (s_sys_dbg_cnt) {
            static char cnt_buf[12];
            snprintf(cnt_buf, sizeof(cnt_buf), "%d", g_mqtt_dbg_count);
            lv_label_set_text(s_sys_dbg_cnt, cnt_buf);
        }
        int total = g_mqtt_dbg_count;
        int show  = total < MQTT_DBG_LINES ? total : MQTT_DBG_LINES;
        for (int i = 0; i < MQTT_DBG_LINES; i++) {
            if (!s_sys_dbg_lbl[i]) continue;
            if (i < (MQTT_DBG_LINES - show)) {
                lv_label_set_text(s_sys_dbg_lbl[i], "---");
            } else {
                int src = i - (MQTT_DBG_LINES - show);  /* 0..show-1 */
                static char dbg_line[100];
                int dbg_idx = (g_mqtt_dbg_count - show + src) % MQTT_DBG_LINES;
                if (dbg_idx < 0) dbg_idx += MQTT_DBG_LINES;
                const char *tp = g_mqtt_dbg[dbg_idx].topic;
                const char *vl = g_mqtt_dbg[dbg_idx].value;
                snprintf(dbg_line, sizeof(dbg_line), "%s = %s", tp, vl);
                lv_label_set_text(s_sys_dbg_lbl[i], dbg_line);
            }
        }
        return;
    }

    /* === HOME content (jen pokud jsme na HOME a máme valid data) === */
    if (g_current_screen != 0) return;
    if (!g_data_valid) return;

    /* VENKU */
    if (objects.lbl_venku_temp && g_venku_temp != 0.0f) {
        snprintf(buf, sizeof(buf), "%.1f°C", g_venku_temp);
        lv_label_set_text(objects.lbl_venku_temp, buf);
    }
    if (objects.lbl_venku_vlhk && g_venku_hum != 0.0f) {
        snprintf(buf, sizeof(buf), "Vlhkost: %d%%", (int)g_venku_hum);
        lv_label_set_text(objects.lbl_venku_vlhk, buf);
        if (objects.pb_venku_vlhk) {
            int w = (220 * (int)g_venku_hum) / 100;
            if (w < 0) w = 0; if (w > 220) w = 220;
            lv_obj_set_width(objects.pb_venku_vlhk, w);
        }
    }

    /* OBÝVÁK */
    if (objects.lbl_obyvak_temp && g_obyvak_temp != 0.0f) {
        snprintf(buf, sizeof(buf), "%.1f°C", g_obyvak_temp);
        lv_label_set_text(objects.lbl_obyvak_temp, buf);
    }
    if (objects.lbl_obyvak_vlhk && g_obyvak_hum != 0.0f) {
        snprintf(buf, sizeof(buf), "Vlhkost: %d%%", (int)g_obyvak_hum);
        lv_label_set_text(objects.lbl_obyvak_vlhk, buf);
        if (objects.pb_obyvak_vlhk) {
            int w = (220 * (int)g_obyvak_hum) / 100;
            if (w < 0) w = 0; if (w > 220) w = 220;
            lv_obj_set_width(objects.pb_obyvak_vlhk, w);
        }
    }

    /* POČASÍ */
    if (objects.lbl_weather_temp && g_weather_temp != 0.0f) {
        snprintf(buf, sizeof(buf), "%.0f°C", g_weather_temp);
        lv_label_set_text(objects.lbl_weather_temp, buf);
    }
    if (objects.lbl_weather_state && g_weather_state_cz[0] != 0) {
        lv_label_set_text(objects.lbl_weather_state, g_weather_state_cz);
    }
    if (objects.lbl_weather_humidity && g_weather_humidity != 0.0f) {
        snprintf(buf, sizeof(buf), "vlh: %d%%", (int)g_weather_humidity);
        lv_label_set_text(objects.lbl_weather_humidity, buf);
    }
    if (objects.lbl_weather_wind && g_weather_wind != 0.0f) {
        snprintf(buf, sizeof(buf), "vítr: %.0f km/h", g_weather_wind);
        lv_label_set_text(objects.lbl_weather_wind, buf);
    }
    if (objects.lbl_weather_pressure && g_weather_pressure != 0.0f) {
        snprintf(buf, sizeof(buf), "%.0f hPa", g_weather_pressure);
        lv_label_set_text(objects.lbl_weather_pressure, buf);
    }
    if (objects.lbl_weather_min && g_weather_temp_min != 0.0f) {
        snprintf(buf, sizeof(buf), "min: %.0f°", g_weather_temp_min);
        lv_label_set_text(objects.lbl_weather_min, buf);
    }
    if (objects.lbl_weather_max && g_weather_temp_max != 0.0f) {
        snprintf(buf, sizeof(buf), "max: %.0f°", g_weather_temp_max);
        lv_label_set_text(objects.lbl_weather_max, buf);
    }

    /* Weather icon - překreslit jen pokud se stav změnil (jinak flikere) */
    static char last_state[24] = "";
    if (objects.icon_weather && g_weather_state[0] != 0 && strcmp(last_state, g_weather_state) != 0) {
        strlcpy(last_state, g_weather_state, sizeof(last_state));
        draw_weather_icon(objects.icon_weather, g_weather_state);
    }

    /* PŘEDPOVĚĎ - 5 sloupců, jen pokud máme valid data */
    char fbuf[16];
    for (int i = 0; i < 5; i++) {
        if (!g_forecast[i].valid) continue;
        if (s_forecast_widgets[i].lbl_day) {
            lv_label_set_text(s_forecast_widgets[i].lbl_day, g_forecast[i].day);
        }
        if (s_forecast_widgets[i].lbl_max) {
            snprintf(fbuf, sizeof(fbuf), "%.0f°", g_forecast[i].temp_max);
            lv_label_set_text(s_forecast_widgets[i].lbl_max, fbuf);
        }
        if (s_forecast_widgets[i].lbl_min) {
            snprintf(fbuf, sizeof(fbuf), "%.0f°", g_forecast[i].temp_min);
            lv_label_set_text(s_forecast_widgets[i].lbl_min, fbuf);
        }
        /* Ikona - překreslit jen pokud se cond změnila */
        if (s_forecast_widgets[i].icon &&
            strcmp(s_forecast_widgets[i].last_cond, g_forecast[i].condition) != 0) {
            strlcpy(s_forecast_widgets[i].last_cond, g_forecast[i].condition,
                    sizeof(s_forecast_widgets[i].last_cond));
            lv_obj_clean(s_forecast_widgets[i].icon);
            draw_weather_icon_mini(s_forecast_widgets[i].icon, 0, 0, g_forecast[i].condition);
        }
    }

    /* Dlaždice SVĚTLA na HOME — barva tečky + text stavu */
    {
        static const bool *states[6] = {
            &g_light_obyvak, &g_light_kuchyn, &g_light_pocitac,
            &g_light_koupelna, &g_light_predsin, &g_light_zahrada
        };
        for (int i = 0; i < 6; i++) {
            if (!s_home_ldot[i] || !s_home_lst[i]) continue;
            bool on = *states[i];
            lv_obj_set_style_bg_color(s_home_ldot[i],
                lv_color_hex(on ? (uint32_t)C_ONLINE : 0x3a2a2a), 0);
            lv_label_set_text(s_home_lst[i], on ? "ON" : "OFF");
            lv_obj_set_style_text_color(s_home_lst[i],
                lv_color_hex(on ? (uint32_t)C_TEXT_ON : (uint32_t)C_TEXT_OFF), 0);
        }
    }
}

/* Aktualizuje jeden light pill na LIGHTS obrazovce */
static void update_light_pill(light_pill_t *p, bool on) {
    if (!p || !p->pill) return;
    if (p->last_state == on) return;   /* žádná změna — nič nekresli */
    p->last_state = on;
    lv_obj_set_style_bg_color(p->pill,
        lv_color_hex(on ? (uint32_t)0x0d2b1a : (uint32_t)0x2a1a1a), 0);
    lv_obj_set_style_border_color(p->pill,
        lv_color_hex(on ? (uint32_t)C_ONLINE : (uint32_t)0x6a4040), 0);
    lv_obj_set_style_bg_color(p->dot,
        lv_color_hex(on ? (uint32_t)C_ONLINE : (uint32_t)0x6a4040), 0);
    lv_label_set_text(p->lbl, on ? "ON" : "OFF");
    lv_obj_set_style_text_color(p->lbl,
        lv_color_hex(on ? (uint32_t)C_TEXT_ON : (uint32_t)0x6a4040), 0);
}

/* LVGL timer 2×/s — update KLIMATE a LIGHTS obrazovek */
static void ui_klimate_lights_update_timer_cb(lv_timer_t *t) {
    (void)t;
    char buf[40];

    /* ENERGY — update hodnot (sloty 0-2 = karty, sloty 3-5 = extra metriky) */
    if (g_current_screen == 3) {
        char ebuf[24];
        /* Sloty 0-2: solár, spotřeba, síť (velké karty) */
        for (int i = 0; i < 3; i++) {
            if (!s_eval[i]) continue;
            float val  = g_energy_power[i];
            float aval = val < 0.0f ? -val : val;
            if (aval >= 1000.0f) snprintf(ebuf, sizeof(ebuf), "%.1f kW", val / 1000.0f);
            else                  snprintf(ebuf, sizeof(ebuf), "%.0f W",  val);
            lv_label_set_text(s_eval[i], ebuf);
        }
        /* Sloty 3-5: baterie W/kW | vyroba dnes kWh | SOC baterie % */
        for (int i = 0; i < 3; i++) {
            if (!s_emtx[i]) continue;
            int slot = i + 3;
            float val  = g_energy_power[slot];
            float aval = val < 0.0f ? -val : val;
            if (slot == 3) {
                /* Baterie — vykon W / kW */
                if (aval >= 1000.0f) snprintf(ebuf, sizeof(ebuf), "%.1f kW", val / 1000.0f);
                else                  snprintf(ebuf, sizeof(ebuf), "%.0f W",  val);
            } else if (slot == 5) {
                /* Baterie SOC — procenta */
                const char *su = cfg_get_energy_unit(slot);
                if (!su || !su[0]) su = "%";
                snprintf(ebuf, sizeof(ebuf), "%.0f %s", val, su);
            } else {
                /* Slot 4 = Vyroba dnes (kWh) — pouzij nakonfigurovnou jednotku primo */
                const char *su = cfg_get_energy_unit(slot);
                if (!su || !su[0]) su = "kWh";
                snprintf(ebuf, sizeof(ebuf), "%.1f %s", val, su);
            }
            lv_label_set_text(s_emtx[i], ebuf);
        }
    }

    if (g_current_screen == 1 && g_data_valid) {
        if (objects.lbl_kli_venku && g_venku_temp != 0.0f) {
            snprintf(buf, sizeof(buf), "%.1f°C", g_venku_temp);
            lv_label_set_text(objects.lbl_kli_venku, buf);
        }
        if (objects.lbl_kli_pracovna && g_kli_pracovna_temp != 0.0f) {
            snprintf(buf, sizeof(buf), "%.1f°C", g_kli_pracovna_temp);
            lv_label_set_text(objects.lbl_kli_pracovna, buf);
        }
        if (objects.lbl_kli_obyvak && g_obyvak_temp != 0.0f) {
            snprintf(buf, sizeof(buf), "%.1f°C", g_obyvak_temp);
            lv_label_set_text(objects.lbl_kli_obyvak, buf);
        }
        if (objects.lbl_kli_bojler && g_kli_bojler_temp != 0.0f) {
            snprintf(buf, sizeof(buf), "%.0f°C", g_kli_bojler_temp);
            lv_label_set_text(objects.lbl_kli_bojler, buf);
        }
        if (objects.lbl_kli_kotel && g_kli_kotel_temp != 0.0f) {
            snprintf(buf, sizeof(buf), "%.0f°C", g_kli_kotel_temp);
            lv_label_set_text(objects.lbl_kli_kotel, buf);
        }
        if (objects.lbl_thermostat_current) {
            if (g_thermostat_current > 0.0f)
                snprintf(buf, sizeof(buf), "%.1f\xc2\xb0\x43", g_thermostat_current);
            else
                strlcpy(buf, "--\xc2\xb0\x43", sizeof(buf));
            lv_label_set_text(objects.lbl_thermostat_current, buf);
        }
    }

    if (g_current_screen == 2) {
        update_light_pill(&s_lights_pills[0], g_light_obyvak);
        update_light_pill(&s_lights_pills[1], g_light_kuchyn);
        update_light_pill(&s_lights_pills[2], g_light_pocitac);
        update_light_pill(&s_lights_pills[3], g_light_koupelna);
        update_light_pill(&s_lights_pills[4], g_light_predsin);
        update_light_pill(&s_lights_pills[5], g_light_zahrada);
    }
}

/* ╔═══════════════════════════════════════════════════════════════════════════╗
   ║ AUTO-RETURN TIMER                                                          ║
   ╚═══════════════════════════════════════════════════════════════════════════╝ */
static void auto_return_home_timer_cb(lv_timer_t *t) {
    (void)t;
    if (g_current_screen == 0) return;  /* už jsme na HOME */
    if (lv_tick_elaps(g_last_activity_tick) > AUTO_HOME_TIMEOUT_MS) {
        switch_to_screen(0);
    }
}

static void screensaver_update(void) {
    if (!g_screensaver_active) return;
    char buf[32];
    /* Hodiny */
    if (g_ss_lbl_time && objects.lbl_cas)
        lv_label_set_text(g_ss_lbl_time, lv_label_get_text(objects.lbl_cas));
    /* Datum */
    if (g_ss_lbl_date && objects.lbl_datum)
        lv_label_set_text(g_ss_lbl_date, lv_label_get_text(objects.lbl_datum));
    /* Teplota počasí */
    if (g_ss_lbl_temp) {
        snprintf(buf, sizeof(buf), "%.0f\xc2\xb0\x43", g_weather_temp);
        lv_label_set_text(g_ss_lbl_temp, buf);
    }
    /* Ikona počasí */
    if (g_ss_icon) {
        static char last_ss_cond[24] = "";
        if (strcmp(last_ss_cond, g_weather_state) != 0) {
            strlcpy(last_ss_cond, g_weather_state, sizeof(last_ss_cond));
            lv_obj_clean(g_ss_icon);
            draw_weather_icon(g_ss_icon, g_weather_state);
        }
    }
}

static void screensaver_timer_cb(lv_timer_t *t) {
    (void)t;
    extern uint8_t g_screensaver_timeout_val;
    uint8_t timeout_min = g_screensaver_timeout_val;
    static uint32_t dbg_last = 0;
    if (lv_tick_elaps(dbg_last) > 10000) {  /* každých 10s vypiš stav */
        dbg_last = lv_tick_get();
        LV_LOG_USER("SS timer: timeout=%d min, data_valid=%d, elapsed=%lu ms, active=%d",
            (int)timeout_min, (int)g_data_valid,
            (unsigned long)lv_tick_elaps(g_last_activity_tick),
            (int)g_screensaver_active);
    }
    if (timeout_min == 0) return;  /* screensaver vypnutý */
    /* g_data_valid check odstraněn - screensaver funguje i bez MQTT */
    uint32_t timeout_ms = (uint32_t)timeout_min * 60UL * 1000UL;
    if (!g_screensaver_active) {
        if (lv_tick_elaps(g_last_activity_tick) > timeout_ms)
            screensaver_show(objects.main);
    } else {
        screensaver_update();
    }
}

void create_screen_main(void) {
    lv_obj_t *scr = lv_obj_create(NULL);
    objects.main = scr;
    lv_obj_set_pos(scr, 0, 0);
    lv_obj_set_size(scr, 800, 480);
    lv_obj_set_style_bg_color(scr, lv_color_hex(C_BG_SCREEN), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    /* Activity detection - jakýkoliv touch resetuje timer */
    lv_obj_add_event_cb(scr, screen_activity_cb, LV_EVENT_PRESSED, NULL);

    build_header(scr);

    /* Content container 800x370 mezi headerem a nav barem */
    g_content_container = lv_obj_create(scr);
    lv_obj_set_pos(g_content_container, 0, 55);
    lv_obj_set_size(g_content_container, 800, 370);
    lv_obj_set_style_bg_opa(g_content_container, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(g_content_container, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(g_content_container, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_clear_flag(g_content_container, LV_OBJ_FLAG_SCROLLABLE);

    build_nav_bar(scr);

    g_current_screen = 0;
    g_last_activity_tick = lv_tick_get();
    show_home_content();

    /* Timer 1x za sekundu - kontroluje auto-return na HOME */
    lv_timer_create(auto_return_home_timer_cb, 1000, NULL);
    /* Timer 1x za sekundu - propisuje MQTT data do UI (HOME) */
    lv_timer_create(ui_data_update_timer_cb, 1000, NULL);
    /* Timer 2x za sekundu - update KLIMATE/LIGHTS/ENERGY */
    lv_timer_create(ui_klimate_lights_update_timer_cb, 500, NULL);
    /* Timer 1x za sekundu - screensaver */
    lv_timer_create(screensaver_timer_cb, 1000, NULL);

    lv_screen_load(objects.main);
    tick_screen_main();
}

void tick_screen_main(void) {
    /* MQTT live update sem (vola se z main.cpp), zatim prazdne */
}

typedef void (*tick_screen_func_t)(void);
static tick_screen_func_t tick_screen_funcs[] = { tick_screen_main };

void tick_screen(int screen_index) { tick_screen_funcs[screen_index](); }

void create_screens(void) {
    create_screen_main();
}
