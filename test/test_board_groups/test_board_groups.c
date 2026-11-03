/*
 * Testy sprzętowe grup wirtualnych na ekranie (spec: virtual-groups, docs/SPEC.md §6.4–6.7,
 * R-GRP-1..5, 8..10, R-UI-7). Dotyk zadawany przez app_display_inject_touch; komendy trafiają do
 * handlera testowego (bez sieci i bez urządzeń), zapis grup – do handlera grup (bez flash).
 */
#include <stdio.h>
#include <string.h>
#include <unity.h>

#include "app_display.h"
#include "app_platform.h"
#include "group_editor.h"
#include "groups.h"
#include "stm32f7xx_hal.h"
#include "ui_details.h"
#include "ui_keyboard.h"
#include "ui_model.h"

#define RENDER_WAIT_MS 300u

enum { R1, R2, R3, R4, L1, L2, P1, N_DEV };

static const dev_info_t DEVS[N_DEV] = {
    { "Roleta 1", "id01", DEV_TYPE_BLIND }, { "Roleta 2", "id02", DEV_TYPE_BLIND },
    { "Roleta 3", "id03", DEV_TYPE_BLIND }, { "Roleta 4", "id04", DEV_TYPE_BLIND },
    { "Lampa 1", "id05", DEV_TYPE_LIGHT },  { "Lampa 2", "id06", DEV_TYPE_LIGHT },
    { "Gniazdo", "id07", DEV_TYPE_PLUG },
};
static dev_state_t STATES[N_DEV];
static groups_t start_groups;

static volatile unsigned tap_calls;
static volatile unsigned cmd_count;
static size_t cmd_idx[16];
static uint32_t cmd_ms[16];
static char cmds[16][64];
static volatile unsigned groups_calls;
static groups_t saved;

void setUp(void) {}
void tearDown(void) {}

static void on_tap(size_t idx)
{
    (void)idx;
    tap_calls++;
}

static void on_command(size_t idx, const char *dps)
{
    if (cmd_count < 16) {
        cmd_idx[cmd_count] = idx;
        cmd_ms[cmd_count] = app_millis();
        snprintf(cmds[cmd_count], sizeof cmds[0], "%s", dps);
    }
    cmd_count++;
}

static void on_groups(const groups_t *gs)
{
    saved = *gs;
    groups_calls++;
}

static void clear_commands(void)
{
    cmd_count = 0;
    memset(cmds, 0, sizeof cmds);
}

static uint16_t pixel(int x, int y)
{
    uint32_t fb = app_display_framebuffer();
    TEST_ASSERT_NOT_EQUAL_MESSAGE(0u, fb, "brak bufora ramki");
    volatile uint16_t *p = (volatile uint16_t *)(fb + (uint32_t)(y * UI_SCREEN_W + x) * 2u);
    SCB_InvalidateDCache_by_Addr((uint32_t *)((uint32_t)p & ~31u), 32);
    return *p;
}

static void tap(int x, int y)
{
    app_display_inject_touch(true, x, y);
    app_delay_ms(80);
    app_display_inject_touch(false, x, y);
    app_delay_ms(RENDER_WAIT_MS);
}

/* Pozycja kafelka na liście kafelków panelu (wg bieżących grup) albo -1 */
static int find_tile(tile_kind_t kind, unsigned index)
{
    static groups_t gs;
    tile_ref_t t[96];
    app_display_get_groups(&gs);
    size_t n = groups_tile_list(&gs, N_DEV, t, 96);
    for (size_t i = 0; i < n; i++)
        if (t[i].kind == kind && (kind == TILE_ADD || t[i].index == index))
            return (int)i;
    return -1;
}

static void tile_point(int pos, int *x, int *y)
{
    TEST_ASSERT_TRUE_MESSAGE(pos >= 0, "brak kafelka");
    app_display_show_page((unsigned)pos / UI_TILES_PER_PAGE);
    app_delay_ms(RENDER_WAIT_MS);
    ui_tile_origin((unsigned)pos % UI_TILES_PER_PAGE, x, y);
    *x += UI_TILE_W / 2;
    *y += UI_TILE_H / 2;
}

static void tap_tile(int pos)
{
    int x, y;
    tile_point(pos, &x, &y);
    tap(x, y);
}

static void long_press_tile(int pos)
{
    int x, y;
    tile_point(pos, &x, &y);
    app_display_inject_touch(true, x, y);
    app_delay_ms(700);
    app_display_inject_touch(false, x, y);
    app_delay_ms(RENDER_WAIT_MS);
}

/* Okno grupy */
static void gd_cell(unsigned i)
{
    tap(UI_GD_CELL_X0 + (int)(i % 3) * UI_GD_CELL_DX + UI_GD_CELL_W / 2,
        UI_GD_CELL_Y0 + (int)(i / 3) * UI_GD_CELL_DY + UI_GD_CELL_H / 2);
}

static void gd_button(unsigned n)
{
    tap(UI_GD_BTN_X0 + (int)n * UI_GD_BTN_STEP + UI_GD_BTN_W / 2, UI_GD_BTN_Y + UI_GD_BTN_H / 2);
}

static void gd_close(void)
{
    tap(UI_GD_CLOSE_X + UI_GD_CLOSE_W / 2, UI_GD_TOP_Y + UI_GD_TOP_H / 2);
}

/* Edytor */
static void ed_tap_next(void)
{
    tap(UI_ED_NEXT_X + UI_ED_BTN_W / 2, UI_ED_BTN_Y + UI_ED_BTN_H / 2);
}

static void ed_tap_back(void)
{
    tap(UI_ED_BACK_X + UI_ED_BTN_W / 2, UI_ED_BTN_Y + UI_ED_BTN_H / 2);
}

static void ed_tap_type(unsigned i)
{
    tap(UI_ED_TYPE_X0 + (int)i * UI_ED_TYPE_STEP + UI_ED_TYPE_W / 2, UI_ED_TYPE_Y + UI_ED_TYPE_H / 2);
}

static void ed_tap_cell(unsigned i)
{
    tap(UI_ED_CELL_X0 + (int)(i % 2) * UI_ED_CELL_DX + UI_ED_CELL_W / 2,
        UI_ED_CELL_Y0 + (int)(i / 2) * UI_ED_CELL_DY + UI_ED_CELL_H / 2);
}

static void key(unsigned row, unsigned col)
{
    tap((int)col * UI_KBD_KEY_W + UI_KBD_KEY_W / 2, UI_KBD_Y0 + (int)row * UI_KBD_KEY_H + UI_KBD_KEY_H / 2);
}

static int group_by_name(const groups_t *gs, const char *name)
{
    for (unsigned i = 0; i < gs->count; i++)
        if (strcmp(gs->g[i].name, name) == 0)
            return (int)i;
    return -1;
}

/* ---------- AC-10: kafelki grup ---------- */

void test_start_with_groups_shows_group_and_add_tiles(void)
{
    for (unsigned i = 0; i < N_DEV; i++)
        STATES[i] = (dev_state_t){ .link = DEV_LINK_ONLINE, .brightness = 500, .power_dw = -1, .voltage_dv = -1,
                                   .current_ma = -1 };
    STATES[R4].link = DEV_LINK_OFFLINE;
    STATES[L2].on = true;
    memset(&start_groups, 0, sizeof start_groups);
    start_groups.count = 1;
    start_groups.g[0] = (group_cfg_t){ .id = 1, .name = "Rolety", .type = DEV_TYPE_BLIND, .member_count = 4 };
    strcpy(start_groups.g[0].members[0], "id01");
    strcpy(start_groups.g[0].members[1], "id02");
    strcpy(start_groups.g[0].members[2], "id03");
    strcpy(start_groups.g[0].members[3], "id04");

    TEST_ASSERT_EQUAL_INT(0, app_display_start(DEVS, STATES, N_DEV));
    app_display_set_tap_handler(on_tap);
    app_display_set_command_handler(on_command);
    app_display_set_groups_handler(on_groups);
    app_display_set_groups(&start_groups);
    app_delay_ms(RENDER_WAIT_MS);

    TEST_ASSERT_EQUAL_UINT(2, app_display_page_count()); /* grupa + 7 urządzeń + „＋” = 9 */
    TEST_ASSERT_EQUAL_INT(0, find_tile(TILE_GROUP, 0));
    TEST_ASSERT_EQUAL_INT(8, find_tile(TILE_ADD, 0));
    int x, y;
    ui_tile_origin(0, &x, &y);
    TEST_ASSERT_EQUAL_HEX16(UI_COLOR_TILE, pixel(x + 8, y + UI_TILE_H / 2)); /* zamknięte, 3/4 online */
    long_press_tile(1);                                                    /* kafelek 1 = Roleta 1 */
    TEST_ASSERT_EQUAL_INT(R1, app_display_details_idx());
    tap(UI_DLG_CLOSE_X + UI_DLG_CLOSE_W / 2, UI_DLG_CLOSE_Y + UI_DLG_CLOSE_H / 2);
    TEST_ASSERT_EQUAL_INT(-1, app_display_details_idx());
}

/* ---------- AC-11: dotknięcie kafelka grupy (R-GRP-2..4, R-GRP-9) ---------- */

void test_group_tap_opens_online_members_within_100ms(void)
{
    clear_commands();
    unsigned taps = tap_calls;
    tap_tile(0);
    TEST_ASSERT_EQUAL_UINT(3, cmd_count); /* Roleta 4 offline – pominięta */
    TEST_ASSERT_EQUAL_UINT(R1, cmd_idx[0]);
    TEST_ASSERT_EQUAL_UINT(R2, cmd_idx[1]);
    TEST_ASSERT_EQUAL_UINT(R3, cmd_idx[2]);
    for (unsigned i = 0; i < 3; i++)
        TEST_ASSERT_EQUAL_STRING("{\"1\":\"open\"}", cmds[i]);
    TEST_ASSERT_LESS_OR_EQUAL_UINT32(100, cmd_ms[2] - cmd_ms[0]);
    TEST_ASSERT_EQUAL_UINT(taps, tap_calls); /* nie jest dotknięciem urządzenia */
}

void test_group_tap_closes_when_any_member_open(void)
{
    dev_state_t s = STATES[R2];
    s.open_pct = 60; /* control-polish AC-2: „otwarta” od 50% (było 30%) */
    app_display_set_state(R2, &s);
    app_delay_ms(RENDER_WAIT_MS);
    clear_commands();
    tap_tile(0);
    TEST_ASSERT_EQUAL_UINT(3, cmd_count);
    for (unsigned i = 0; i < 3; i++)
        TEST_ASSERT_EQUAL_STRING("{\"1\":\"close\"}", cmds[i]);
}

void test_group_tap_stops_when_any_member_moving(void)
{
    dev_state_t s = STATES[R1];
    s.moving = true;
    app_display_set_state(R1, &s);
    app_delay_ms(RENDER_WAIT_MS);
    clear_commands();
    tap_tile(0);
    TEST_ASSERT_EQUAL_UINT(3, cmd_count);
    for (unsigned i = 0; i < 3; i++)
        TEST_ASSERT_EQUAL_STRING("{\"1\":\"stop\"}", cmds[i]);
    app_display_set_state(R1, &STATES[R1]); /* z powrotem: zamknięte, bez ruchu */
    app_display_set_state(R2, &STATES[R2]);
    app_delay_ms(RENDER_WAIT_MS);
}

/* control-polish AC-2: członek uchylony poniżej 50% nie zamyka grupy */
void test_group_tap_opens_when_members_below_50(void)
{
    dev_state_t s = STATES[R2];
    s.open_pct = 30;
    app_display_set_state(R2, &s);
    app_delay_ms(RENDER_WAIT_MS);
    clear_commands();
    tap_tile(0);
    app_display_set_state(R2, &STATES[R2]); /* sprzątanie przed asercjami – kolejne testy bez zmian */
    app_delay_ms(RENDER_WAIT_MS);
    TEST_ASSERT_EQUAL_UINT(3, cmd_count);
    for (unsigned i = 0; i < 3; i++)
        TEST_ASSERT_EQUAL_STRING("{\"1\":\"open\"}", cmds[i]);
}

/* ---------- AC-12: okno grupy – sterowanie tylko zaznaczonymi (R-UI-7, R-GRP-5) ---------- */

void test_long_press_group_opens_group_details(void)
{
    long_press_tile(0);
    TEST_ASSERT_EQUAL_INT(0, app_display_group_details_idx());
    TEST_ASSERT_EQUAL_INT(-1, app_display_details_idx());
    TEST_ASSERT_EQUAL_INT(-1, app_display_editor_step());
}

void test_group_details_control_only_selected_members(void)
{
    gd_cell(0); /* odznacz Roleta 1 */
    gd_cell(1); /* odznacz Roleta 2 */
    clear_commands();
    gd_button(0); /* ▲ Otwórz */
    TEST_ASSERT_EQUAL_UINT(1, cmd_count); /* Roleta 3 (Roleta 4 offline) */
    TEST_ASSERT_EQUAL_UINT(R3, cmd_idx[0]);
    TEST_ASSERT_EQUAL_STRING("{\"1\":\"open\"}", cmds[0]);
    gd_cell(0); /* zaznacz Roleta 1 */
    clear_commands();
    gd_button(1); /* ■ Stop */
    TEST_ASSERT_EQUAL_UINT(2, cmd_count);
    TEST_ASSERT_EQUAL_UINT(R1, cmd_idx[0]);
    TEST_ASSERT_EQUAL_UINT(R3, cmd_idx[1]);
    TEST_ASSERT_EQUAL_STRING("{\"1\":\"stop\"}", cmds[1]);
    clear_commands();
    tap(UI_DLG_SLIDER_X + UI_DLG_SLIDER_W * 40 / 100, UI_GD_SLIDER1_Y + UI_GD_SLIDER_H / 2);
    TEST_ASSERT_EQUAL_UINT(2, cmd_count);
    TEST_ASSERT_EQUAL_STRING("{\"2\":40}", cmds[0]);
    TEST_ASSERT_EQUAL_STRING("{\"2\":40}", cmds[1]);
    static groups_t gs;
    app_display_get_groups(&gs);
    TEST_ASSERT_EQUAL_UINT8(4, gs.g[0].member_count); /* zaznaczenie nie zmienia składu grupy */
    for (unsigned m = 0; m < 4; m++)
        TEST_ASSERT_EQUAL_STRING(start_groups.g[0].members[m], gs.g[0].members[m]);
}

void test_group_details_close_button(void)
{
    gd_close();
    TEST_ASSERT_EQUAL_INT(-1, app_display_group_details_idx());
    TEST_ASSERT_EQUAL_UINT(0, groups_calls);
}

void test_light_group_brightness_to_all_members(void)
{
    static groups_t gs;
    gs = start_groups;
    gs.g[1] = (group_cfg_t){ .id = 2, .name = "Światła", .type = DEV_TYPE_LIGHT, .member_count = 2, .position = 1 };
    strcpy(gs.g[1].members[0], "id05");
    strcpy(gs.g[1].members[1], "id06");
    gs.count = 2;
    app_display_set_groups(&gs);
    app_delay_ms(RENDER_WAIT_MS);
    TEST_ASSERT_EQUAL_INT(1, find_tile(TILE_GROUP, 1));
    long_press_tile(1);
    TEST_ASSERT_EQUAL_INT(1, app_display_group_details_idx());
    clear_commands();
    tap(UI_DLG_SLIDER_X + UI_DLG_SLIDER_W / 2, UI_GD_SLIDER1_Y + UI_GD_SLIDER_H / 2); /* jasność 50 % */
    TEST_ASSERT_EQUAL_UINT(2, cmd_count); /* R-GRP-5 */
    TEST_ASSERT_EQUAL_UINT(L1, cmd_idx[0]);
    TEST_ASSERT_EQUAL_UINT(L2, cmd_idx[1]);
    TEST_ASSERT_EQUAL_STRING("{\"22\":500}", cmds[0]);
    TEST_ASSERT_EQUAL_STRING("{\"22\":500}", cmds[1]);
    gd_close();
}

/* control-polish AC-6: suwak jasności grupy – członek w trybie koloru dostaje DP24 z własnym odcieniem */
void test_light_group_brightness_keeps_member_colour(void)
{
    TEST_ASSERT_EQUAL_INT(1, find_tile(TILE_GROUP, 1)); /* „Światła” z poprzedniego testu */
    dev_state_t colour = STATES[L2];
    colour.colour = true;
    colour.hue = 120;
    colour.sat = 1000;
    colour.val = 800;
    app_display_set_state(L2, &colour);
    app_delay_ms(RENDER_WAIT_MS);
    long_press_tile(1);
    TEST_ASSERT_EQUAL_INT(1, app_display_group_details_idx());
    clear_commands();
    tap(UI_DLG_SLIDER_X + UI_DLG_SLIDER_W / 2, UI_GD_SLIDER1_Y + UI_GD_SLIDER_H / 2); /* jasność 50 % */
    gd_close(); /* sprzątanie przed asercjami – kolejne testy bez zmian */
    app_display_set_state(L2, &STATES[L2]);
    app_delay_ms(RENDER_WAIT_MS);
    TEST_ASSERT_EQUAL_UINT(2, cmd_count);
    TEST_ASSERT_EQUAL_UINT(L1, cmd_idx[0]);
    TEST_ASSERT_EQUAL_STRING("{\"22\":500}", cmds[0]);
    TEST_ASSERT_EQUAL_UINT(L2, cmd_idx[1]);
    TEST_ASSERT_EQUAL_STRING("{\"21\":\"colour\",\"24\":\"007803e801f4\"}", cmds[1]);
}

/* ---------- AC-13: edytor (R-GRP-1, R-GRP-8, R-GRP-10) ---------- */

void test_editor_creates_parter_group_at_first_position(void)
{
    tap_tile(find_tile(TILE_ADD, 0));
    TEST_ASSERT_EQUAL_INT(ED_STEP_TYPE, app_display_editor_step());
    ed_tap_type(0); /* Rolety */
    ed_tap_next();
    TEST_ASSERT_EQUAL_INT(ED_STEP_MEMBERS, app_display_editor_step());
    ed_tap_cell(0);
    ed_tap_cell(1);
    ed_tap_cell(2);
    ed_tap_next();
    TEST_ASSERT_EQUAL_INT(ED_STEP_NAME, app_display_editor_step());
    for (unsigned i = 0; i < 10; i++)
        key(1, 9); /* ⌫ – usuwa propozycję „Rolety (3)” */
    key(2, 9);     /* ⇧ – na jeden znak */
    key(0, 9);     /* P */
    key(2, 0);     /* a */
    key(0, 3);     /* r */
    key(0, 4);     /* t */
    key(0, 2);     /* e */
    key(0, 3);     /* r */
    ed_tap_next();
    TEST_ASSERT_EQUAL_INT(ED_STEP_POSITION, app_display_editor_step());
    ed_tap_next(); /* Zapisz (pozycja domyślna 0) */
    TEST_ASSERT_EQUAL_INT(-1, app_display_editor_step());
    TEST_ASSERT_EQUAL_UINT(1, groups_calls);
    TEST_ASSERT_EQUAL_UINT8(3, saved.count);
    int pi = group_by_name(&saved, "Parter");
    TEST_ASSERT_EQUAL_INT(2, pi);
    TEST_ASSERT_EQUAL_INT(DEV_TYPE_BLIND, saved.g[pi].type);
    TEST_ASSERT_EQUAL_UINT8(3, saved.g[pi].member_count); /* plakietka „3” */
    TEST_ASSERT_EQUAL_STRING("id01", saved.g[pi].members[0]);
    TEST_ASSERT_EQUAL_STRING("id03", saved.g[pi].members[2]);
    TEST_ASSERT_EQUAL_UINT8(0, saved.g[pi].position);
    TEST_ASSERT_EQUAL_INT(0, find_tile(TILE_GROUP, (unsigned)pi)); /* we wskazanym miejscu */
    long_press_tile(0);
    TEST_ASSERT_EQUAL_INT(pi, app_display_group_details_idx());
    gd_close();
}

void test_editor_light_type_lists_only_lights(void)
{
    tap_tile(find_tile(TILE_ADD, 0));
    ed_tap_type(1); /* Światła */
    ed_tap_next();
    ed_tap_cell(0);
    ed_tap_cell(1);
    ed_tap_cell(2); /* brak trzeciego kandydata – nic */
    ed_tap_next();
    ed_tap_next();
    ed_tap_next(); /* Zapisz */
    TEST_ASSERT_EQUAL_UINT(2, groups_calls);
    int li = group_by_name(&saved, "Światła (2)");
    TEST_ASSERT_TRUE(li >= 0);
    TEST_ASSERT_EQUAL_UINT8(2, saved.g[li].member_count); /* R-GRP-8: tylko LIGHT */
    TEST_ASSERT_EQUAL_STRING("id05", saved.g[li].members[0]);
    TEST_ASSERT_EQUAL_STRING("id06", saved.g[li].members[1]);
}

void test_edit_and_delete_group_with_confirmation(void)
{
    static groups_t gs; /* groups_t ma ok. 6,7 KB – nie na stosie zadania testowego */
    app_display_get_groups(&gs);
    int li = group_by_name(&gs, "Światła (2)");
    TEST_ASSERT_TRUE(li >= 0);
    long_press_tile(find_tile(TILE_GROUP, (unsigned)li));
    TEST_ASSERT_EQUAL_INT(li, app_display_group_details_idx());
    tap(UI_GD_EDIT_X + UI_GD_EDIT_W / 2, UI_GD_TOP_Y + UI_GD_TOP_H / 2); /* ✎ Edytuj */
    TEST_ASSERT_EQUAL_INT(ED_STEP_MEMBERS, app_display_editor_step());
    unsigned calls = groups_calls;
    tap(UI_ED_DEL_X + UI_ED_DEL_W / 2, UI_ED_DEL_Y + UI_ED_DEL_H / 2);
    tap(UI_ED_CONFIRM_CANCEL_X + UI_ED_CONFIRM_W / 2, UI_ED_CONFIRM_Y + UI_ED_CONFIRM_H / 2); /* Anuluj */
    TEST_ASSERT_EQUAL_INT(ED_STEP_MEMBERS, app_display_editor_step());
    TEST_ASSERT_EQUAL_UINT(calls, groups_calls);
    tap(UI_ED_DEL_X + UI_ED_DEL_W / 2, UI_ED_DEL_Y + UI_ED_DEL_H / 2);
    tap(UI_ED_CONFIRM_OK_X + UI_ED_CONFIRM_W / 2, UI_ED_CONFIRM_Y + UI_ED_CONFIRM_H / 2); /* Usuń */
    TEST_ASSERT_EQUAL_INT(-1, app_display_editor_step());
    TEST_ASSERT_EQUAL_UINT(calls + 1, groups_calls);
    TEST_ASSERT_EQUAL_UINT8(gs.count - 1, saved.count);
    TEST_ASSERT_EQUAL_INT(-1, group_by_name(&saved, "Światła (2)"));
    TEST_ASSERT_TRUE(find_tile(TILE_DEVICE, L1) >= 0); /* urządzenia bez zmian */
    TEST_ASSERT_TRUE(find_tile(TILE_DEVICE, L2) >= 0);
}

void test_back_from_first_step_abandons_new_group(void)
{
    unsigned calls = groups_calls;
    tap_tile(find_tile(TILE_ADD, 0));
    TEST_ASSERT_EQUAL_INT(ED_STEP_TYPE, app_display_editor_step());
    ed_tap_back();
    TEST_ASSERT_EQUAL_INT(-1, app_display_editor_step());
    TEST_ASSERT_EQUAL_UINT(calls, groups_calls);
}

void test_add_tile_inactive_at_16_groups(void)
{
    static groups_t before, full;
    memset(&full, 0, sizeof full);
    app_display_get_groups(&before);
    for (unsigned i = 0; i < GROUP_MAX; i++) {
        full.g[i] = start_groups.g[0];
        full.g[i].id = (uint8_t)(i + 1);
        full.g[i].position = 200;
    }
    full.count = GROUP_MAX;
    app_display_set_groups(&full);
    app_delay_ms(RENDER_WAIT_MS);
    tap_tile(find_tile(TILE_ADD, 0));
    TEST_ASSERT_EQUAL_INT(-1, app_display_editor_step()); /* „Maks. 16 grup” */
    app_display_set_groups(&before);
    app_delay_ms(RENDER_WAIT_MS);
}

void test_group_details_do_not_control_hidden_members(void)
{
    /* grupa 14 członków: 12 widocznych to brakujące urządzenia, ukryci 13. i 14. to obie lampy */
    static groups_t before, gs;
    app_display_get_groups(&before);
    gs = before;
    group_cfg_t *g = &gs.g[gs.count];
    *g = (group_cfg_t){ .id = 100, .name = "Duża", .type = DEV_TYPE_LIGHT, .position = 0 };
    for (unsigned m = 0; m < 12; m++)
        snprintf(g->members[m], GROUP_DEVID_LEN, "brak%02u", m);
    strcpy(g->members[12], "id05");
    strcpy(g->members[13], "id06");
    g->member_count = 14;
    gs.count++;
    app_display_set_groups(&gs);
    app_delay_ms(RENDER_WAIT_MS);
    int pos = find_tile(TILE_GROUP, gs.count - 1u);
    long_press_tile(pos);
    TEST_ASSERT_EQUAL_INT(gs.count - 1, app_display_group_details_idx());
    clear_commands();
    tap(UI_DLG_SLIDER_X + UI_DLG_SLIDER_W / 2, UI_GD_SLIDER1_Y + UI_GD_SLIDER_H / 2);
    TEST_ASSERT_EQUAL_UINT(0, cmd_count); /* ukryci członkowie nie dostają komend z okna */
    gd_close();
    clear_commands();
    tap_tile(pos);
    TEST_ASSERT_EQUAL_UINT(2, cmd_count); /* kafelek grupy steruje wszystkimi */
    app_display_set_groups(&before);
    app_delay_ms(RENDER_WAIT_MS);
}

/* control-polish AC-10: klawisz „123” – cyfry w nazwie grupy, „abc” – z powrotem litery */
void test_editor_digits_key_types_digits(void)
{
    static groups_t before;
    app_display_get_groups(&before);
    unsigned calls = groups_calls;
    tap_tile(find_tile(TILE_ADD, 0));
    TEST_ASSERT_EQUAL_INT(ED_STEP_TYPE, app_display_editor_step());
    ed_tap_type(0); /* Rolety */
    ed_tap_next();
    ed_tap_cell(0);
    ed_tap_cell(1);
    ed_tap_next();
    TEST_ASSERT_EQUAL_INT(ED_STEP_NAME, app_display_editor_step());
    for (unsigned i = 0; i < 12; i++)
        key(1, 9); /* ⌫ – usuwa propozycję nazwy (w obu układach na tym samym miejscu) */
    key(3, 7);     /* 123 */
    key(0, 0);     /* 1 */
    key(0, 1);     /* 2 */
    key(3, 7);     /* abc */
    key(2, 0);     /* a */
    ed_tap_next();
    TEST_ASSERT_EQUAL_INT(ED_STEP_POSITION, app_display_editor_step());
    ed_tap_next(); /* Zapisz */
    TEST_ASSERT_EQUAL_INT(-1, app_display_editor_step());
    TEST_ASSERT_EQUAL_UINT(calls + 1, groups_calls);
    TEST_ASSERT_TRUE(group_by_name(&saved, "12a") >= 0);
    app_display_set_groups(&before);
    app_delay_ms(RENDER_WAIT_MS);
}

static void test_task(void *arg)
{
    (void)arg;
    app_delay_ms(2000); /* czas na otwarcie portu szeregowego po resecie */
    UNITY_BEGIN();
    RUN_TEST(test_start_with_groups_shows_group_and_add_tiles);
    RUN_TEST(test_group_tap_opens_online_members_within_100ms);
    RUN_TEST(test_group_tap_closes_when_any_member_open);
    RUN_TEST(test_group_tap_stops_when_any_member_moving);
    RUN_TEST(test_group_tap_opens_when_members_below_50);
    RUN_TEST(test_long_press_group_opens_group_details);
    RUN_TEST(test_group_details_control_only_selected_members);
    RUN_TEST(test_group_details_close_button);
    RUN_TEST(test_light_group_brightness_to_all_members);
    RUN_TEST(test_light_group_brightness_keeps_member_colour);
    RUN_TEST(test_editor_creates_parter_group_at_first_position);
    RUN_TEST(test_editor_light_type_lists_only_lights);
    RUN_TEST(test_edit_and_delete_group_with_confirmation);
    RUN_TEST(test_back_from_first_step_abandons_new_group);
    RUN_TEST(test_add_tile_inactive_at_16_groups);
    RUN_TEST(test_group_details_do_not_control_hidden_members);
    RUN_TEST(test_editor_digits_key_types_digits);
    UNITY_END();
    for (;;)
        app_delay_ms(1000);
}

int main(void)
{
    app_board_init();
    app_rtos_start(test_task);
    return 0;
}
