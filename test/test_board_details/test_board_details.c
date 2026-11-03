/*
 * Testy sprzętowe okien szczegółów i potwierdzania komend (spec: tile-details, docs/SPEC.md §6.3–6.5).
 * Dotyk zadawany przez app_display_inject_touch; sprawdzane są komendy przekazane do handlera
 * (bez sieci i bez urządzeń) oraz piksele bufora ramki w punktach z ui_details.h.
 */
#include <stdio.h>
#include <string.h>
#include <unity.h>

#include "app_display.h"
#include "app_platform.h"
#include "stm32f7xx_hal.h"
#include "ui_details.h"
#include "ui_model.h"

#define RENDER_WAIT_MS 300u

enum { IDX_LIGHT, IDX_BLIND, IDX_PLUG, IDX_LIGHT_OFF, IDX_BLIND2, N_DEV };

static const dev_info_t DEVS[N_DEV] = {
    { "Lampa", "id00000000000000000a", DEV_TYPE_LIGHT },
    { "Roleta", "id00000000000000000b", DEV_TYPE_BLIND },
    { "Gniazdo", "id00000000000000000c", DEV_TYPE_PLUG },
    { "Lampa offline", "id00000000000000000d", DEV_TYPE_LIGHT },
    { "Roleta 2", "id00000000000000000e", DEV_TYPE_BLIND },
};
static dev_state_t STATES[N_DEV];

/* Zapis wywołań handlerów */
static volatile unsigned tap_calls;
static volatile size_t tap_idx;
static volatile unsigned cmd_count;
static volatile size_t cmd_idx;
static char cmds[16][64];

void setUp(void) {}
void tearDown(void) {}

static void on_tap(size_t idx)
{
    tap_idx = idx;
    tap_calls++;
}

static void on_command(size_t idx, const char *dps)
{
    cmd_idx = idx;
    if (cmd_count < sizeof cmds / sizeof cmds[0])
        snprintf(cmds[cmd_count], sizeof cmds[0], "%s", dps);
    cmd_count++;
}

static void clear_commands(void)
{
    cmd_count = 0;
    memset(cmds, 0, sizeof cmds);
}

static const char *last_command(void)
{
    return cmd_count == 0 ? "" : cmds[(cmd_count - 1) % (sizeof cmds / sizeof cmds[0])];
}

static uint16_t pixel(int x, int y)
{
    uint32_t fb = app_display_framebuffer();
    TEST_ASSERT_NOT_EQUAL_MESSAGE(0u, fb, "brak bufora ramki");
    volatile uint16_t *p = (volatile uint16_t *)(fb + (uint32_t)(y * UI_SCREEN_W + x) * 2u);
    SCB_InvalidateDCache_by_Addr((uint32_t *)((uint32_t)p & ~31u), 32);
    return *p;
}

static void tile_center(unsigned slot, int *x, int *y)
{
    ui_tile_origin(slot, x, y);
    *x += UI_TILE_W / 2;
    *y += UI_TILE_H / 2;
}

/* Tło kafelka i jego lewa krawędź (obrys) w połowie wysokości */
static uint16_t tile_bg(unsigned slot)
{
    int x, y;
    ui_tile_origin(slot, &x, &y);
    return pixel(x + 8, y + UI_TILE_H / 2);
}

static uint16_t tile_edge(unsigned slot)
{
    int x, y;
    ui_tile_origin(slot, &x, &y);
    return pixel(x + 1, y + UI_TILE_H / 2);
}

/* Skrajny lewy piksel kafelka w połowie wysokości – obrys 1 px (tile-state-ux) */
static uint16_t tile_outline(unsigned slot)
{
    int x, y;
    ui_tile_origin(slot, &x, &y);
    return pixel(x, y + UI_TILE_H / 2);
}

/* Lewa krawędź ramy ikony okna rolety: ikona 40x40 na środku u góry (6 px od góry), rama 34x34 od (3,2)
 * z obrysem 2 px; zawartość kafelka przesunięta o ramkę kafelka 2 px (LVGL liczy pozycje od wnętrza ramki) */
static uint16_t blind_icon_frame(unsigned slot)
{
    int x, y;
    ui_tile_origin(slot, &x, &y);
    return pixel(x + 2 + (UI_TILE_W - 40) / 2 + 3, y + 2 + 6 + 2 + 17);
}

static void tap(int x, int y)
{
    app_display_inject_touch(true, x, y);
    app_delay_ms(80);
    app_display_inject_touch(false, x, y);
    app_delay_ms(RENDER_WAIT_MS);
}

static void long_press_tile(unsigned slot)
{
    int x, y;
    tile_center(slot, &x, &y);
    app_display_inject_touch(true, x, y);
    app_delay_ms(700);
    app_display_inject_touch(false, x, y);
    app_delay_ms(RENDER_WAIT_MS);
}

static void tap_button(unsigned n)
{
    tap(UI_DLG_BTN_X0 + (int)n * UI_DLG_BTN_STEP + UI_DLG_BTN_W / 2, UI_DLG_BTN_Y + UI_DLG_BTN_H / 2);
}

static int slider_x(unsigned pct)
{
    return UI_DLG_SLIDER_X + (int)(UI_DLG_SLIDER_W * pct / 100u);
}

static void tap_close(void)
{
    tap(UI_DLG_CLOSE_X + UI_DLG_CLOSE_W / 2, UI_DLG_CLOSE_Y + UI_DLG_CLOSE_H / 2);
}

/* ---------- przygotowanie ---------- */

void test_display_start_ok(void)
{
    STATES[IDX_LIGHT] = (dev_state_t){ .link = DEV_LINK_ONLINE, .on = true, .brightness = 500, .power_dw = -1,
                                       .voltage_dv = -1, .current_ma = -1 };
    STATES[IDX_BLIND] = (dev_state_t){ .link = DEV_LINK_ONLINE, .open_pct = 30, .power_dw = -1, .voltage_dv = -1,
                                       .current_ma = -1 };
    STATES[IDX_PLUG] = (dev_state_t){ .link = DEV_LINK_ONLINE, .on = true, .power_dw = 234, .voltage_dv = 2301,
                                      .current_ma = 105 };
    STATES[IDX_LIGHT_OFF] = (dev_state_t){ .link = DEV_LINK_OFFLINE, .power_dw = -1, .voltage_dv = -1,
                                           .current_ma = -1 };
    STATES[IDX_BLIND2] = (dev_state_t){ .link = DEV_LINK_ONLINE, .open_pct = 0, .power_dw = -1, .voltage_dv = -1,
                                        .current_ma = -1 };
    TEST_ASSERT_EQUAL_INT(0, app_display_start(DEVS, STATES, N_DEV));
    app_display_set_tap_handler(on_tap);
    app_display_set_command_handler(on_command);
    app_delay_ms(RENDER_WAIT_MS);
    TEST_ASSERT_EQUAL_INT(-1, app_display_details_idx());
}

/* ---------- tile-state-ux: ramka 2 px w kolorze stanu, offline bursztyn ---------- */

void test_tile_frames_show_state_and_availability(void)
{
    TEST_ASSERT_EQUAL_HEX16(UI_COLOR_TILE, tile_bg(IDX_BLIND2));                     /* zamknięta – tło bez zmian */
    TEST_ASSERT_EQUAL_HEX16(UI_COLOR_BORDER_BLIND_CLOSED, tile_outline(IDX_BLIND2)); /* niebieska ramka */
    TEST_ASSERT_EQUAL_HEX16(UI_COLOR_BORDER_BLIND_CLOSED, tile_edge(IDX_BLIND2));    /* 2 px */
    TEST_ASSERT_EQUAL_HEX16(UI_COLOR_TILE_ON_BLIND, tile_bg(IDX_BLIND));             /* otwarta 30% */
    TEST_ASSERT_EQUAL_HEX16(UI_COLOR_BORDER_BLIND_OPEN, tile_outline(IDX_BLIND));    /* zielona ramka */
    TEST_ASSERT_EQUAL_HEX16(UI_COLOR_TILE_DIM, tile_bg(IDX_LIGHT_OFF));              /* offline – tło bez zmian */
    TEST_ASSERT_EQUAL_HEX16(UI_COLOR_WARN, tile_outline(IDX_LIGHT_OFF));             /* bursztynowa ramka */
    TEST_ASSERT_EQUAL_HEX16(UI_COLOR_WARN, tile_edge(IDX_LIGHT_OFF));                /* 2 px */
}

/* control-polish AC-1: ikona rolety w kolorze ramki */
void test_blind_icon_matches_frame_colour(void)
{
    TEST_ASSERT_EQUAL_HEX16(UI_COLOR_BORDER_BLIND_CLOSED, blind_icon_frame(IDX_BLIND2)); /* zamknięta: niebieska */
    TEST_ASSERT_EQUAL_HEX16(UI_COLOR_BORDER_BLIND_OPEN, blind_icon_frame(IDX_BLIND));    /* otwarta 30%: zielona */
}

/* ---------- AC-7: dotknięcie i przytrzymanie kafelka (§6.3) ---------- */

void test_tap_on_tile_calls_tap_handler_with_index(void)
{
    int x, y;
    tile_center(IDX_PLUG, &x, &y);
    tap(x, y);
    TEST_ASSERT_EQUAL_UINT(1, tap_calls);
    TEST_ASSERT_EQUAL_UINT(IDX_PLUG, tap_idx);
    TEST_ASSERT_EQUAL_INT(-1, app_display_details_idx());
}

/* tile-state-ux AC-9: dotknięcie kafelka offline – bez komendy, czerwony obrys na 2 s */
void test_tap_on_offline_tile_flashes_red_without_tap_handler(void)
{
    unsigned calls = tap_calls;
    int x, y;
    tile_center(IDX_LIGHT_OFF, &x, &y);
    tap(x, y);
    TEST_ASSERT_EQUAL_UINT(calls, tap_calls);                             /* handler nie wołany */
    TEST_ASSERT_EQUAL_INT(-1, app_display_details_idx());
    TEST_ASSERT_EQUAL_HEX16(UI_COLOR_ERROR, tile_edge(IDX_LIGHT_OFF));    /* czerwony obrys */
    app_delay_ms(2200);
    TEST_ASSERT_EQUAL_HEX16(UI_COLOR_WARN, tile_outline(IDX_LIGHT_OFF));  /* po 2 s z powrotem bursztyn */
    TEST_ASSERT_NOT_EQUAL(UI_COLOR_ERROR, tile_edge(IDX_LIGHT_OFF));
}

void test_long_press_opens_details_before_release(void)
{
    int x, y;
    tile_center(IDX_LIGHT, &x, &y);
    unsigned taps = tap_calls;
    app_display_inject_touch(true, x, y);
    app_delay_ms(700);
    TEST_ASSERT_EQUAL_INT(IDX_LIGHT, app_display_details_idx()); /* otwarte jeszcze przed puszczeniem */
    app_display_inject_touch(false, x, y);
    app_delay_ms(RENDER_WAIT_MS);
    TEST_ASSERT_EQUAL_UINT(taps, tap_calls); /* przytrzymanie nie przełącza urządzenia */
    TEST_ASSERT_EQUAL_INT(IDX_LIGHT, app_display_details_idx());
    TEST_ASSERT_EQUAL_HEX16(UI_COLOR_DIALOG, pixel(UI_DLG_X + 8, UI_DLG_Y + UI_DLG_H - 8));
}

/* ---------- AC-8: okno żarówki (R-UI-6) ---------- */

void test_light_switch_shows_state_and_follows_set_state(void)
{
    int bx = UI_DLG_BTN_X0 + 6, by = UI_DLG_BTN_Y + UI_DLG_BTN_H / 2;
    TEST_ASSERT_EQUAL_INT(IDX_LIGHT, app_display_details_idx()); /* piksel należy do okna, nie do kafelka pod nim */
    TEST_ASSERT_EQUAL_HEX16(UI_COLOR_TILE_ON_LIGHT, pixel(bx, by)); /* wł. */
    dev_state_t off = STATES[IDX_LIGHT];
    off.on = false;
    app_display_set_state(IDX_LIGHT, &off);
    app_delay_ms(RENDER_WAIT_MS);
    TEST_ASSERT_EQUAL_HEX16(UI_COLOR_TILE, pixel(bx, by)); /* okno odświeża się na żywo */
}

void test_light_switch_tap_sends_opposite_state(void)
{
    clear_commands();
    tap_button(0);
    TEST_ASSERT_EQUAL_UINT(1, cmd_count);
    TEST_ASSERT_EQUAL_UINT(IDX_LIGHT, cmd_idx);
    TEST_ASSERT_EQUAL_STRING("{\"20\":true}", last_command());
}

void test_brightness_tap_sends_one_dp22(void)
{
    clear_commands();
    tap(slider_x(50), UI_DLG_SLIDER1_Y + UI_DLG_SLIDER_H / 2);
    TEST_ASSERT_EQUAL_UINT(1, cmd_count);
    TEST_ASSERT_EQUAL_STRING("{\"22\":500}", last_command());
}

void test_brightness_drag_is_throttled_and_ends_with_final_value(void)
{
    clear_commands();
    int y = UI_DLG_SLIDER1_Y + UI_DLG_SLIDER_H / 2;
    for (unsigned i = 0; i <= 20; i++) { /* 0 -> 100 % w ok. 1 s */
        app_display_inject_touch(true, slider_x(i * 5u), y);
        app_delay_ms(50);
    }
    app_display_inject_touch(false, slider_x(100), y);
    app_delay_ms(RENDER_WAIT_MS);
    TEST_ASSERT_GREATER_OR_EQUAL_UINT(2, cmd_count);
    TEST_ASSERT_LESS_OR_EQUAL_UINT(5, cmd_count); /* max 1 / 300 ms + wartość końcowa */
    TEST_ASSERT_EQUAL_STRING("{\"22\":10}", cmds[0]);
    TEST_ASSERT_EQUAL_STRING("{\"22\":1000}", last_command());
}

void test_temperature_tap_sends_white_mode(void)
{
    clear_commands();
    tap(slider_x(0), UI_DLG_SLIDER2_Y + UI_DLG_SLIDER_H / 2);
    TEST_ASSERT_EQUAL_UINT(1, cmd_count);
    TEST_ASSERT_EQUAL_STRING("{\"21\":\"white\",\"23\":0}", last_command());
}

void test_palette_swatches_send_colour(void)
{
    clear_commands();
    int y = UI_DLG_PAL_Y + UI_DLG_PAL_H / 2;
    /* control-polish AC-7: V = bieżąca jasność żarówki (biel, DP22 = 500), było 1000 */
    tap(UI_DLG_PAL_X0 + UI_DLG_PAL_W / 2, y); /* pole 0: czerwony */
    TEST_ASSERT_EQUAL_STRING("{\"21\":\"colour\",\"24\":\"000003e801f4\"}", last_command());
    tap(UI_DLG_PAL_X0 + 4 * UI_DLG_PAL_STEP + UI_DLG_PAL_W / 2, y); /* pole 4: niebieski (240) */
    TEST_ASSERT_EQUAL_STRING("{\"21\":\"colour\",\"24\":\"00f003e801f4\"}", last_command());
    TEST_ASSERT_EQUAL_UINT(2, cmd_count);
}

/* control-polish AC-5: w trybie koloru suwak jasności zmienia V koloru (DP24), nie DP22 */
void test_colour_mode_brightness_sends_dp24(void)
{
    dev_state_t colour = STATES[IDX_LIGHT];
    colour.on = true;
    colour.colour = true;
    colour.hue = 240;
    colour.sat = 500;
    colour.val = 300;
    app_display_set_state(IDX_LIGHT, &colour);
    app_delay_ms(RENDER_WAIT_MS);
    clear_commands();
    tap(slider_x(50), UI_DLG_SLIDER1_Y + UI_DLG_SLIDER_H / 2);
    dev_state_t off = STATES[IDX_LIGHT]; /* sprzątanie przed asercjami: jak po test_light_switch_shows_state… */
    off.on = false;
    app_display_set_state(IDX_LIGHT, &off);
    app_delay_ms(RENDER_WAIT_MS);
    TEST_ASSERT_EQUAL_UINT(1, cmd_count);
    TEST_ASSERT_EQUAL_STRING("{\"21\":\"colour\",\"24\":\"00f001f401f4\"}", last_command());
}

void test_tile_under_open_dialog_is_not_tapped(void)
{
    TEST_ASSERT_EQUAL_INT(IDX_LIGHT, app_display_details_idx());
    unsigned taps = tap_calls;
    clear_commands();
    int x, y;
    tile_center(IDX_BLIND, &x, &y); /* kafelek 1 leży pod oknem, w pustym miejscu okna żarówki */
    tap(x, y);
    TEST_ASSERT_EQUAL_UINT(taps, tap_calls);
    TEST_ASSERT_EQUAL_UINT(0, cmd_count);
    TEST_ASSERT_EQUAL_INT(IDX_LIGHT, app_display_details_idx());
}

void test_device_going_offline_disables_open_dialog(void)
{
    dev_state_t off = STATES[IDX_LIGHT];
    off.on = false;
    dev_state_t offline = off;
    offline.link = DEV_LINK_OFFLINE;
    app_display_set_state(IDX_LIGHT, &offline);
    app_delay_ms(RENDER_WAIT_MS);
    clear_commands();
    tap_button(0);
    tap(slider_x(50), UI_DLG_SLIDER1_Y + UI_DLG_SLIDER_H / 2);
    TEST_ASSERT_EQUAL_UINT(0, cmd_count);
    TEST_ASSERT_EQUAL_INT(IDX_LIGHT, app_display_details_idx()); /* okno zostaje w wariancie offline */
    app_display_set_state(IDX_LIGHT, &off);                     /* powrót: online, wył. */
    app_delay_ms(RENDER_WAIT_MS);
    tap_button(0);
    TEST_ASSERT_EQUAL_STRING("{\"20\":true}", last_command());
}

/* ---------- AC-9: zamykanie okna (§6.5) ---------- */

void test_close_button_closes_details(void)
{
    TEST_ASSERT_EQUAL_INT(IDX_LIGHT, app_display_details_idx());
    tap_close();
    TEST_ASSERT_EQUAL_INT(-1, app_display_details_idx());
    TEST_ASSERT_EQUAL_HEX16(UI_COLOR_TILE, tile_bg(IDX_LIGHT)); /* znowu widać kafelki (żarówka wył.) */
}

void test_tap_outside_closes_without_tile_action(void)
{
    long_press_tile(IDX_LIGHT);
    TEST_ASSERT_EQUAL_INT(IDX_LIGHT, app_display_details_idx());
    unsigned taps = tap_calls;
    clear_commands();
    tap(10, 140); /* kafelek 0, ale poza oknem (x < UI_DLG_X) */
    TEST_ASSERT_EQUAL_INT(-1, app_display_details_idx());
    TEST_ASSERT_EQUAL_UINT(taps, tap_calls);
    TEST_ASSERT_EQUAL_UINT(0, cmd_count);
}

/* ---------- AC-10: okno rolety ---------- */

void test_blind_buttons_and_position_slider(void)
{
    long_press_tile(IDX_BLIND);
    TEST_ASSERT_EQUAL_INT(IDX_BLIND, app_display_details_idx());
    clear_commands();
    tap_button(0);
    TEST_ASSERT_EQUAL_STRING("{\"1\":\"open\"}", last_command());
    tap_button(1);
    TEST_ASSERT_EQUAL_STRING("{\"1\":\"stop\"}", last_command());
    tap_button(2);
    TEST_ASSERT_EQUAL_STRING("{\"1\":\"close\"}", last_command());
    tap(slider_x(40), UI_DLG_SLIDER1_Y + UI_DLG_SLIDER_H / 2);
    TEST_ASSERT_EQUAL_STRING("{\"2\":40}", last_command());
    TEST_ASSERT_EQUAL_UINT(4, cmd_count);
    TEST_ASSERT_EQUAL_UINT(IDX_BLIND, cmd_idx);
    tap_close();
}

/* ---------- AC-11: okno gniazdka ---------- */

void test_plug_switch_sends_off(void)
{
    long_press_tile(IDX_PLUG);
    TEST_ASSERT_EQUAL_INT(IDX_PLUG, app_display_details_idx());
    clear_commands();
    tap_button(0);
    TEST_ASSERT_EQUAL_UINT(1, cmd_count);
    TEST_ASSERT_EQUAL_STRING("{\"1\":false}", last_command());
    tap_close();
}

/* ---------- AC-12: urządzenie offline ---------- */

void test_offline_details_have_no_controls(void)
{
    long_press_tile(IDX_LIGHT_OFF);
    TEST_ASSERT_EQUAL_INT(IDX_LIGHT_OFF, app_display_details_idx()); /* okno „Urządzenie offline” */
    clear_commands();
    tap_button(0);
    tap(slider_x(50), UI_DLG_SLIDER1_Y + UI_DLG_SLIDER_H / 2);
    tap(slider_x(50), UI_DLG_SLIDER2_Y + UI_DLG_SLIDER_H / 2);
    tap(UI_DLG_PAL_X0 + UI_DLG_PAL_W / 2, UI_DLG_PAL_Y + UI_DLG_PAL_H / 2);
    TEST_ASSERT_EQUAL_UINT(0, cmd_count);
    TEST_ASSERT_EQUAL_INT(IDX_LIGHT_OFF, app_display_details_idx());
    tap_close();
    TEST_ASSERT_EQUAL_INT(-1, app_display_details_idx());
}

/* ---------- AC-13: stan optymistyczny i powrót (R-UI-8) ---------- */

void test_pending_shows_target_then_reverts_with_red_outline(void)
{
    TEST_ASSERT_EQUAL_HEX16(UI_COLOR_TILE, tile_bg(IDX_BLIND2)); /* zamknięta */
    dev_state_t target = STATES[IDX_BLIND2];
    target.moving = true;
    app_display_begin_pending(IDX_BLIND2, &target);
    app_delay_ms(200);
    TEST_ASSERT_EQUAL_HEX16(UI_COLOR_TILE_ON_BLIND, tile_bg(IDX_BLIND2)); /* od razu „w ruchu” */
    app_delay_ms(3100);                                                   /* > 3 s bez potwierdzenia */
    TEST_ASSERT_EQUAL_HEX16(UI_COLOR_TILE, tile_bg(IDX_BLIND2));          /* powrót */
    TEST_ASSERT_EQUAL_HEX16(UI_COLOR_ERROR, tile_edge(IDX_BLIND2));       /* czerwony obrys */
    app_delay_ms(2200);
    TEST_ASSERT_NOT_EQUAL(UI_COLOR_ERROR, tile_edge(IDX_BLIND2));         /* po 2 s znika */
}

void test_pending_confirmed_keeps_state_without_outline(void)
{
    dev_state_t target = STATES[IDX_LIGHT];
    target.on = true;
    dev_state_t off = target;
    off.on = false;
    app_display_set_state(IDX_LIGHT, &off);
    app_delay_ms(RENDER_WAIT_MS);
    TEST_ASSERT_EQUAL_HEX16(UI_COLOR_TILE, tile_bg(IDX_LIGHT)); /* wył. */
    app_display_begin_pending(IDX_LIGHT, &target);
    app_delay_ms(300);
    TEST_ASSERT_EQUAL_HEX16(UI_COLOR_TILE_ON_LIGHT, tile_bg(IDX_LIGHT)); /* od razu „wł.” */
    app_display_set_state(IDX_LIGHT, &target); /* push z urządzenia */
    app_delay_ms(3300);
    TEST_ASSERT_EQUAL_HEX16(UI_COLOR_TILE_ON_LIGHT, tile_bg(IDX_LIGHT));
    TEST_ASSERT_NOT_EQUAL(UI_COLOR_ERROR, tile_edge(IDX_LIGHT));
}

void test_pending_bad_index_is_ignored(void)
{
    dev_state_t target = STATES[IDX_LIGHT];
    uint32_t f0 = app_display_frame_count();
    app_display_begin_pending(N_DEV, &target);
    app_delay_ms(200);
    TEST_ASSERT_EQUAL_UINT32(f0, app_display_frame_count());
}

static void test_task(void *arg)
{
    (void)arg;
    app_delay_ms(2000); /* czas na otwarcie portu szeregowego po resecie */
    UNITY_BEGIN();
    RUN_TEST(test_display_start_ok);
    RUN_TEST(test_tile_frames_show_state_and_availability);
    RUN_TEST(test_blind_icon_matches_frame_colour);
    RUN_TEST(test_tap_on_tile_calls_tap_handler_with_index);
    RUN_TEST(test_tap_on_offline_tile_flashes_red_without_tap_handler);
    RUN_TEST(test_long_press_opens_details_before_release);
    RUN_TEST(test_light_switch_shows_state_and_follows_set_state);
    RUN_TEST(test_light_switch_tap_sends_opposite_state);
    RUN_TEST(test_brightness_tap_sends_one_dp22);
    RUN_TEST(test_brightness_drag_is_throttled_and_ends_with_final_value);
    RUN_TEST(test_temperature_tap_sends_white_mode);
    RUN_TEST(test_palette_swatches_send_colour);
    RUN_TEST(test_colour_mode_brightness_sends_dp24);
    RUN_TEST(test_tile_under_open_dialog_is_not_tapped);
    RUN_TEST(test_device_going_offline_disables_open_dialog);
    RUN_TEST(test_close_button_closes_details);
    RUN_TEST(test_tap_outside_closes_without_tile_action);
    RUN_TEST(test_blind_buttons_and_position_slider);
    RUN_TEST(test_plug_switch_sends_off);
    RUN_TEST(test_offline_details_have_no_controls);
    RUN_TEST(test_pending_shows_target_then_reverts_with_red_outline);
    RUN_TEST(test_pending_confirmed_keeps_state_without_outline);
    RUN_TEST(test_pending_bad_index_is_ignored);
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
