/*
 * Wyświetlacz STM32F746G-DISCO: SDRAM, LTDC 480x272 RGB565, dotyk FT5336, GUI (spec: display-tiles).
 * GUI działa we własnym zadaniu FreeRTOS; funkcje wołane z innych zadań są bezpieczne wątkowo.
 */
#ifndef APP_DISPLAY_H
#define APP_DISPLAY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "groups.h"
#include "net_util.h"
#include "ui_model.h"

/*
 * Inicjalizuje SDRAM, LCD, podświetlenie, dotyk i GUI, tworzy zadanie GUI i rysuje stronę 1
 * z kafelkami devs[0..n-1] w stanach states[0..n-1] (devs musi żyć do końca programu,
 * states jest kopiowane; najwyżej 64 urządzenia).
 * Wymaga działającego schedulera FreeRTOS. 0 = OK.
 */
int app_display_start(const dev_info_t *devs, const dev_state_t *states, size_t n);

/* Adres bufora ramki aktualnie wyświetlanego przez LTDC (w SDRAM). 0 = brak. */
uint32_t app_display_framebuffer(void);

/* true, gdy kontroler dotyku FT5336 odpowiedział przy inicjalizacji. */
bool app_display_touch_ready(void);

/* Liczba zakończonych odświeżeń ekranu (rośnie, gdy GUI działa). */
uint32_t app_display_frame_count(void);

unsigned app_display_page(void);
unsigned app_display_page_count(void);

/* Przełącza stronę (poza zakresem -> bez zmian). */
void app_display_show_page(unsigned page);

/*
 * Podświetlenie 0..100% (programowy PWM ~500 Hz na LCD_BL_CTRL, krok 5%; 100 = stały stan wysoki).
 * Po app_display_start = 100. (spec: system-reliability)
 */
void app_display_set_backlight(uint8_t pct);
uint8_t app_display_backlight(void);

/*
 * Bezczynność (§6.6): po home_ms bez dotyku strona 1 (gdy brak otwartego okna / edytora), po dim_ms
 * podświetlenie UI_BACKLIGHT_DIM_PCT; pierwszy dotyk tylko wybudza. Domyślnie UI_IDLE_HOME_MS / UI_IDLE_DIM_MS.
 * Ustawienie czasów zaczyna nowy okres bezczynności (jak dotyk, ale bez wybudzania).
 */
void app_display_set_idle_timeouts(uint32_t home_ms, uint32_t dim_ms);

/* Aktualizuje pasek stanu (adres IP / brak sieci). */
void app_display_set_net_status(const net_status_t *st);

/*
 * Nowy stan urządzenia idx (kopiowany); kafelek jest przerysowany, jeśli jest na bieżącej stronie.
 * Zły indeks -> bez zmian. Bezpieczne wątkowo (spec: tuya-control).
 */
void app_display_set_state(size_t idx, const dev_state_t *st);

/* Wywoływane z zadania GUI po dotknięciu (TAP) kafelka urządzenia idx; NULL wyłącza. */
typedef void (*app_display_tap_handler_t)(size_t idx);
void app_display_set_tap_handler(app_display_tap_handler_t handler);

/* ---------- Okna szczegółów i potwierdzanie (spec: tile-details) ---------- */

/*
 * Komenda z okna szczegółów (JSON DP, np. {"22":500}) dla urządzenia idx.
 * Wywoływane z zadania GUI poza blokadą GUI; NULL wyłącza.
 */
typedef void (*app_display_command_handler_t)(size_t idx, const char *dps_json);
void app_display_set_command_handler(app_display_command_handler_t handler);

/*
 * Dotyk zadany programowo (testy, zdalne sterowanie): pressed = true przytrzymuje punkt (x, y)
 * zamiast odczytu FT5336, pressed = false zgłasza puszczenie w (x, y); potem znowu czytany jest FT5336.
 */
void app_display_inject_touch(bool pressed, int x, int y);

/* Indeks urządzenia z otwartym oknem szczegółów albo -1. */
int app_display_details_idx(void);

/*
 * Stan optymistyczny po wysłaniu komendy (§6.4, R-UI-8): kafelek idx od razu pokazuje target ze spinnerem;
 * set_state potwierdzające komendę kończy oczekiwanie, brak potwierdzenia w 3 s -> powrót do stanu
 * z set_state i czerwony obrys kafelka na 2 s. Zły indeks -> bez zmian.
 */
void app_display_begin_pending(size_t idx, const dev_state_t *target);

/* ---------- Grupy wirtualne (spec: virtual-groups) ---------- */

/*
 * Ustawia grupy (kopiowane) i włącza ich kafelki oraz kafelek „＋ Nowa grupa” (groups_tile_list).
 * Bez wywołania panel pokazuje tylko urządzenia (jak dotąd). Przerysowuje bieżącą stronę.
 *   dotknięcie kafelka grupy  -> komendy groups_toggle przez handler komend (po jednej na członka)
 *   przytrzymanie             -> okno grupy (pola wyboru członków, sterowanie jak dla typu, „✎ Edytuj”)
 *   dotknięcie „＋”           -> edytor nowej grupy (przy 16 grupach nieaktywny)
 */
void app_display_set_groups(const groups_t *gs);

/* Wywoływane z zadania GUI poza blokadą po „Zapisz” / „Usuń” w edytorze z nową listą grup. */
typedef void (*app_display_groups_handler_t)(const groups_t *gs);
void app_display_set_groups_handler(app_display_groups_handler_t handler);

/* Kopia bieżących grup. */
void app_display_get_groups(groups_t *out);

/* Indeks grupy z otwartym oknem grupy albo -1. */
int app_display_group_details_idx(void);

/* Bieżący krok edytora (ed_step_t) albo -1, gdy edytor zamknięty. */
int app_display_editor_step(void);

#endif /* APP_DISPLAY_H */
