/* Okna szczegółów i potwierdzanie komend – logika niezależna od LVGL (spec: tile-details). */
#include "ui_details.h"

#include <stdio.h>

unsigned ui_slider_value(int x)
{
    if (x <= UI_DLG_SLIDER_X)
        return 0;
    if (x >= UI_DLG_SLIDER_X + UI_DLG_SLIDER_W)
        return 100;
    return (unsigned)(((x - UI_DLG_SLIDER_X) * 100 + UI_DLG_SLIDER_W / 2) / UI_DLG_SLIDER_W);
}

unsigned ui_brightness_from_pct(unsigned pct)
{
    if (pct > 100)
        pct = 100;
    return pct < 1 ? 10u : pct * 10u;
}

unsigned ui_palette_hue(unsigned i)
{
    static const unsigned hues[UI_PALETTE_N] = { 0, 30, 60, 120, 240, 300 };
    return i < UI_PALETTE_N ? hues[i] : 0u;
}

void ui_drag_begin(ui_drag_t *d)
{
    d->active = true;
    d->sent_any = false;
    d->last_ms = 0;
    d->last_sent = 0;
}

static bool drag_send(ui_drag_t *d, uint32_t now, unsigned value)
{
    d->sent_any = true;
    d->last_ms = now;
    d->last_sent = value;
    return true;
}

bool ui_drag_move(ui_drag_t *d, uint32_t now, unsigned value)
{
    if (!d->active)
        return false;
    if (!d->sent_any)
        return drag_send(d, now, value);
    if (value == d->last_sent || now - d->last_ms < UI_DRAG_THROTTLE_MS)
        return false;
    return drag_send(d, now, value);
}

bool ui_drag_end(ui_drag_t *d, uint32_t now, unsigned value)
{
    if (!d->active)
        return false;
    d->active = false;
    if (d->sent_any && value == d->last_sent)
        return false;
    return drag_send(d, now, value);
}

bool ui_optimistic_state(dev_type_t type, const dev_state_t *cur, dev_state_t *out)
{
    if (cur->link != DEV_LINK_ONLINE)
        return false;
    *out = *cur;
    if (type == DEV_TYPE_BLIND) {
        out->moving = !cur->moving;
        if (out->moving)
            out->opening = cur->open_pct < 50; /* jak tuya_dps_toggle: >= 50 -> "close" */
    }
    else
        out->on = !cur->on;
    return true;
}

void ui_pending_start(ui_pending_t *p, uint32_t now, const dev_state_t *prev, const dev_state_t *target)
{
    p->active = true;
    p->t0 = now;
    p->prev = *prev;
    p->target = *target;
    p->error_until = 0;
}

bool ui_pending_confirm(ui_pending_t *p, dev_type_t type, const dev_state_t *actual)
{
    if (!p->active || actual->link != DEV_LINK_ONLINE)
        return false;
    bool ok;
    if (type == DEV_TYPE_BLIND)
        /* zmiana pozycji potwierdza tylko ruch – roleta w ruchu raportuje pozycję także bez „stop” */
        ok = actual->moving == p->target.moving || (p->target.moving && actual->open_pct != p->prev.open_pct);
    else
        ok = actual->on == p->target.on;
    if (ok)
        p->active = false;
    return ok;
}

bool ui_pending_tick(ui_pending_t *p, uint32_t now)
{
    if (!p->active) {
        /* obrys wygasł – zerowanie, żeby po przekręceniu licznika ms (~24,8 dnia) nie wrócił */
        if (p->error_until != 0 && (int32_t)(p->error_until - now) <= 0)
            p->error_until = 0;
        return false;
    }
    if (now - p->t0 < UI_PENDING_TIMEOUT_MS)
        return false;
    ui_pending_reject(p, now); /* koniec oczekiwania + czerwony obrys */
    return true;
}

bool ui_pending_error(const ui_pending_t *p, uint32_t now)
{
    return p->error_until != 0 && (int32_t)(p->error_until - now) > 0;
}

void ui_pending_reject(ui_pending_t *p, uint32_t now)
{
    p->active = false;
    p->error_until = now + UI_ERROR_SHOW_MS;
    if (p->error_until == 0)
        p->error_until = 1; /* 0 = brak obrysu */
}

bool ui_details_should_close(uint32_t last_input_ms, uint32_t now)
{
    return now - last_input_ms >= UI_DETAILS_IDLE_MS;
}

int ui_plug_readout(char *buf, size_t cap, const dev_state_t *st)
{
    char tmp[96];
    int n = 0;
    if (st->power_dw >= 0)
        n += snprintf(tmp + n, sizeof tmp - (size_t)n, "Moc: %ld.%ld W", (long)(st->power_dw / 10),
                      (long)(st->power_dw % 10));
    if (st->voltage_dv >= 0)
        n += snprintf(tmp + n, sizeof tmp - (size_t)n, "%sNapięcie: %ld.%ld V", n ? "\n" : "",
                      (long)(st->voltage_dv / 10), (long)(st->voltage_dv % 10));
    if (st->current_ma >= 0)
        n += snprintf(tmp + n, sizeof tmp - (size_t)n, "%sPrąd: %ld.%03ld A", n ? "\n" : "",
                      (long)(st->current_ma / 1000), (long)(st->current_ma % 1000));
    return snprintf(buf, cap, "%s", n ? tmp : "Brak pomiarów");
}
