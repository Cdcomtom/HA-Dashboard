/*
 * fonts_cz.h – deklarace custom LVGL fontů s českou diakritikou
 *
 * Použití v C/C++:
 *     #include "fonts_cz.h"
 *     lv_obj_set_style_text_font(obj, &montserrat_14_cz, LV_PART_MAIN | LV_STATE_DEFAULT);
 *
 * Pokrytí znaků:
 *   ASCII 0x20-0x7E (95 znaků)
 *   ° (U+00B0)
 *   Á É Í Ó Ú Ý  á é í ó ú ý      (Latin-1)
 *   Č Ď Ě Ň Ř Š Ť Ů Ž  č ď ě ň ř š ť ů ž   (Latin Extended-A)
 *
 * Soubory:
 *   montserrat_14_cz.c  – velikost 14 px, 4 bpp, ~5 KB bitmapa
 *   montserrat_20_cz.c  – velikost 20 px, 4 bpp, ~8 KB bitmapa
 *
 * Pozn.: glyph data jsou vyrenderovaná z Poppins-Regular (Montserrat
 * v sandboxu nebyl k dispozici). Geometricky je velmi blízká, vizuálně
 * se rozdíl pozná jen při srovnání vedle sebe.
 */
#ifndef FONTS_CZ_H
#define FONTS_CZ_H

#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl.h"

LV_FONT_DECLARE(montserrat_14_cz)
LV_FONT_DECLARE(montserrat_20_cz)

#ifdef __cplusplus
}
#endif

#endif /* FONTS_CZ_H */
