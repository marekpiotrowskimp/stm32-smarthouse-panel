/* Bezczynność ekranu (spec: system-reliability, docs/SPEC.md §6.6). */
#include "ui_idle.h"

#include <string.h>

void ui_idle_init(ui_idle_t *p, uint32_t now)
{
    memset(p, 0, sizeof *p);
    p->last_input = now;
}

bool ui_idle_touch(ui_idle_t *p, uint32_t now)
{
    bool wake = p->dimmed;
    p->last_input = now;
    p->home_done = false;
    p->dimmed = false;
    return wake;
}

unsigned ui_idle_tick(ui_idle_t *p, uint32_t now, uint32_t home_ms, uint32_t dim_ms)
{
    uint32_t idle = now - p->last_input; /* modulo 2^32 – odporne na przekręcenie */
    unsigned ev = UI_IDLE_NONE;
    if (!p->home_done && idle >= home_ms) {
        p->home_done = true;
        ev |= UI_IDLE_GO_HOME;
    }
    if (!p->dimmed && idle >= dim_ms) {
        p->dimmed = true;
        ev |= UI_IDLE_DIM;
    }
    return ev;
}
