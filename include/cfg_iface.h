#pragma once
/*
 * cfg_iface.h – čisté C rozhraní pro screens.c
 * Stačí includnout tento soubor; config_manager.h není potřeba.
 */

#ifndef CFG_MAX_LIGHTS
#  define CFG_MAX_LIGHTS 6
#endif
#ifndef CFG_MAX_ENERGY
#  define CFG_MAX_ENERGY 6
#endif

const char* cfg_get_light_entity(int idx);
const char* cfg_get_light_name(int idx);
const char* cfg_get_thermostat_entity(void);

/* Energy */
const char* cfg_get_energy_entity(int idx);
const char* cfg_get_energy_name(int idx);
const char* cfg_get_energy_unit(int idx);

/* System info */
const char* sys_get_wifi_ssid(void);
const char* sys_get_wifi_ip(void);
void        sys_get_heap(uint32_t *free_kb, uint32_t *total_kb);
void        sys_get_uptime(uint32_t *days, uint32_t *hours, uint32_t *mins);

const char* cfg_get_energy_entity(int idx);
const char* cfg_get_energy_name(int idx);
const char* cfg_get_energy_unit(int idx);

const char* sys_get_wifi_ssid(void);
const char* sys_get_wifi_ip(void);
void        sys_get_heap(uint32_t *free_kb, uint32_t *total_kb);
void        sys_get_uptime(uint32_t *days, uint32_t *hours, uint32_t *mins);
