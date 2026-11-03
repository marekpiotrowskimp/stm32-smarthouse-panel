/*
 * Edytor grup wirtualnych (§6.7, R-GRP-1, R-GRP-8, R-GRP-10) i układ ekranów grup – logika bez LVGL
 * (spec: virtual-groups). Kroki: typ -> członkowie -> nazwa -> pozycja; „Zapisz” na ostatnim kroku.
 */
#ifndef GROUP_EDITOR_H
#define GROUP_EDITOR_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "groups.h"

#define ED_MAX_DEVICES 64

typedef enum { ED_STEP_TYPE, ED_STEP_MEMBERS, ED_STEP_NAME, ED_STEP_POSITION } ed_step_t;

typedef struct {
    bool       active;
    int        group;        /* indeks edytowanej grupy w groups_t, -1 = nowa */
    uint8_t    group_id;     /* id edytowanej grupy (0 = nowa) */
    ed_step_t  step;
    bool       type_chosen;
    dev_type_t type;
    uint64_t   selected;     /* bit i = urządzenie i wybrane */
    uint8_t    missing;      /* członkowie edytowanej grupy spoza konfiguracji („brak urządzenia”) */
    char       name[GROUP_NAME_LEN];
    bool       name_edited;  /* użytkownik zmienił nazwę – bez nadpisywania propozycją */
    unsigned   position;
    bool       confirm_delete; /* okno „Usuń” / „Anuluj” */
} group_editor_t;

/* Nowa grupa: krok TYPE, nic nie wybrane, pozycja 0. */
void ed_begin_new(group_editor_t *ed);

/* Edycja istniejącej grupy gi: typ stały, krok MEMBERS, wybrani członkowie z konfiguracji, nazwa i pozycja grupy. */
void ed_begin_edit(group_editor_t *ed, const groups_t *gs, int gi, const dev_info_t *devs, size_t n);

/* Wybór typu – tylko dla nowej grupy w kroku TYPE; zmiana typu czyści wybór członków. */
bool ed_select_type(group_editor_t *ed, dev_type_t type);

/* Kandydaci kroku MEMBERS: indeksy urządzeń wybranego typu w kolejności devs (R-GRP-8). */
size_t ed_candidates(const group_editor_t *ed, const dev_info_t *devs, size_t n, int *out, size_t cap);

/* Przełącza członka; ignoruje urządzenie innego typu i 17. członka. */
void ed_toggle_member(group_editor_t *ed, const dev_info_t *devs, size_t n, size_t idx);
unsigned ed_member_count(const group_editor_t *ed);

/* Czy można przejść dalej: TYPE – typ wybrany; MEMBERS – 2..16 członków; NAME – niepusta nazwa; POSITION – zawsze. */
bool ed_can_next(const group_editor_t *ed);

/* Dalej: false, gdy nie wolno albo krok POSITION (tam jest „Zapisz”). Wejście w NAME bez edycji -> propozycja nazwy. */
bool ed_next(group_editor_t *ed);

/* Wstecz: poprzedni krok; z pierwszego kroku (TYPE nowej, MEMBERS edytowanej) -> active = false (porzucenie). */
void ed_back(group_editor_t *ed);

/* Propozycja nazwy: "Rolety (4)", "Światła (2)", "Gniazdka (3)". Semantyka snprintf. */
int ed_default_name(char *buf, size_t cap, dev_type_t type, unsigned count);

/* Dopisuje jeden znak UTF-8 (1..4 bajty), gdy nazwa zmieści się w 31 bajtach; true = dopisano. */
bool ed_name_append(group_editor_t *ed, const char *utf8_char);

/* Usuwa ostatni znak UTF-8 (cały, wielobajtowy). */
void ed_name_backspace(group_editor_t *ed);

/*
 * Zapis: nowa grupa dostaje groups_free_id i jest dopisywana na końcu gs; edytowana zachowuje id.
 * Członkowie = wybrane urządzenia w kolejności devs (brakujący są pomijani). false (gs bez zmian), gdy
 * kroki niekompletne albo nowa grupa przy limicie 16. Po zapisie active = false.
 */
bool ed_save(group_editor_t *ed, groups_t *gs, const dev_info_t *devs, size_t n);

/* Usunięcie edytowanej grupy (po potwierdzeniu); false dla nowej. Po usunięciu active = false. */
bool ed_delete(group_editor_t *ed, groups_t *gs);

/* ---------- Układ ekranów (współrzędne ekranu 480x272) ---------- */

/* Edytor – pełny ekran; pasek górny */
#define UI_ED_BACK_X   4    /* „Wstecz” */
#define UI_ED_NEXT_X   380  /* „Dalej” / „Zapisz” */
#define UI_ED_BTN_Y    4
#define UI_ED_BTN_W    96
#define UI_ED_BTN_H    32
#define UI_ED_DEL_X    4    /* „Usuń grupę” (tylko edycja) */
#define UI_ED_DEL_Y    236
#define UI_ED_DEL_W    120
#define UI_ED_DEL_H    32
#define UI_ED_CONFIRM_OK_X     120 /* okno potwierdzenia: „Usuń” */
#define UI_ED_CONFIRM_CANCEL_X 250 /* „Anuluj” */
#define UI_ED_CONFIRM_Y        150
#define UI_ED_CONFIRM_W        110
#define UI_ED_CONFIRM_H        40

/* Krok TYPE: Rolety / Światła / Gniazdka */
#define UI_ED_TYPE_X0   20
#define UI_ED_TYPE_STEP 150
#define UI_ED_TYPE_Y    80
#define UI_ED_TYPE_W    140
#define UI_ED_TYPE_H    80

/* Krok MEMBERS: siatka 2 x 6 pól wyboru (kandydat i -> kolumna i % 2, wiersz i / 2) */
#define UI_ED_CELL_X0   8
#define UI_ED_CELL_DX   236
#define UI_ED_CELL_Y0   48
#define UI_ED_CELL_DY   36
#define UI_ED_CELL_W    228
#define UI_ED_CELL_H    32

/* Krok POSITION: − / + */
#define UI_ED_POS_MINUS_X 140
#define UI_ED_POS_PLUS_X  280
#define UI_ED_POS_Y       100
#define UI_ED_POS_W       60
#define UI_ED_POS_H       60

/* Okno grupy (przytrzymanie kafelka grupy) – pełny ekran */
#define UI_GD_CLOSE_X  436
#define UI_GD_EDIT_X   330  /* „✎ Edytuj” */
#define UI_GD_TOP_Y    4
#define UI_GD_CLOSE_W  40
#define UI_GD_EDIT_W   100
#define UI_GD_TOP_H    32
#define UI_GD_CELL_X0  8    /* członkowie: siatka 3 x 4 pól wyboru (członek i -> kolumna i % 3, wiersz i / 3) */
#define UI_GD_CELL_DX  156
#define UI_GD_CELL_Y0  44
#define UI_GD_CELL_DY  30
#define UI_GD_CELL_W   152
#define UI_GD_CELL_H   28
#define UI_GD_BTN_X0   8    /* BLIND: ▲ ■ ▼; LIGHT / PLUG: przełącznik w slocie 0 */
#define UI_GD_BTN_STEP 120
#define UI_GD_BTN_Y    172
#define UI_GD_BTN_W    112
#define UI_GD_BTN_H    36
#define UI_GD_SLIDER1_Y 212 /* jasność / pozycja; tor jak UI_DLG_SLIDER_X / _W */
#define UI_GD_SLIDER2_Y 242 /* barwa (LIGHT) */
#define UI_GD_SLIDER_H  28

#endif /* GROUP_EDITOR_H */
