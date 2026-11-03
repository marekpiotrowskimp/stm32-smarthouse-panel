/*
 * Kontrakt ui_model (spec: display-tiles, docs/SPEC.md §6).
 * Teksty UTF-8 z polskimi znakami porównywane bajt w bajt.
 */
#include <string.h>
#include <unity.h>

#include "ui_model.h"

static ui_tile_view_t v;
static char buf[64];

void setUp(void)
{
    memset(&v, 0x5A, sizeof v);
    memset(buf, 0x7F, sizeof buf);
}

void tearDown(void) {}

static dev_state_t online(void)
{
    dev_state_t s = { .link = DEV_LINK_ONLINE, .power_dw = -1 };
    return s;
}

/* ---------- AC-1: wygląd kafelka ---------- */

void test_light_off(void)
{
    dev_state_t s = online();
    s.on = false;
    ui_tile_view(DEV_TYPE_LIGHT, &s, &v);
    TEST_ASSERT_EQUAL_INT(UI_ICON_BULB_OFF, v.icon);
    TEST_ASSERT_FALSE(v.dimmed);
    TEST_ASSERT_EQUAL_UINT8(0, v.glow_pct);
    TEST_ASSERT_EQUAL_STRING("Wył.", v.status);
}

void test_light_on_brightness_600_is_60_percent(void)
{
    dev_state_t s = online();
    s.on = true;
    s.brightness = 600;
    ui_tile_view(DEV_TYPE_LIGHT, &s, &v);
    TEST_ASSERT_EQUAL_INT(UI_ICON_BULB_ON, v.icon);
    TEST_ASSERT_EQUAL_UINT8(60, v.glow_pct);
    TEST_ASSERT_EQUAL_STRING("Wł. 60%", v.status);
}

void test_light_brightness_rounding_and_limits(void)
{
    dev_state_t s = online();
    s.on = true;
    s.brightness = 10;
    ui_tile_view(DEV_TYPE_LIGHT, &s, &v);
    TEST_ASSERT_EQUAL_STRING("Wł. 1%", v.status);
    TEST_ASSERT_EQUAL_UINT8(1, v.glow_pct);

    s.brightness = 605;
    ui_tile_view(DEV_TYPE_LIGHT, &s, &v);
    TEST_ASSERT_EQUAL_STRING("Wł. 61%", v.status);

    s.brightness = 1000;
    ui_tile_view(DEV_TYPE_LIGHT, &s, &v);
    TEST_ASSERT_EQUAL_STRING("Wł. 100%", v.status);
    TEST_ASSERT_EQUAL_UINT8(100, v.glow_pct);
}

/* control-polish AC-4: jasność kafelka z DP22 w bieli, z V (DP24) w trybie koloru */
void test_light_level_and_tile_percent_follow_mode(void)
{
    dev_state_t s = online();
    s.on = true;
    s.brightness = 800;
    s.val = 300;
    s.colour = false;
    TEST_ASSERT_EQUAL_UINT16(800, ui_light_level(&s));
    ui_tile_view(DEV_TYPE_LIGHT, &s, &v);
    TEST_ASSERT_EQUAL_STRING("Wł. 80%", v.status);
    TEST_ASSERT_EQUAL_UINT8(80, v.glow_pct);

    s.colour = true;
    TEST_ASSERT_EQUAL_UINT16(300, ui_light_level(&s));
    ui_tile_view(DEV_TYPE_LIGHT, &s, &v);
    TEST_ASSERT_EQUAL_STRING("Wł. 30%", v.status);
    TEST_ASSERT_EQUAL_UINT8(30, v.glow_pct);
}

void test_blind_closed_open_partial(void)
{
    dev_state_t s = online();
    s.open_pct = 0;
    ui_tile_view(DEV_TYPE_BLIND, &s, &v);
    TEST_ASSERT_EQUAL_INT(UI_ICON_BLIND_CLOSED, v.icon);
    TEST_ASSERT_EQUAL_STRING("Zamknięta", v.status);

    s.open_pct = 100;
    ui_tile_view(DEV_TYPE_BLIND, &s, &v);
    TEST_ASSERT_EQUAL_INT(UI_ICON_BLIND_OPEN, v.icon);
    TEST_ASSERT_EQUAL_STRING("Otwarta", v.status);

    s.open_pct = 40;
    ui_tile_view(DEV_TYPE_BLIND, &s, &v);
    TEST_ASSERT_EQUAL_INT(UI_ICON_BLIND_PARTIAL, v.icon);
    TEST_ASSERT_EQUAL_STRING("Otwarta 40%", v.status); /* tile-state-ux AC-5 (było "40%") */
    TEST_ASSERT_FALSE(v.dimmed);
}

/* tile-state-ux AC-6: kierunek ruchu zamiast „W ruchu”; ruch ma pierwszeństwo przed pozycją */
void test_blind_moving_shows_direction(void)
{
    dev_state_t s = online();
    s.open_pct = 100;
    s.moving = true;
    s.opening = false;
    ui_tile_view(DEV_TYPE_BLIND, &s, &v);
    TEST_ASSERT_EQUAL_INT(UI_ICON_BLIND_MOVING, v.icon);
    TEST_ASSERT_EQUAL_STRING("Zamykanie…", v.status);
    TEST_ASSERT_FALSE(v.dimmed);

    s.open_pct = 0;
    s.opening = true;
    ui_tile_view(DEV_TYPE_BLIND, &s, &v);
    TEST_ASSERT_EQUAL_INT(UI_ICON_BLIND_MOVING, v.icon);
    TEST_ASSERT_EQUAL_STRING("Otwieranie…", v.status);
}

void test_plug_on_with_power_and_without_measurement(void)
{
    dev_state_t s = online();
    s.on = true;
    s.power_dw = 234;
    ui_tile_view(DEV_TYPE_PLUG, &s, &v);
    TEST_ASSERT_EQUAL_INT(UI_ICON_PLUG_ON, v.icon);
    TEST_ASSERT_EQUAL_STRING("Wł. 23.4 W", v.status);

    s.power_dw = -1;
    ui_tile_view(DEV_TYPE_PLUG, &s, &v);
    TEST_ASSERT_EQUAL_STRING("Wł.", v.status);

    s.power_dw = 0;
    ui_tile_view(DEV_TYPE_PLUG, &s, &v);
    TEST_ASSERT_EQUAL_STRING("Wł. 0.0 W", v.status);
}

void test_plug_off(void)
{
    dev_state_t s = online();
    s.on = false;
    s.power_dw = 0;
    ui_tile_view(DEV_TYPE_PLUG, &s, &v);
    TEST_ASSERT_EQUAL_INT(UI_ICON_PLUG_OFF, v.icon);
    TEST_ASSERT_EQUAL_STRING("Wył.", v.status);
}

void test_offline_device_is_dimmed_with_base_icon(void)
{
    dev_state_t s = online();
    s.link = DEV_LINK_OFFLINE;
    s.on = true;
    s.brightness = 1000;
    ui_tile_view(DEV_TYPE_LIGHT, &s, &v);
    TEST_ASSERT_EQUAL_INT(UI_ICON_BULB_OFF, v.icon);
    TEST_ASSERT_TRUE(v.dimmed);
    TEST_ASSERT_EQUAL_UINT8(0, v.glow_pct);
    TEST_ASSERT_EQUAL_STRING("Offline", v.status);
    TEST_ASSERT_TRUE(v.warn);         /* tile-state-ux AC-2: napis bursztynowy */
    TEST_ASSERT_TRUE(v.offline_mark); /* obrys UI_COLOR_WARN i znak ostrzeżenia */
}

/* tile-state-ux AC-3: „Łączenie…” zamiast „—”, bez ostrzeżenia */
void test_unknown_state_is_dimmed_connecting(void)
{
    dev_state_t s = { 0 }; /* DEV_LINK_UNKNOWN */
    ui_tile_view(DEV_TYPE_BLIND, &s, &v);
    TEST_ASSERT_EQUAL_INT(UI_ICON_BLIND_CLOSED, v.icon);
    TEST_ASSERT_TRUE(v.dimmed);
    TEST_ASSERT_EQUAL_STRING("Łączenie…", v.status);
    TEST_ASSERT_FALSE(v.warn);
    TEST_ASSERT_FALSE(v.offline_mark);

    ui_tile_view(DEV_TYPE_PLUG, &s, &v);
    TEST_ASSERT_EQUAL_INT(UI_ICON_PLUG_OFF, v.icon);
    TEST_ASSERT_EQUAL_STRING("Łączenie…", v.status);
}

/* tile-state-ux AC-1/AC-2: kafelki online – bez ostrzeżenia, niezależnie od stanu */
void test_online_states_have_no_warning(void)
{
    dev_state_t s = online();
    s.open_pct = 0;
    ui_tile_view(DEV_TYPE_BLIND, &s, &v);
    TEST_ASSERT_FALSE(v.dimmed);
    TEST_ASSERT_FALSE(v.warn);
    TEST_ASSERT_FALSE(v.offline_mark);

    s.on = false;
    ui_tile_view(DEV_TYPE_LIGHT, &s, &v);
    TEST_ASSERT_FALSE(v.warn);
    TEST_ASSERT_FALSE(v.offline_mark);

    s.on = true;
    ui_tile_view(DEV_TYPE_PLUG, &s, &v);
    TEST_ASSERT_FALSE(v.warn);
    TEST_ASSERT_FALSE(v.offline_mark);
}

/* tile-state-ux AC-8: brak potwierdzenia komendy – „Brak reakcji”, ikona stanu poprzedniego zostaje */
void test_error_on_online_tile_shows_no_reaction(void)
{
    dev_state_t s = online();
    s.open_pct = 0;
    ui_tile_view(DEV_TYPE_BLIND, &s, &v);
    ui_tile_view_error(&v);
    TEST_ASSERT_EQUAL_STRING("Brak reakcji", v.status);
    TEST_ASSERT_TRUE(v.warn);
    TEST_ASSERT_EQUAL_INT(UI_ICON_BLIND_CLOSED, v.icon);
    TEST_ASSERT_FALSE(v.dimmed);
    TEST_ASSERT_FALSE(v.offline_mark);
}

/* tile-state-ux AC-8/AC-9: kafelek przygaszony (dotknięty, gdy niedostępny) zachowuje napis */
void test_error_on_dimmed_tile_keeps_text(void)
{
    dev_state_t s = online();
    s.link = DEV_LINK_OFFLINE;
    ui_tile_view(DEV_TYPE_LIGHT, &s, &v);
    ui_tile_view_error(&v);
    TEST_ASSERT_EQUAL_STRING("Offline", v.status);
    TEST_ASSERT_TRUE(v.warn);
    TEST_ASSERT_TRUE(v.dimmed);

    dev_state_t u = { 0 };
    ui_tile_view(DEV_TYPE_PLUG, &u, &v);
    ui_tile_view_error(&v);
    TEST_ASSERT_EQUAL_STRING("Łączenie…", v.status);
    TEST_ASSERT_TRUE(v.warn);
}

/* ---------- AC-2: strony i układ ---------- */

void test_page_count(void)
{
    TEST_ASSERT_EQUAL_UINT(1, ui_page_count(0));
    TEST_ASSERT_EQUAL_UINT(1, ui_page_count(1));
    TEST_ASSERT_EQUAL_UINT(1, ui_page_count(8));
    TEST_ASSERT_EQUAL_UINT(2, ui_page_count(9));
    TEST_ASSERT_EQUAL_UINT(2, ui_page_count(15));
    TEST_ASSERT_EQUAL_UINT(2, ui_page_count(16));
    TEST_ASSERT_EQUAL_UINT(3, ui_page_count(17));
}

void test_tile_index_for_15_devices(void)
{
    TEST_ASSERT_EQUAL_INT(0, ui_tile_index(0, 0, 15));
    TEST_ASSERT_EQUAL_INT(7, ui_tile_index(0, 7, 15));
    TEST_ASSERT_EQUAL_INT(8, ui_tile_index(1, 0, 15));
    TEST_ASSERT_EQUAL_INT(14, ui_tile_index(1, 6, 15));
    TEST_ASSERT_EQUAL_INT(-1, ui_tile_index(1, 7, 15)); /* puste miejsce */
    TEST_ASSERT_EQUAL_INT(-1, ui_tile_index(2, 0, 15)); /* nie ma strony 3 */
    TEST_ASSERT_EQUAL_INT(-1, ui_tile_index(0, 8, 15)); /* slot poza siatką */
}

void test_tile_origins_form_4x2_grid(void)
{
    int x, y;
    ui_tile_origin(0, &x, &y);
    TEST_ASSERT_EQUAL_INT(6, x);
    TEST_ASSERT_EQUAL_INT(34, y);
    ui_tile_origin(3, &x, &y);
    TEST_ASSERT_EQUAL_INT(6 + 3 * (112 + 6), x); /* 360 */
    TEST_ASSERT_EQUAL_INT(34, y);
    ui_tile_origin(4, &x, &y);
    TEST_ASSERT_EQUAL_INT(6, x);
    TEST_ASSERT_EQUAL_INT(34 + 104 + 6, y); /* 144 */
    ui_tile_origin(7, &x, &y);
    TEST_ASSERT_EQUAL_INT(360, x);
    TEST_ASSERT_EQUAL_INT(144, y);
    /* ostatni kafelek mieści się nad wskaźnikiem stron */
    TEST_ASSERT_LESS_OR_EQUAL_INT(UI_SCREEN_W, x + UI_TILE_W);
    TEST_ASSERT_LESS_OR_EQUAL_INT(UI_SCREEN_H - UI_DOTS_H, y + UI_TILE_H);
}

/* ---------- AC-3: gesty ---------- */

void test_gesture_tap_and_long_press(void)
{
    TEST_ASSERT_EQUAL_INT(UI_GESTURE_TAP, ui_gesture_classify(120, 3, -2));
    TEST_ASSERT_EQUAL_INT(UI_GESTURE_TAP, ui_gesture_classify(499, 9, 9));
    TEST_ASSERT_EQUAL_INT(UI_GESTURE_LONG_PRESS, ui_gesture_classify(500, 0, 0));
    TEST_ASSERT_EQUAL_INT(UI_GESTURE_LONG_PRESS, ui_gesture_classify(1500, -9, 4));
}

void test_gesture_swipes(void)
{
    TEST_ASSERT_EQUAL_INT(UI_GESTURE_SWIPE_LEFT, ui_gesture_classify(200, -40, 5));
    TEST_ASSERT_EQUAL_INT(UI_GESTURE_SWIPE_RIGHT, ui_gesture_classify(800, 120, -30));
}

void test_gesture_ambiguous_movement_is_none(void)
{
    TEST_ASSERT_EQUAL_INT(UI_GESTURE_NONE, ui_gesture_classify(200, 10, 0));   /* za mały na swipe, za duży na tap */
    TEST_ASSERT_EQUAL_INT(UI_GESTURE_NONE, ui_gesture_classify(200, 39, 0));
    TEST_ASSERT_EQUAL_INT(UI_GESTURE_NONE, ui_gesture_classify(200, -50, 60)); /* głównie pionowo */
}

void test_page_after_gesture_clamps_without_wrapping(void)
{
    TEST_ASSERT_EQUAL_UINT(1, ui_page_after_gesture(0, 2, UI_GESTURE_SWIPE_LEFT));
    TEST_ASSERT_EQUAL_UINT(1, ui_page_after_gesture(1, 2, UI_GESTURE_SWIPE_LEFT));
    TEST_ASSERT_EQUAL_UINT(0, ui_page_after_gesture(1, 2, UI_GESTURE_SWIPE_RIGHT));
    TEST_ASSERT_EQUAL_UINT(0, ui_page_after_gesture(0, 2, UI_GESTURE_SWIPE_RIGHT));
    TEST_ASSERT_EQUAL_UINT(1, ui_page_after_gesture(1, 2, UI_GESTURE_TAP));
    TEST_ASSERT_EQUAL_UINT(0, ui_page_after_gesture(0, 1, UI_GESTURE_SWIPE_LEFT));
}

/* ---------- AC-4: pasek stanu ---------- */

void test_net_label(void)
{
    net_status_t st = { .link_up = true, .has_ip = true, .ip = NET_IP4(192, 168, 0, 245) };
    TEST_ASSERT_EQUAL_INT(13, ui_net_label(buf, sizeof buf, &st));
    TEST_ASSERT_EQUAL_STRING("192.168.0.245", buf);

    st.has_ip = false;
    ui_net_label(buf, sizeof buf, &st);
    TEST_ASSERT_EQUAL_STRING("Łączenie…", buf);

    st.link_up = false;
    ui_net_label(buf, sizeof buf, &st);
    TEST_ASSERT_EQUAL_STRING("Brak sieci", buf);
}

void test_page_label(void)
{
    TEST_ASSERT_EQUAL_INT(3, ui_page_label(buf, sizeof buf, 0, 2));
    TEST_ASSERT_EQUAL_STRING("1/2", buf);
    ui_page_label(buf, sizeof buf, 1, 2);
    TEST_ASSERT_EQUAL_STRING("2/2", buf);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_light_off);
    RUN_TEST(test_light_on_brightness_600_is_60_percent);
    RUN_TEST(test_light_brightness_rounding_and_limits);
    RUN_TEST(test_light_level_and_tile_percent_follow_mode);
    RUN_TEST(test_blind_closed_open_partial);
    RUN_TEST(test_blind_moving_shows_direction);
    RUN_TEST(test_plug_on_with_power_and_without_measurement);
    RUN_TEST(test_plug_off);
    RUN_TEST(test_offline_device_is_dimmed_with_base_icon);
    RUN_TEST(test_unknown_state_is_dimmed_connecting);
    RUN_TEST(test_online_states_have_no_warning);
    RUN_TEST(test_error_on_online_tile_shows_no_reaction);
    RUN_TEST(test_error_on_dimmed_tile_keeps_text);
    RUN_TEST(test_page_count);
    RUN_TEST(test_tile_index_for_15_devices);
    RUN_TEST(test_tile_origins_form_4x2_grid);
    RUN_TEST(test_gesture_tap_and_long_press);
    RUN_TEST(test_gesture_swipes);
    RUN_TEST(test_gesture_ambiguous_movement_is_none);
    RUN_TEST(test_page_after_gesture_clamps_without_wrapping);
    RUN_TEST(test_net_label);
    RUN_TEST(test_page_label);
    return UNITY_END();
}
