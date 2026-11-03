/*
 * Model ekranu panelu – logika niezależna od LVGL i sprzętu (docs/SPEC.md §4, §6; spec: display-tiles).
 * Kompilowany na hoście (testy) i na STM32.
 */
#ifndef UI_MODEL_H
#define UI_MODEL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "net_util.h"

/* ---------- Urządzenia ---------- */

typedef enum { DEV_TYPE_BLIND, DEV_TYPE_LIGHT, DEV_TYPE_PLUG } dev_type_t;

typedef struct {
    const char *name; /* UTF-8, wyświetlana nazwa */
    const char *id;   /* devId Tuya */
    dev_type_t  type;
} dev_info_t;

typedef enum { DEV_LINK_UNKNOWN, DEV_LINK_ONLINE, DEV_LINK_OFFLINE } dev_link_t;

typedef struct {
    dev_link_t link;
    bool       on;          /* LIGHT / PLUG */
    uint8_t    open_pct;    /* BLIND: 0 = zamknięta, 100 = otwarta */
    bool       moving;      /* BLIND */
    bool       opening;     /* BLIND w ruchu: true = otwieranie (DP1 "open"), false = zamykanie */
    uint16_t   brightness;  /* LIGHT: 10..1000 (DP22) */
    uint16_t   temp;        /* LIGHT: barwa 0 = ciepła .. 1000 = zimna (DP23) */
    bool       colour;      /* LIGHT: tryb koloru (DP21 "colour"); false = biel / inny */
    uint16_t   hue;         /* LIGHT: DP24 odcień 0..360 */
    uint16_t   sat;         /* LIGHT: DP24 nasycenie 0..1000 */
    uint16_t   val;         /* LIGHT: DP24 jasność koloru 0..1000 */
    int32_t    power_dw;    /* PLUG: moc w 0,1 W; < 0 = brak pomiaru (DP19) */
    int32_t    voltage_dv;  /* PLUG: napięcie w 0,1 V; < 0 = brak pomiaru (DP20) */
    int32_t    current_ma;  /* PLUG: prąd w mA; < 0 = brak pomiaru (DP18) */
} dev_state_t;

/* ---------- Układ ekranu 480x272 (§6.1) ---------- */

#define UI_SCREEN_W        480
#define UI_SCREEN_H        272
#define UI_BAR_H           28
#define UI_DOTS_H          20
#define UI_GRID_COLS       4
#define UI_GRID_ROWS       2
#define UI_TILES_PER_PAGE  (UI_GRID_COLS * UI_GRID_ROWS)
#define UI_TILE_W          112
#define UI_TILE_H          104
#define UI_TILE_GAP        6
#define UI_GRID_X          UI_TILE_GAP                  /* 6  */
#define UI_GRID_Y          (UI_BAR_H + UI_TILE_GAP)     /* 34 */

/* Kolory RGB565 (jedno źródło dla GUI i testów pikseli) */
#define UI_COLOR_BG        0x0862u /* #0A0D12 tło ekranu */
#define UI_COLOR_BAR       0x1107u /* #16213A pasek stanu */
#define UI_COLOR_TILE      0x2188u /* #253047 kafelek (wył. / zamknięty) */
#define UI_COLOR_TILE_DIM  0x10C4u /* #161B24 kafelek offline / stan nieznany */
#define UI_COLOR_TILE_BORDER 0x8CD7u /* #8A9BB8 ramka 2 px światła / gniazdka wył. (i „＋ Nowa grupa”) */
#define UI_COLOR_BORDER_BLIND_CLOSED 0x4E1Eu /* #4FC3F7 ramka 2 px rolety zamkniętej (niebieska) */
#define UI_COLOR_BORDER_BLIND_OPEN   0x5F0Fu /* #5EE07A ramka 2 px rolety otwartej / w ruchu (zielona) */
#define UI_COLOR_WARN      0xFD08u /* #FFA040 offline: ramka 2 px, znak i napis; napis „Brak reakcji” */

/* Tło aktywnego kafelka (wł. / otwarta / w ruchu) wg typu */
#define UI_COLOR_TILE_ON_BLIND 0x120Bu /* #12405A */
#define UI_COLOR_TILE_ON_LIGHT 0x49C2u /* #4A3A10 */
#define UI_COLOR_TILE_ON_PLUG  0x1225u /* #15452A */

/* ---------- Kafelek ---------- */

typedef enum {
    UI_ICON_BLIND_CLOSED,
    UI_ICON_BLIND_PARTIAL,
    UI_ICON_BLIND_OPEN,
    UI_ICON_BLIND_MOVING,
    UI_ICON_BULB_OFF,
    UI_ICON_BULB_ON,
    UI_ICON_PLUG_OFF,
    UI_ICON_PLUG_ON,
} ui_icon_t;

typedef struct {
    ui_icon_t icon;
    bool      dimmed;      /* tło UI_COLOR_TILE_DIM, ikona wyszarzona */
    uint8_t   glow_pct;    /* LIGHT wł.: intensywność poświaty 1..100, inaczej 0 */
    bool      warn;        /* napis stanu w kolorze UI_COLOR_WARN */
    bool      offline_mark; /* offline: ramka 2 px UI_COLOR_WARN i znak ostrzeżenia w rogu */
    char      status[24];  /* linia stanu pod nazwą, UTF-8 */
} ui_tile_view_t;

/*
 * Wygląd kafelka ze stanu urządzenia (§6.2):
 *   stan nieznany          -> ikona bazowa typu (zamknięta / wył.), dimmed, "Łączenie…"
 *   offline                -> ikona bazowa typu, dimmed, warn, offline_mark, "Offline"
 *   LIGHT wył. / wł.       -> BULB_OFF "Wył." / BULB_ON "Wł. NN%" (NN = (brightness+5)/10, 1..100)
 *   BLIND w ruchu          -> BLIND_MOVING "Otwieranie…" (opening) / "Zamykanie…"
 *   BLIND 0 / 100 / inne   -> BLIND_CLOSED "Zamknięta" / BLIND_OPEN "Otwarta" / BLIND_PARTIAL "Otwarta NN%"
 *   PLUG wył. / wł.        -> PLUG_OFF "Wył." / PLUG_ON "Wł. 23.4 W" (bez pomiaru: "Wł.")
 */
void ui_tile_view(dev_type_t type, const dev_state_t *st, ui_tile_view_t *out);

/* LIGHT: bieżąca jasność 0..1000 – val w trybie koloru, brightness (DP22) w trybie bieli. */
uint16_t ui_light_level(const dev_state_t *st);

/*
 * Brak reakcji (czerwony obrys, R-UI-8): warn = true; kafelek nieprzygaszony dostaje napis "Brak reakcji",
 * przygaszony (offline / łączenie) zachowuje napis. Ikona i pozostałe pola bez zmian.
 */
void ui_tile_view_error(ui_tile_view_t *v);

/* ---------- Strony (§6.6) ---------- */

/* Liczba stron dla n kafelków; zawsze >= 1 (pusta lista = jedna pusta strona). */
unsigned ui_page_count(size_t n_tiles);

/* Indeks kafelka na stronie page w miejscu slot (0..7) lub -1, gdy miejsce jest puste. */
int ui_tile_index(unsigned page, unsigned slot, size_t n_tiles);

/* Lewy górny róg miejsca slot (0..7, wierszami od lewej). */
void ui_tile_origin(unsigned slot, int *x, int *y);

/* ---------- Gesty (§6.3) ---------- */

typedef enum {
    UI_GESTURE_NONE,
    UI_GESTURE_TAP,         /* < 500 ms, ruch < 10 px */
    UI_GESTURE_LONG_PRESS,  /* >= 500 ms, ruch < 10 px */
    UI_GESTURE_SWIPE_LEFT,  /* dx <= -40 px i |dx| > |dy| -> następna strona */
    UI_GESTURE_SWIPE_RIGHT, /* dx >= 40 px i |dx| > |dy| -> poprzednia strona */
} ui_gesture_t;

ui_gesture_t ui_gesture_classify(uint32_t press_ms, int dx, int dy);

/* Strona po geście; bez zawijania (pierwsza/ostatnia strona zostaje). */
unsigned ui_page_after_gesture(unsigned page, unsigned page_count, ui_gesture_t g);

/* ---------- Pasek stanu ---------- */

/* "192.168.0.245" | "Łączenie…" (link bez IP) | "Brak sieci"; semantyka snprintf. */
int ui_net_label(char *buf, size_t cap, const net_status_t *st);

/* "1/2"; semantyka snprintf. */
int ui_page_label(char *buf, size_t cap, unsigned page, unsigned page_count);

#endif /* UI_MODEL_H */
