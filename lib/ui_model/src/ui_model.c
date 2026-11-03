/* Model ekranu panelu – logika niezależna od LVGL i sprzętu (docs/SPEC.md §6). */
#include "ui_model.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TAP_MAX_MOVE_PX  10
#define LONG_PRESS_MS    500u
#define SWIPE_MIN_PX     40

static void set_status(ui_tile_view_t *out, const char *text)
{
    snprintf(out->status, sizeof out->status, "%s", text);
}

static ui_icon_t base_icon(dev_type_t type)
{
    switch (type) {
    case DEV_TYPE_LIGHT: return UI_ICON_BULB_OFF;
    case DEV_TYPE_PLUG:  return UI_ICON_PLUG_OFF;
    case DEV_TYPE_BLIND:
    default:             return UI_ICON_BLIND_CLOSED;
    }
}

static void light_view(const dev_state_t *st, ui_tile_view_t *out)
{
    if (!st->on) {
        out->icon = UI_ICON_BULB_OFF;
        set_status(out, "Wył.");
        return;
    }
    unsigned pct = (ui_light_level(st) + 5u) / 10u;
    if (pct < 1)
        pct = 1;
    if (pct > 100)
        pct = 100;
    out->icon = UI_ICON_BULB_ON;
    out->glow_pct = (uint8_t)pct;
    snprintf(out->status, sizeof out->status, "Wł. %u%%", pct);
}

static void blind_view(const dev_state_t *st, ui_tile_view_t *out)
{
    if (st->moving) {
        out->icon = UI_ICON_BLIND_MOVING;
        set_status(out, st->opening ? "Otwieranie…" : "Zamykanie…");
    } else if (st->open_pct == 0) {
        out->icon = UI_ICON_BLIND_CLOSED;
        set_status(out, "Zamknięta");
    } else if (st->open_pct >= 100) {
        out->icon = UI_ICON_BLIND_OPEN;
        set_status(out, "Otwarta");
    } else {
        out->icon = UI_ICON_BLIND_PARTIAL;
        snprintf(out->status, sizeof out->status, "Otwarta %u%%", (unsigned)st->open_pct);
    }
}

static void plug_view(const dev_state_t *st, ui_tile_view_t *out)
{
    if (!st->on) {
        out->icon = UI_ICON_PLUG_OFF;
        set_status(out, "Wył.");
        return;
    }
    out->icon = UI_ICON_PLUG_ON;
    if (st->power_dw < 0)
        set_status(out, "Wł.");
    else
        snprintf(out->status, sizeof out->status, "Wł. %ld.%ld W",
                 (long)(st->power_dw / 10), (long)(st->power_dw % 10));
}

void ui_tile_view(dev_type_t type, const dev_state_t *st, ui_tile_view_t *out)
{
    memset(out, 0, sizeof *out);

    if (st->link != DEV_LINK_ONLINE) {
        out->icon = base_icon(type);
        out->dimmed = true;
        if (st->link == DEV_LINK_OFFLINE) {
            out->warn = true;
            out->offline_mark = true;
            set_status(out, "Offline");
        } else {
            set_status(out, "Łączenie…");
        }
        return;
    }

    switch (type) {
    case DEV_TYPE_LIGHT: light_view(st, out); break;
    case DEV_TYPE_PLUG:  plug_view(st, out); break;
    case DEV_TYPE_BLIND:
    default:             blind_view(st, out); break;
    }
}

unsigned ui_page_count(size_t n_tiles)
{
    if (n_tiles == 0)
        return 1;
    return (unsigned)((n_tiles + UI_TILES_PER_PAGE - 1) / UI_TILES_PER_PAGE);
}

int ui_tile_index(unsigned page, unsigned slot, size_t n_tiles)
{
    if (slot >= UI_TILES_PER_PAGE)
        return -1;
    size_t idx = (size_t)page * UI_TILES_PER_PAGE + slot;
    return idx < n_tiles ? (int)idx : -1;
}

void ui_tile_origin(unsigned slot, int *x, int *y)
{
    unsigned col = slot % UI_GRID_COLS;
    unsigned row = slot / UI_GRID_COLS;
    *x = UI_GRID_X + (int)col * (UI_TILE_W + UI_TILE_GAP);
    *y = UI_GRID_Y + (int)row * (UI_TILE_H + UI_TILE_GAP);
}

ui_gesture_t ui_gesture_classify(uint32_t press_ms, int dx, int dy)
{
    int adx = abs(dx);
    int ady = abs(dy);

    if (adx < TAP_MAX_MOVE_PX && ady < TAP_MAX_MOVE_PX)
        return press_ms >= LONG_PRESS_MS ? UI_GESTURE_LONG_PRESS : UI_GESTURE_TAP;
    if (adx >= SWIPE_MIN_PX && adx > ady)
        return dx < 0 ? UI_GESTURE_SWIPE_LEFT : UI_GESTURE_SWIPE_RIGHT;
    return UI_GESTURE_NONE;
}

unsigned ui_page_after_gesture(unsigned page, unsigned page_count, ui_gesture_t g)
{
    if (g == UI_GESTURE_SWIPE_LEFT && page + 1 < page_count)
        return page + 1;
    if (g == UI_GESTURE_SWIPE_RIGHT && page > 0)
        return page - 1;
    return page;
}

int ui_net_label(char *buf, size_t cap, const net_status_t *st)
{
    if (!st->link_up)
        return snprintf(buf, cap, "Brak sieci");
    if (!st->has_ip)
        return snprintf(buf, cap, "Łączenie…");
    return snprintf(buf, cap, "%u.%u.%u.%u",
                    (unsigned)(st->ip >> 24), (unsigned)(st->ip >> 16) & 0xFFu,
                    (unsigned)(st->ip >> 8) & 0xFFu, (unsigned)st->ip & 0xFFu);
}

int ui_page_label(char *buf, size_t cap, unsigned page, unsigned page_count)
{
    return snprintf(buf, cap, "%u/%u", page + 1, page_count);
}

void ui_tile_view_error(ui_tile_view_t *v)
{
    v->warn = true;
    if (!v->dimmed)
        set_status(v, "Brak reakcji");
}

uint16_t ui_light_level(const dev_state_t *st)
{
    return st->colour ? st->val : st->brightness;
}
