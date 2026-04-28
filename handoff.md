# HA Dashboard — handoff dokument (v1.0)

> Kopíruj tento soubor do projektu (např. `HA_Dashboard/HANDOFF.md`).
> V příští konverzaci s Claude ho prostě nahraj přes upload — okamžitě se zorientuje.

## Stav: ✅ Funkční v1.0 (bez MQTT)

- 5 obrazovek (HOME / KLIMATE / LIGHTS / ENERGY / SYSTEM) s tap navigací
- DOMA header + NTP hodiny (zlatavé 28px) + české datum
- MQTT/WiFi pily se zelenou tečkou
- POČASÍ karta s ikonou + 5 metrik (teplota, stav, min/max, vlhkost, vítr, tlak)
- PŘEDPOVĚĎ karta s 5 dny + mini ikonami
- KLIMATE: 5 teplot (VENKU, PRACOVNA, OBÝVÁK, BOJLER, KOTEL) + funkční termostat (+/- buttons, mode pily)
- Auto-return na HOME po 30 s neaktivity na non-home obrazovce
- Plně české (montserrat_14/20/28 s diakritikou)

## Hardware

- **Deska**: ESP32-8048S070C (ESP32-S3, 7" RGB panel 800×480, GT911 dotyk, 8 MB PSRAM, 16 MB Flash)
- **Cesta projektu**: `C:\Users\sruba\eez-projects\HA_Dashboard`

## platformio.ini

```ini
[env:esp32s3box]
platform = espressif32@6.9.0
board = esp32s3box
framework = arduino
build_src_filter = 
  +<*> 
  +<../.pio/libdeps/esp32s3box/lvgl/examples>

build_flags = 
    -D LV_CONF_INCLUDE_SIMPLE
    -I src/
    -D LV_USE_EXAMPLES=1
lib_deps = 
    lvgl/lvgl@^9.2.2
    tamctec/TAMC_GT911@^1.0.2
    moononournation/GFX Library for Arduino@1.5.0
    adafruit/Adafruit NeoPixel @ ^1.12.0
    adafruit/DHT sensor library @ ^1.4.6
    adafruit/Adafruit Unified Sensor @ ^1.1.14
```

> Warning `TFT_BL redefined` (45 vs 2) je neškodný — `display.h` přebíjí pin definici.

## Struktura projektu

```
HA_Dashboard/
├── include/
│   ├── display.h           (driver Arduino_GFX + GT911 + LVGL buffer)
│   ├── fonts_cz.h          (LV_FONT_DECLARE pro 14/20/28 cz fonts)
│   └── screens.h           (objects_t struct + draw_weather_icon proto)
├── src/
│   ├── main.cpp            (WiFi+NTP, optional MQTT, hodiny+datum loop)
│   ├── screens.c           (~1021 řádků - 5 screens, tap navigation, termostat)
│   ├── montserrat_14_cz.c  (bitmap font 4bpp, ~50 KB)
│   ├── montserrat_20_cz.c  (bitmap font 4bpp, ~72 KB)
│   └── montserrat_28_cz.c  (bitmap font 4bpp, ~125 KB)
└── platformio.ini
```

## Layout obrazovky 800×480

```
[0..49]    HEADER  (50 px)
           DOMA | datum | hodiny 28px (vycentrované) | MQTT pill | WiFi pill
[55..425]  CONTENT (~370 px) — swappable
[430..480] NAV BAR (50 px)
           HOME | KLIMATE | LIGHTS | ENERGY | SYSTEM
```

## HOME content (řada 1: VENKU/OBÝVÁK/POČASÍ; řada 2: SVĚTLA/PŘEDPOVĚĎ)

| Karta | Pozice | Obsah |
|---|---|---|
| **VENKU** | (10, 5) 250×185 | Teplota, vlhkost, progress bar, ikona ":)" placeholder |
| **OBÝVÁK** | (275, 5) 250×185 | Teplota, vlhkost, progress bar, "uvnitř" tag |
| **POČASÍ** | (540, 5) 250×185 | Aktuální teplota, stav (Slunečno...), ikona 60×60, min/max/tlak/vlh/vítr |
| **SVĚTLA** | (10, 198) 380×160 | 3 řádky (Obývák, Kuchyň, Ložnice) s ON/OFF |
| **PŘEDPOVĚĎ** | (405, 198) 380×160 | 5 dní (Po-Pá) s mini ikonami 40×40, max/min teplota |

## KLIMATE content

- **Řada 1**: 5 teplotních karet 156×130 (VENKU, PRACOVNA, OBÝVÁK, BOJLER, KOTEL)
- **Řada 2**: TERMOSTAT panel 790×220 — current temp, target temp s **funkčními +/- buttony** (krok 0.5°C, range 5-30°C), 4 mode pily (Topení/Chlazení/Auto/Vyp.) - perzistuje mezi přepnutím obrazovek

## LIGHTS content
6 řádků se "switch" pily (Obývák, Kuchyň, Ložnice, Koupelna, Předsíň, Zahrada) — mock ON/OFF

## ENERGY content
4 metric karty + placeholder na graf

## SYSTEM content
9 řádků s ESP32 info (chip, flash, PSRAM, heap, IP, uptime, firmware, LVGL)

## MQTT mapping (v `main.cpp`, zatím `ENABLE_MQTT = 0`)

```
homeassistant/sensor/venku/temperature/state    → g_venku_temp
homeassistant/sensor/venku/humidity/state       → g_venku_vlhk
homeassistant/sensor/obyvak/temperature/state   → g_obyvak_temp
homeassistant/sensor/obyvak/humidity/state      → g_obyvak_vlhk

homeassistant/weather/main/temperature/state    → g_weather_temp
homeassistant/weather/main/humidity/state       → g_weather_humidity
homeassistant/weather/main/pressure/state       → g_weather_pressure
homeassistant/weather/main/wind_speed/state     → g_weather_wind
homeassistant/weather/main/temp_min/state       → g_weather_min
homeassistant/weather/main/temp_max/state       → g_weather_max
homeassistant/weather/main/condition/state      → g_weather_state (sunny/cloudy/...)

homeassistant/light/obyvak/state                → g_light_obyvak (ON/OFF)
homeassistant/light/kuchyn/state                → g_light_kuchyn
homeassistant/light/loznice/state               → g_light_loznice
```

### Plánované entity (zatím nemapované — TODO)
- weather.forecast_domov → 5-day forecast pro PŘEDPOVĚĎ kartu (atributy: condition, temperature, templow, datetime)
- KLIMATE teploty: VENKU, PRACOVNA, OBÝVÁK, BOJLER, KOTEL (potřeba entity_id)
- Termostat target — klimat entity (např. climate.obyvak)

## Konfigurace v main.cpp

```cpp
#define ENABLE_WIFI_NTP   1   // WiFi + NTP (real time z internetu)
#define ENABLE_MQTT       0   // MQTT live data (zatím off)

#define WIFI_SSID         "TvojeSit"
#define WIFI_PASSWORD     "TvojeHeslo"
#define MQTT_SERVER       "192.168.1.x"   // IP HA / brokeru
#define MQTT_PORT         1883

#define NTP_SERVER1       "tik.cesnet.cz"
#define TZ_PRAGUE         "CET-1CEST,M3.5.0,M10.5.0/3"
```

## Co dál (TODO)

- [ ] Sepsat kompletní entity list z HA
- [ ] Aktivovat MQTT (`ENABLE_MQTT = 1` + topics dle entit)
- [ ] Publish weather.forecast_domov forecast přes HA automation (5 dní jako 10 topics)
- [ ] Publish KLIMATE teploty (5 sensors)
- [ ] Termostat publish — když user klikne +/- nebo mode, poslat MQTT command (climate.set_temperature, climate.set_hvac_mode)
- [ ] LIGHTS publish — toggle světlo přes MQTT (klikni na ON/OFF pill)
- [ ] ENERGY: graf spotřeby (lv_chart) napojený na HA recorder
- [ ] SYSTEM: real ESP32 stats (uptime, heap free z `ESP.getFreeHeap()`, RSSI z WiFi)

## Klíčové LVGL helpery v screens.c

```c
make_card(parent, x, y, w, h, accent_color)    // karta s barevným accent pruhem
card_title(card, text, color)                   // titulek karty
make_pb(parent, x, y, w, fill_w, fill_color)    // progress bar
make_nav_btn(parent, x, idx, text, active)      // nav button s eventem
draw_weather_icon(cont, state)                  // 60×60 weather icon
draw_weather_icon_mini(parent, x, y, state)     // 40×40 mini weather icon
icon_circle(parent, x, y, dia, color)           // kruh
icon_rect(parent, x, y, w, h, color, radius)    // obdélník
```

## Tap navigation

- `g_content_container` — swappable area mezi headerem a nav barem
- `nav_btn_event_cb(idx)` → `switch_to_screen(idx)` → `lv_obj_clean(g_content_container)` + `show_X_content()`
- `g_current_screen` (0=HOME) — index aktuální obrazovky
- `g_nav_buttons[5]` — pole odkazů na nav buttony pro update_nav_highlight()

## Auto-return na HOME

- `g_last_activity_tick` — `lv_tick_get()` při poslední aktivitě
- `screen_activity_cb` — LV_EVENT_PRESSED na celý screen → reset
- `auto_return_home_timer_cb` — LVGL timer 1×/s, kontrola elapsed > 30000 ms
- `AUTO_HOME_TIMEOUT_MS = 30000` (změň podle libosti)

## Termostat state (perzistuje mezi přepnutími)

```c
static float g_thermostat_target = 21.5f;  // current target temp
static int g_thermostat_mode = 0;           // 0=Topení 1=Chlazení 2=Auto 3=Vyp
```

## Rychlé troubleshooting

### Kompilace fail: `lv_obj_t` nedefinovaný / `objects_t` nezná
→ `include/screens.h` chybí nebo má starý obsah. Zkontroluj přes:
```powershell
Select-String -Path include\screens.h -Pattern "objects_t|lbl_weather_temp" | Measure
```
Mělo by vrátit Count > 30.

### Kompilace fail: `lvgl/lvgl.h: No such file or directory`
→ V některém z `.c`/`.h` souborů máš `#include "lvgl/lvgl.h"` místo `#include "lvgl.h"`.

### Soubor "useknutý" v editoru po stažení
→ Některé browsery cachují. Zkontroluj velikost přes `(Get-Item src\screens.c).Length` (mělo by být ~50 831 B). Pokud menší, použij split download.

### Touch nereaguje
→ GT911 driver v display.h. Zkontroluj že je `setup_display()` volán a `loop_display()` v main loopu.

## Klíčové barvy

```
C_BG_SCREEN     0x050d1a   pozadí screenu
C_BG_CARD       0x0d1f38   karty
C_ACCENT_BLUE   0x4a9eff   VENKU, DOMA logo
C_ACCENT_TEAL   0x00c8b4   OBÝVÁK, KLIMATE
C_ACCENT_ORANGE 0xffaa00   POČASÍ, ENERGY, max teploty
C_ACCENT_GREEN  0x22d46a   SVĚTLA, termostat target, online
C_ACCENT_PURPLE 0xa060ff   SYSTÉM, ESP32 info
0xfff5b8                   zlatavé hodiny
0xff6644                   horké teploty (KOTEL, mode "Topení")
```

---

**Verze**: 1.0  
**Build**: ~1021 řádků screens.c + 278 main.cpp + 89 screens.h
**Generováno**: spolupráce s Claude (Anthropic), 26. dubna 2026