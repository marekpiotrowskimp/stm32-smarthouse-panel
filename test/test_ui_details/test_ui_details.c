/*
 * Kontrakt ui_details (spec: tile-details, docs/SPEC.md §6.4, §6.5, R-UI-3..8).
 * Logika okna szczegółów i potwierdzania komend – bez LVGL i sprzętu.
 */
#include <string.h>
#include <unity.h>

#include "ui_details.h"

static char buf[96];

void setUp(void)
{
    memset(buf, 0x7F, sizeof buf);
}

void tearDown(void) {}

static dev_state_t online(void)
{
    dev_state_t s = { .link = DEV_LINK_ONLINE, .power_dw = -1, .voltage_dv = -1, .current_ma = -1 };
    return s;
}

/* ---------- AC-1: suwaki i paleta ---------- */

void test_slider_value_maps_track_to_0_100(void)
{
    TEST_ASSERT_EQUAL_UINT(0, ui_slider_value(UI_DLG_SLIDER_X));
    TEST_ASSERT_EQUAL_UINT(50, ui_slider_value(UI_DLG_SLIDER_X + UI_DLG_SLIDER_W / 2));
    TEST_ASSERT_EQUAL_UINT(100, ui_slider_value(UI_DLG_SLIDER_X + UI_DLG_SLIDER_W));
    TEST_ASSERT_EQUAL_UINT(40, ui_slider_value(UI_DLG_SLIDER_X + 144)); /* 144/360 = 40 % */
    TEST_ASSERT_EQUAL_UINT(1, ui_slider_value(UI_DLG_SLIDER_X + 2));    /* 0,56 % -> 1 */
}

void test_slider_value_clamps_outside_track(void)
{
    TEST_ASSERT_EQUAL_UINT(0, ui_slider_value(0));
    TEST_ASSERT_EQUAL_UINT(0, ui_slider_value(-50));
    TEST_ASSERT_EQUAL_UINT(100, ui_slider_value(479));
}

void test_brightness_from_pct(void)
{
    TEST_ASSERT_EQUAL_UINT(10, ui_brightness_from_pct(0));
    TEST_ASSERT_EQUAL_UINT(10, ui_brightness_from_pct(1));
    TEST_ASSERT_EQUAL_UINT(500, ui_brightness_from_pct(50));
    TEST_ASSERT_EQUAL_UINT(1000, ui_brightness_from_pct(100));
    TEST_ASSERT_EQUAL_UINT(1000, ui_brightness_from_pct(150));
}

void test_palette_hues(void)
{
    static const unsigned exp[UI_PALETTE_N] = { 0, 30, 60, 120, 240, 300 };
    for (unsigned i = 0; i < UI_PALETTE_N; i++)
        TEST_ASSERT_EQUAL_UINT(exp[i], ui_palette_hue(i));
    TEST_ASSERT_EQUAL_UINT(0, ui_palette_hue(UI_PALETTE_N));
}

/* ---------- AC-2: max 1 komenda / 300 ms podczas przeciągania ---------- */

void test_drag_first_value_sent_immediately(void)
{
    ui_drag_t d;
    ui_drag_begin(&d);
    TEST_ASSERT_TRUE(ui_drag_move(&d, 1000, 20));
}

void test_drag_throttles_to_300ms(void)
{
    ui_drag_t d;
    ui_drag_begin(&d);
    TEST_ASSERT_TRUE(ui_drag_move(&d, 1000, 20));
    TEST_ASSERT_FALSE(ui_drag_move(&d, 1100, 30));
    TEST_ASSERT_FALSE(ui_drag_move(&d, 1299, 40));
    TEST_ASSERT_TRUE(ui_drag_move(&d, 1300, 50));
    TEST_ASSERT_FALSE(ui_drag_move(&d, 1450, 60));
}

void test_drag_same_value_not_resent(void)
{
    ui_drag_t d;
    ui_drag_begin(&d);
    TEST_ASSERT_TRUE(ui_drag_move(&d, 1000, 20));
    TEST_ASSERT_FALSE(ui_drag_move(&d, 2000, 20));
}

void test_drag_end_sends_final_value_only_if_changed(void)
{
    ui_drag_t d;
    ui_drag_begin(&d);
    ui_drag_move(&d, 1000, 20);
    ui_drag_move(&d, 1100, 70);                   /* wstrzymane przez 300 ms */
    TEST_ASSERT_TRUE(ui_drag_end(&d, 1150, 70));  /* puszczenie: wartość końcowa idzie zawsze */

    ui_drag_begin(&d);                            /* dotknięcie bez przeciągania = 1 komenda */
    TEST_ASSERT_TRUE(ui_drag_move(&d, 5000, 40));
    TEST_ASSERT_FALSE(ui_drag_end(&d, 5080, 40));
}

/* ---------- AC-3: stan optymistyczny po dotknięciu ---------- */

void test_optimistic_light_and_plug_flip_on(void)
{
    dev_state_t cur = online(), out;
    cur.on = false;
    cur.brightness = 700;
    TEST_ASSERT_TRUE(ui_optimistic_state(DEV_TYPE_LIGHT, &cur, &out));
    TEST_ASSERT_TRUE(out.on);
    TEST_ASSERT_EQUAL_UINT16(700, out.brightness); /* żarówka wraca do ostatniej jasności */
    cur.on = true;
    TEST_ASSERT_TRUE(ui_optimistic_state(DEV_TYPE_PLUG, &cur, &out));
    TEST_ASSERT_FALSE(out.on);
}

void test_optimistic_blind(void)
{
    dev_state_t cur = online(), out;
    cur.open_pct = 30;
    TEST_ASSERT_TRUE(ui_optimistic_state(DEV_TYPE_BLIND, &cur, &out));
    TEST_ASSERT_TRUE(out.moving);
    TEST_ASSERT_EQUAL_UINT8(30, out.open_pct);
    cur.moving = true;
    TEST_ASSERT_TRUE(ui_optimistic_state(DEV_TYPE_BLIND, &cur, &out));
    TEST_ASSERT_FALSE(out.moving);
}

void test_optimistic_offline_or_unknown_is_false(void)
{
    dev_state_t cur = { 0 }, out = online();
    out.brightness = 123;
    TEST_ASSERT_FALSE(ui_optimistic_state(DEV_TYPE_LIGHT, &cur, &out));
    cur.link = DEV_LINK_OFFLINE;
    TEST_ASSERT_FALSE(ui_optimistic_state(DEV_TYPE_BLIND, &cur, &out));
    TEST_ASSERT_EQUAL_UINT16(123, out.brightness); /* out bez zmian */
}

/* tile-state-ux AC-7: kierunek ruchu jak komenda tuya_dps_toggle (open_pct >= 50 -> "close") */
void test_optimistic_blind_direction_follows_toggle_command(void)
{
    dev_state_t cur = online(), out;
    cur.open_pct = 30;
    cur.opening = false;
    TEST_ASSERT_TRUE(ui_optimistic_state(DEV_TYPE_BLIND, &cur, &out));
    TEST_ASSERT_TRUE(out.moving);
    TEST_ASSERT_TRUE(out.opening);

    cur.open_pct = 70;
    cur.opening = true;
    TEST_ASSERT_TRUE(ui_optimistic_state(DEV_TYPE_BLIND, &cur, &out));
    TEST_ASSERT_TRUE(out.moving);
    TEST_ASSERT_FALSE(out.opening);

    cur.open_pct = 50;
    cur.opening = true;
    TEST_ASSERT_TRUE(ui_optimistic_state(DEV_TYPE_BLIND, &cur, &out));
    TEST_ASSERT_FALSE(out.opening);
}

/* ---------- AC-4: potwierdzenie albo powrót po 3 s z czerwonym obrysem (R-UI-8) ---------- */

void test_pending_confirmed_by_matching_light_state(void)
{
    ui_pending_t p;
    dev_state_t prev = online(), target = online(), actual = online();
    target.on = true;
    ui_pending_start(&p, 1000, &prev, &target);
    TEST_ASSERT_TRUE(p.active);
    TEST_ASSERT_FALSE(ui_pending_confirm(&p, DEV_TYPE_LIGHT, &actual)); /* nadal wył. */
    TEST_ASSERT_TRUE(p.active);
    actual.on = true;
    TEST_ASSERT_TRUE(ui_pending_confirm(&p, DEV_TYPE_LIGHT, &actual));
    TEST_ASSERT_FALSE(p.active);
    TEST_ASSERT_FALSE(ui_pending_tick(&p, 5000));
    TEST_ASSERT_FALSE(ui_pending_error(&p, 5000));
}

void test_pending_blind_confirmed_by_motion_or_position_change(void)
{
    ui_pending_t p;
    dev_state_t prev = online(), target = online(), actual = online();
    prev.open_pct = 30;
    target.open_pct = 30;
    target.moving = true;
    ui_pending_start(&p, 0, &prev, &target);
    actual.open_pct = 30;
    TEST_ASSERT_FALSE(ui_pending_confirm(&p, DEV_TYPE_BLIND, &actual));
    actual.open_pct = 40; /* bez DP1 "open", ale pozycja się zmieniła */
    TEST_ASSERT_TRUE(ui_pending_confirm(&p, DEV_TYPE_BLIND, &actual));

    ui_pending_start(&p, 0, &prev, &target);
    actual.open_pct = 30;
    actual.moving = true;
    TEST_ASSERT_TRUE(ui_pending_confirm(&p, DEV_TYPE_BLIND, &actual));
}

void test_pending_offline_state_does_not_confirm(void)
{
    ui_pending_t p;
    dev_state_t prev = online(), target = online(), actual = { .link = DEV_LINK_OFFLINE, .on = true };
    target.on = true;
    ui_pending_start(&p, 0, &prev, &target);
    TEST_ASSERT_FALSE(ui_pending_confirm(&p, DEV_TYPE_LIGHT, &actual));
    TEST_ASSERT_TRUE(p.active);
}

void test_pending_times_out_after_3s_with_2s_error(void)
{
    ui_pending_t p;
    dev_state_t prev = online(), target = online();
    target.on = true;
    ui_pending_start(&p, 1000, &prev, &target);
    TEST_ASSERT_FALSE(ui_pending_tick(&p, 3999));
    TEST_ASSERT_TRUE(p.active);
    TEST_ASSERT_TRUE(ui_pending_tick(&p, 4000));  /* 1000 + 3000 */
    TEST_ASSERT_FALSE(p.active);
    TEST_ASSERT_FALSE(ui_pending_tick(&p, 4100)); /* tylko raz */
    TEST_ASSERT_TRUE(ui_pending_error(&p, 4000));
    TEST_ASSERT_TRUE(ui_pending_error(&p, 5999));
    TEST_ASSERT_FALSE(ui_pending_error(&p, 6000)); /* 4000 + 2000 */
}

/* tile-state-ux AC-9: dotknięcie kafelka niedostępnego – czerwony obrys 2 s, przerwane oczekiwanie */
void test_pending_reject_shows_error_for_2s_and_cancels_pending(void)
{
    ui_pending_t p;
    dev_state_t prev = online(), target = online();
    target.on = true;
    ui_pending_start(&p, 500, &prev, &target);
    TEST_ASSERT_TRUE(p.active);
    ui_pending_reject(&p, 1000);
    TEST_ASSERT_FALSE(p.active);
    TEST_ASSERT_TRUE(ui_pending_error(&p, 1000));
    TEST_ASSERT_TRUE(ui_pending_error(&p, 2999));
    TEST_ASSERT_FALSE(ui_pending_error(&p, 3000)); /* 1000 + 2000 */
    TEST_ASSERT_FALSE(ui_pending_tick(&p, 3000));  /* brak drugiego „timeoutu” */
    TEST_ASSERT_EQUAL_UINT32(0, p.error_until);    /* wygasły obrys wyzerowany */

    memset(&p, 0, sizeof p);
    ui_pending_reject(&p, 0xFFFFF830u); /* 0xFFFFF830 + 2000 = 0 -> zamienione na 1 */
    TEST_ASSERT_TRUE(p.error_until != 0);
    TEST_ASSERT_TRUE(ui_pending_error(&p, 0xFFFFFFFFu));
}

void test_pending_restart_clears_error(void)
{
    ui_pending_t p;
    dev_state_t prev = online(), target = online();
    target.on = true;
    ui_pending_start(&p, 0, &prev, &target);
    ui_pending_tick(&p, 3000);
    TEST_ASSERT_TRUE(ui_pending_error(&p, 3100));
    ui_pending_start(&p, 3200, &prev, &target);
    TEST_ASSERT_FALSE(ui_pending_error(&p, 3300));
    TEST_ASSERT_TRUE(p.active);
    TEST_ASSERT_FALSE(ui_pending_tick(&p, 6100)); /* czas liczony od nowa: 3200 + 3000 */
    TEST_ASSERT_TRUE(ui_pending_tick(&p, 6200));
}

void test_pending_blind_stop_not_confirmed_by_position_change(void)
{
    /* roleta w ruchu raportuje pozycję także wtedy, gdy „stop” nie dotarł */
    ui_pending_t p;
    dev_state_t prev = online(), target = online(), actual = online();
    prev.open_pct = 30;
    prev.moving = true;
    target = prev;
    target.moving = false;
    ui_pending_start(&p, 0, &prev, &target);
    actual.open_pct = 40;
    actual.moving = true;
    TEST_ASSERT_FALSE(ui_pending_confirm(&p, DEV_TYPE_BLIND, &actual));
    actual.moving = false;
    TEST_ASSERT_TRUE(ui_pending_confirm(&p, DEV_TYPE_BLIND, &actual));
}

void test_pending_error_does_not_return_after_counter_wrap(void)
{
    ui_pending_t p;
    dev_state_t prev = online(), target = online();
    target.on = true;
    ui_pending_start(&p, 0, &prev, &target);
    ui_pending_tick(&p, 3000);  /* obrys do 5000 */
    ui_pending_tick(&p, 6000);  /* cykliczne wywołanie po wygaśnięciu */
    TEST_ASSERT_FALSE(ui_pending_error(&p, 6000));
    TEST_ASSERT_FALSE(ui_pending_error(&p, 5000u + 0x80000000u + 100u)); /* ~24,8 dnia później */
}

/* ---------- AC-5: zamknięcie okna po 30 s bezczynności ---------- */

void test_details_not_closed_before_30s_across_counter_wrap(void)
{
    TEST_ASSERT_FALSE(ui_details_should_close(0xFFFFF000u, 0xFFFFF000u + 29999u));
}

void test_details_close_after_30s_idle(void)
{
    TEST_ASSERT_FALSE(ui_details_should_close(1000, 30999));
    TEST_ASSERT_TRUE(ui_details_should_close(1000, 31000));
    TEST_ASSERT_TRUE(ui_details_should_close(0xFFFFF000u, 0xFFFFF000u + 30000u)); /* przepełnienie licznika ms */
}

/* ---------- AC-6: odczyty gniazdka ---------- */

void test_plug_readout_all_measurements(void)
{
    dev_state_t s = online();
    s.on = true;
    s.power_dw = 234;
    s.voltage_dv = 2301;
    s.current_ma = 105;
    static const char exp[] = "Moc: 23.4 W\nNapięcie: 230.1 V\nPrąd: 0.105 A";
    TEST_ASSERT_EQUAL_INT(strlen(exp), ui_plug_readout(buf, sizeof buf, &s));
    TEST_ASSERT_EQUAL_STRING(exp, buf);
}

void test_plug_readout_partial_and_none(void)
{
    dev_state_t s = online();
    s.voltage_dv = 2299;
    ui_plug_readout(buf, sizeof buf, &s);
    TEST_ASSERT_EQUAL_STRING("Napięcie: 229.9 V", buf);
    s.voltage_dv = -1;
    ui_plug_readout(buf, sizeof buf, &s);
    TEST_ASSERT_EQUAL_STRING("Brak pomiarów", buf);
    s.current_ma = 2500;
    s.power_dw = 0;
    ui_plug_readout(buf, sizeof buf, &s);
    TEST_ASSERT_EQUAL_STRING("Moc: 0.0 W\nPrąd: 2.500 A", buf);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_slider_value_maps_track_to_0_100);
    RUN_TEST(test_slider_value_clamps_outside_track);
    RUN_TEST(test_brightness_from_pct);
    RUN_TEST(test_palette_hues);
    RUN_TEST(test_drag_first_value_sent_immediately);
    RUN_TEST(test_drag_throttles_to_300ms);
    RUN_TEST(test_drag_same_value_not_resent);
    RUN_TEST(test_drag_end_sends_final_value_only_if_changed);
    RUN_TEST(test_optimistic_light_and_plug_flip_on);
    RUN_TEST(test_optimistic_blind);
    RUN_TEST(test_optimistic_offline_or_unknown_is_false);
    RUN_TEST(test_optimistic_blind_direction_follows_toggle_command);
    RUN_TEST(test_pending_confirmed_by_matching_light_state);
    RUN_TEST(test_pending_blind_confirmed_by_motion_or_position_change);
    RUN_TEST(test_pending_offline_state_does_not_confirm);
    RUN_TEST(test_pending_times_out_after_3s_with_2s_error);
    RUN_TEST(test_pending_reject_shows_error_for_2s_and_cancels_pending);
    RUN_TEST(test_pending_restart_clears_error);
    RUN_TEST(test_pending_blind_stop_not_confirmed_by_position_change);
    RUN_TEST(test_pending_error_does_not_return_after_counter_wrap);
    RUN_TEST(test_details_close_after_30s_idle);
    RUN_TEST(test_details_not_closed_before_30s_across_counter_wrap);
    RUN_TEST(test_plug_readout_all_measurements);
    RUN_TEST(test_plug_readout_partial_and_none);
    return UNITY_END();
}
