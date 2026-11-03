/*
 * Bezczynność ekranu (docs/SPEC.md §6.6; spec: system-reliability): po 60 s bez dotyku powrót na stronę 1,
 * po 5 min przyciemnienie podświetlenia do 20%; pierwszy dotyk po przyciemnieniu tylko wybudza.
 * Czas w ms, odporny na przekręcenie licznika.
 */
#ifndef UI_IDLE_H
#define UI_IDLE_H

#include <stdbool.h>
#include <stdint.h>

#define UI_IDLE_HOME_MS      60000u
#define UI_IDLE_DIM_MS       300000u
#define UI_BACKLIGHT_DIM_PCT 20u

/* Zdarzenia z ui_idle_tick (maska bitowa) */
#define UI_IDLE_NONE    0u
#define UI_IDLE_GO_HOME 1u
#define UI_IDLE_DIM     2u

typedef struct {
    uint32_t last_input;
    bool     home_done; /* GO_HOME już zgłoszone w tym okresie bezczynności */
    bool     dimmed;
} ui_idle_t;

void ui_idle_init(ui_idle_t *p, uint32_t now);

/* Dotyk (każda próbka wciśnięcia): nowy okres bezczynności; true = dotyk tylko wybudza (był przyciemniony). */
bool ui_idle_touch(ui_idle_t *p, uint32_t now);

/* Zdarzenia, które właśnie nastąpiły: GO_HOME przy >= home_ms, DIM przy >= dim_ms – każde raz na okres. */
unsigned ui_idle_tick(ui_idle_t *p, uint32_t now, uint32_t home_ms, uint32_t dim_ms);

#endif /* UI_IDLE_H */
