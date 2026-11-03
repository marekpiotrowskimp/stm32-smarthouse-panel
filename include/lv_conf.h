/* Konfiguracja LVGL 9.2 dla STM32F746G-DISCO (480x272 RGB565). Pozostałe opcje: wartości domyślne LVGL. */
#ifndef LV_CONF_H
#define LV_CONF_H

#define LV_COLOR_DEPTH          16

/* Sterta LVGL w SDRAM (region MPU niecache'owany, app_display.c); bufor ramki LTDC leży pod 0xC0000000 */
#define LV_USE_STDLIB_MALLOC    LV_STDLIB_BUILTIN
#define LV_MEM_SIZE             (1024u * 1024u)
#define LV_MEM_ADR              0xC0100000u

/* Wątki obsługuje app_display.c (mutex FreeRTOS wokół wywołań LVGL) */
#define LV_USE_OS               LV_OS_NONE

#define LV_DEF_REFR_PERIOD      20
#define LV_DPI_DEF              130

/* Czcionki: bitmapowe Montserrat z polskimi znakami (lib/app_display/src/font_pl_*.c, tools/gen_font.py);
 * wbudowana 14 px jako fallback dla symboli LV_SYMBOL_* */
#define LV_FONT_MONTSERRAT_14   1
#define LV_FONT_DEFAULT         &lv_font_montserrat_14

#define LV_USE_LOG              0
#define LV_USE_PERF_MONITOR     0
#define LV_USE_MEM_MONITOR      0
#define LV_BUILD_EXAMPLES       0

#endif /* LV_CONF_H */
