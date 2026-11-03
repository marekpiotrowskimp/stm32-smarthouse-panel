/*
 * Okna szczegółów (przytrzymanie kafelka) i potwierdzanie komend – logika niezależna od LVGL
 * (docs/SPEC.md §6.2–6.5, R-UI-3..8; spec: tile-details). Kompilowana na hoście (testy) i na STM32.
 */
#ifndef UI_DETAILS_H
#define UI_DETAILS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ui_model.h"

/* ---------- Czasy ---------- */

#define UI_PENDING_TIMEOUT_MS 3000u  /* brak potwierdzenia komendy -> powrót stanu */
#define UI_ERROR_SHOW_MS      2000u  /* czerwony obrys kafelka po braku potwierdzenia */
#define UI_DRAG_THROTTLE_MS   300u   /* max 1 komenda / 300 ms podczas przeciągania suwaka */
#define UI_DETAILS_IDLE_MS    30000u /* okno szczegółów zamyka się po 30 s bez dotyku */

/* ---------- Układ okna szczegółów (współrzędne ekranu) ---------- */

#define UI_DLG_X        20
#define UI_DLG_Y        20
#define UI_DLG_W        440
#define UI_DLG_H        232

#define UI_DLG_CLOSE_X  412  /* przycisk ✕ */
#define UI_DLG_CLOSE_Y  24
#define UI_DLG_CLOSE_W  44
#define UI_DLG_CLOSE_H  40

/* Rząd przycisków: LIGHT/PLUG – przełącznik w slocie 0; BLIND – ▲ Otwórz / ■ Stop / ▼ Zamknij w slotach 0/1/2 */
#define UI_DLG_BTN_X0   36
#define UI_DLG_BTN_STEP 140
#define UI_DLG_BTN_Y    68
#define UI_DLG_BTN_W    128
#define UI_DLG_BTN_H    44

/* Suwaki: wartość 0..100 % wzdłuż X; suwak 1 = jasność (LIGHT) / pozycja (BLIND), suwak 2 = barwa (LIGHT) */
#define UI_DLG_SLIDER_X  60
#define UI_DLG_SLIDER_W  360
#define UI_DLG_SLIDER1_Y 124
#define UI_DLG_SLIDER2_Y 164
#define UI_DLG_SLIDER_H  32

/* Paleta kolorów (LIGHT): UI_PALETTE_N pól */
#define UI_PALETTE_N     6
#define UI_DLG_PAL_X0    36
#define UI_DLG_PAL_STEP  68
#define UI_DLG_PAL_Y     204
#define UI_DLG_PAL_W     56
#define UI_DLG_PAL_H     36

/* Kolory RGB565 */
#define UI_COLOR_DIALOG  0x1906u /* #1C2333 tło okna szczegółów */
#define UI_COLOR_ERROR   0xE1C6u /* #E53935 obrys kafelka bez potwierdzenia */

/* Wartość suwaka 0..100 % dla punktu x (zaokrąglenie do najbliższej, obcięcie do zakresu). */
unsigned ui_slider_value(int x);

/* Jasność % (0..100) -> DP22 10..1000 (0 % = minimum 10). */
unsigned ui_brightness_from_pct(unsigned pct);

/* Odcień 0..359 pola palety i (0..UI_PALETTE_N-1): 0, 30, 60, 120, 240, 300; poza zakresem 0. */
unsigned ui_palette_hue(unsigned i);

/* ---------- Ograniczanie komend podczas przeciągania suwaka ---------- */

typedef struct {
    bool     active;
    bool     sent_any;
    uint32_t last_ms;   /* czas ostatniej wysłanej komendy */
    unsigned last_sent; /* ostatnia wysłana wartość */
} ui_drag_t;

void ui_drag_begin(ui_drag_t *d);

/*
 * Nowa wartość podczas przeciągania; true = wyślij teraz.
 * Pierwsza wartość zawsze; kolejne tylko gdy różne od ostatnio wysłanej i minęło >= UI_DRAG_THROTTLE_MS.
 */
bool ui_drag_move(ui_drag_t *d, uint32_t now, unsigned value);

/* Puszczenie suwaka; true = wyślij wartość końcową (różna od ostatnio wysłanej). Kończy przeciąganie. */
bool ui_drag_end(ui_drag_t *d, uint32_t now, unsigned value);

/* ---------- Stan optymistyczny po dotknięciu (§6.4, R-UI-8) ---------- */

/*
 * Stan spodziewany po komendzie jednego dotknięcia (tuya_dps_toggle):
 *   LIGHT / PLUG: on = !on; BLIND w ruchu: moving = false; BLIND inaczej: moving = true,
 *   opening = open_pct < 50 (jak komenda tuya_dps_toggle), pozycja bez zmian.
 * false (out bez zmian) dla urządzenia offline / o nieznanym stanie.
 */
bool ui_optimistic_state(dev_type_t type, const dev_state_t *cur, dev_state_t *out);

typedef struct {
    bool        active;      /* czeka na potwierdzenie – kafelek pokazuje target ze spinnerem */
    uint32_t    t0;
    dev_state_t prev;
    dev_state_t target;
    uint32_t    error_until; /* czerwony obrys do tej chwili (0 = brak) */
} ui_pending_t;

/* Początek oczekiwania; zastępuje poprzednie oczekiwanie i kasuje obrys błędu. */
void ui_pending_start(ui_pending_t *p, uint32_t now, const dev_state_t *prev, const dev_state_t *target);

/*
 * Nowy stan z urządzenia; true = potwierdzono (koniec oczekiwania, bez błędu). Potwierdza (gdy online):
 *   LIGHT / PLUG: actual.on == target.on
 *   BLIND: actual.moving == target.moving albo (dla ruchu) actual.open_pct != prev.open_pct
 */
bool ui_pending_confirm(ui_pending_t *p, dev_type_t type, const dev_state_t *actual);

/*
 * Upływ czasu (wołać cyklicznie); true = właśnie minął UI_PENDING_TIMEOUT_MS (koniec oczekiwania,
 * obrys błędu na UI_ERROR_SHOW_MS). Po wygaśnięciu obrysu zeruje error_until.
 */
bool ui_pending_tick(ui_pending_t *p, uint32_t now);

bool ui_pending_error(const ui_pending_t *p, uint32_t now);

/* Dotknięcie kafelka niedostępnego: koniec oczekiwania (active = false) i czerwony obrys na UI_ERROR_SHOW_MS. */
void ui_pending_reject(ui_pending_t *p, uint32_t now);

/* true, gdy okno szczegółów ma się zamknąć (>= UI_DETAILS_IDLE_MS od ostatniego dotyku). */
bool ui_details_should_close(uint32_t last_input_ms, uint32_t now);

/*
 * Odczyty gniazdka (tylko pomiary obecne, w osobnych liniach, separator '\n'):
 *   "Moc: 23.4 W\nNapięcie: 230.1 V\nPrąd: 0.105 A"; bez pomiarów "Brak pomiarów". Semantyka snprintf.
 */
int ui_plug_readout(char *buf, size_t cap, const dev_state_t *st);

#endif /* UI_DETAILS_H */
