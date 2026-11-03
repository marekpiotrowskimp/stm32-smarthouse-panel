/*
 * Kontrakt tuya_dps (spec: tuya-control). Formaty JSON zgodne z referencyjnym tinytuya
 * (ten sam JSON CONTROL co w test_tuya_proto/vectors.h) i z prawdziwymi odpowiedziami urządzeń.
 */
#include <string.h>
#include <unity.h>

#include "tuya_dps.h"

static char buf[256];

void setUp(void)
{
    memset(buf, 0x7F, sizeof buf);
}

void tearDown(void) {}

static dev_state_t online(void)
{
    dev_state_t s = { .link = DEV_LINK_ONLINE, .power_dw = -1 };
    return s;
}

/* ---------- AC-1: JSON ramek ---------- */

void test_control_json_matches_tinytuya_format(void)
{
    static const char exp[] =
        "{\"devId\":\"bf0000000000000000test\",\"uid\":\"bf0000000000000000test\",\"t\":\"1700000000\",\"dps\":{\"20\":true}}";
    TEST_ASSERT_EQUAL_INT(strlen(exp), tuya_json_control(buf, sizeof buf, "bf0000000000000000test", 1700000000u, "{\"20\":true}"));
    TEST_ASSERT_EQUAL_STRING(exp, buf);
}

void test_query_json_matches_tinytuya_format(void)
{
    static const char exp[] =
        "{\"gwId\":\"bf0000000000000000test\",\"devId\":\"bf0000000000000000test\",\"uid\":\"bf0000000000000000test\",\"t\":\"1700000000\"}";
    TEST_ASSERT_EQUAL_INT(strlen(exp), tuya_json_query(buf, sizeof buf, "bf0000000000000000test", 1700000000u));
    TEST_ASSERT_EQUAL_STRING(exp, buf);
}

void test_control_json_truncates_like_snprintf(void)
{
    int n = tuya_json_control(buf, 10, "abc", 1u, "{\"1\":true}");
    TEST_ASSERT_EQUAL_INT(strlen("{\"devId\":\"abc\",\"uid\":\"abc\",\"t\":\"1\",\"dps\":{\"1\":true}}"), n);
    TEST_ASSERT_EQUAL_STRING("{\"devId\":", buf); /* 9 znaków + '\0' */
}

/* ---------- AC-2: budowanie DP ---------- */

void test_blind_control_commands(void)
{
    TEST_ASSERT_EQUAL_INT(12, tuya_dps_blind_control(buf, sizeof buf, "open"));
    TEST_ASSERT_EQUAL_STRING("{\"1\":\"open\"}", buf);
    tuya_dps_blind_control(buf, sizeof buf, "stop");
    TEST_ASSERT_EQUAL_STRING("{\"1\":\"stop\"}", buf);
    tuya_dps_blind_control(buf, sizeof buf, "close");
    TEST_ASSERT_EQUAL_STRING("{\"1\":\"close\"}", buf);
}

void test_blind_control_rejects_unknown_value(void)
{
    TEST_ASSERT_EQUAL_INT(0, tuya_dps_blind_control(buf, sizeof buf, "up"));
    TEST_ASSERT_EQUAL_STRING("", buf);
}

void test_blind_position_rounds_to_step_10_and_clamps(void)
{
    tuya_dps_blind_position(buf, sizeof buf, 40);
    TEST_ASSERT_EQUAL_STRING("{\"2\":40}", buf);
    tuya_dps_blind_position(buf, sizeof buf, 44);
    TEST_ASSERT_EQUAL_STRING("{\"2\":40}", buf);
    tuya_dps_blind_position(buf, sizeof buf, 45);
    TEST_ASSERT_EQUAL_STRING("{\"2\":50}", buf);
    tuya_dps_blind_position(buf, sizeof buf, 0);
    TEST_ASSERT_EQUAL_STRING("{\"2\":0}", buf);
    tuya_dps_blind_position(buf, sizeof buf, 120);
    TEST_ASSERT_EQUAL_STRING("{\"2\":100}", buf);
}

void test_switch_uses_dp_per_type(void)
{
    tuya_dps_switch(buf, sizeof buf, DEV_TYPE_LIGHT, true);
    TEST_ASSERT_EQUAL_STRING("{\"20\":true}", buf);
    tuya_dps_switch(buf, sizeof buf, DEV_TYPE_LIGHT, false);
    TEST_ASSERT_EQUAL_STRING("{\"20\":false}", buf);
    tuya_dps_switch(buf, sizeof buf, DEV_TYPE_PLUG, true);
    TEST_ASSERT_EQUAL_STRING("{\"1\":true}", buf);
    TEST_ASSERT_EQUAL_INT(0, tuya_dps_switch(buf, sizeof buf, DEV_TYPE_BLIND, true));
    TEST_ASSERT_EQUAL_STRING("", buf);
}

void test_brightness_clamped_to_10_1000(void)
{
    tuya_dps_brightness(buf, sizeof buf, 500);
    TEST_ASSERT_EQUAL_STRING("{\"22\":500}", buf);
    tuya_dps_brightness(buf, sizeof buf, 5);
    TEST_ASSERT_EQUAL_STRING("{\"22\":10}", buf);
    tuya_dps_brightness(buf, sizeof buf, 2000);
    TEST_ASSERT_EQUAL_STRING("{\"22\":1000}", buf);
}

/* ---------- AC-3: jedno dotknięcie = stan przeciwny (§6.4) ---------- */

void test_toggle_blind(void)
{
    dev_state_t s = online();
    s.open_pct = 0;
    tuya_dps_toggle(buf, sizeof buf, DEV_TYPE_BLIND, &s);
    TEST_ASSERT_EQUAL_STRING("{\"1\":\"open\"}", buf);
    s.open_pct = 49;
    tuya_dps_toggle(buf, sizeof buf, DEV_TYPE_BLIND, &s);
    TEST_ASSERT_EQUAL_STRING("{\"1\":\"open\"}", buf);
    s.open_pct = 50;
    tuya_dps_toggle(buf, sizeof buf, DEV_TYPE_BLIND, &s);
    TEST_ASSERT_EQUAL_STRING("{\"1\":\"close\"}", buf);
    s.open_pct = 100;
    tuya_dps_toggle(buf, sizeof buf, DEV_TYPE_BLIND, &s);
    TEST_ASSERT_EQUAL_STRING("{\"1\":\"close\"}", buf);
}

void test_toggle_moving_blind_stops(void)
{
    dev_state_t s = online();
    s.open_pct = 100;
    s.moving = true;
    tuya_dps_toggle(buf, sizeof buf, DEV_TYPE_BLIND, &s);
    TEST_ASSERT_EQUAL_STRING("{\"1\":\"stop\"}", buf);
}

void test_toggle_light_and_plug(void)
{
    dev_state_t s = online();
    s.on = true;
    tuya_dps_toggle(buf, sizeof buf, DEV_TYPE_LIGHT, &s);
    TEST_ASSERT_EQUAL_STRING("{\"20\":false}", buf);
    s.on = false;
    tuya_dps_toggle(buf, sizeof buf, DEV_TYPE_LIGHT, &s);
    TEST_ASSERT_EQUAL_STRING("{\"20\":true}", buf);
    s.on = true;
    tuya_dps_toggle(buf, sizeof buf, DEV_TYPE_PLUG, &s);
    TEST_ASSERT_EQUAL_STRING("{\"1\":false}", buf);
}

void test_toggle_unknown_or_offline_sends_nothing(void)
{
    dev_state_t s = { 0 }; /* DEV_LINK_UNKNOWN */
    TEST_ASSERT_EQUAL_INT(0, tuya_dps_toggle(buf, sizeof buf, DEV_TYPE_LIGHT, &s));
    TEST_ASSERT_EQUAL_STRING("", buf);
    s.link = DEV_LINK_OFFLINE;
    TEST_ASSERT_EQUAL_INT(0, tuya_dps_toggle(buf, sizeof buf, DEV_TYPE_BLIND, &s));
    TEST_ASSERT_EQUAL_STRING("", buf);
}

/* ---------- AC-4: stan z odpowiedzi / pusha ---------- */

void test_apply_blind_push(void)
{
    dev_state_t s = online();
    s.moving = true;
    TEST_ASSERT_TRUE(tuya_dps_apply("{\"devId\":\"x\",\"dps\":{\"1\":\"stop\",\"2\":40},\"t\":1700000001}", DEV_TYPE_BLIND, &s));
    TEST_ASSERT_EQUAL_UINT8(40, s.open_pct);
    TEST_ASSERT_FALSE(s.moving);
}

void test_apply_blind_moving_keeps_position(void)
{
    dev_state_t s = online();
    s.open_pct = 70;
    TEST_ASSERT_TRUE(tuya_dps_apply("{\"dps\":{\"1\":\"close\"}}", DEV_TYPE_BLIND, &s));
    TEST_ASSERT_TRUE(s.moving);
    TEST_ASSERT_EQUAL_UINT8(70, s.open_pct);
}

/* tile-state-ux AC-6: DP1 "open" / "close" ustawia kierunek ruchu */
void test_blind_dp1_sets_motion_direction(void)
{
    dev_state_t s = online();
    s.open_pct = 20;
    TEST_ASSERT_TRUE(tuya_dps_apply("{\"dps\":{\"1\":\"open\"}}", DEV_TYPE_BLIND, &s));
    TEST_ASSERT_TRUE(s.moving);
    TEST_ASSERT_TRUE(s.opening);

    TEST_ASSERT_TRUE(tuya_dps_apply("{\"dps\":{\"1\":\"close\"}}", DEV_TYPE_BLIND, &s));
    TEST_ASSERT_TRUE(s.moving);
    TEST_ASSERT_FALSE(s.opening);

    TEST_ASSERT_TRUE(tuya_dps_apply("{\"dps\":{\"1\":\"open\",\"2\":35}}", DEV_TYPE_BLIND, &s));
    TEST_ASSERT_TRUE(s.opening);
    TEST_ASSERT_EQUAL_UINT8(35, s.open_pct);

    TEST_ASSERT_TRUE(tuya_dps_apply("{\"dps\":{\"1\":\"stop\"}}", DEV_TYPE_BLIND, &s));
    TEST_ASSERT_FALSE(s.moving);
}

/* tile-state-ux AC-4: pierwsza nieudana próba przed pierwszym połączeniem nie daje „Offline” */
void test_link_after_fail_needs_two_failures_before_first_online(void)
{
    TEST_ASSERT_EQUAL_INT(DEV_LINK_UNKNOWN, tuya_link_after_fail(DEV_LINK_UNKNOWN, 1));
    TEST_ASSERT_EQUAL_INT(DEV_LINK_OFFLINE, tuya_link_after_fail(DEV_LINK_UNKNOWN, 2));
    TEST_ASSERT_EQUAL_INT(DEV_LINK_OFFLINE, tuya_link_after_fail(DEV_LINK_UNKNOWN, 5));
    TEST_ASSERT_EQUAL_INT(DEV_LINK_OFFLINE, tuya_link_after_fail(DEV_LINK_ONLINE, 1)); /* było online – od razu */
    TEST_ASSERT_EQUAL_INT(DEV_LINK_OFFLINE, tuya_link_after_fail(DEV_LINK_OFFLINE, 1));
}

void test_apply_full_blind_status_from_device(void)
{
    /* odpowiedź DP_QUERY rolety „Pralnia” */
    dev_state_t s = online();
    TEST_ASSERT_TRUE(tuya_dps_apply("{\"devId\":\"bfdemo0000000000000008\",\"dps\":{\"1\":\"stop\",\"2\":29,\"8\":\"forward\",\"10\":10}}",
                                    DEV_TYPE_BLIND, &s));
    TEST_ASSERT_EQUAL_UINT8(29, s.open_pct);
    TEST_ASSERT_FALSE(s.moving);
}

void test_apply_full_light_status_from_device(void)
{
    /* odpowiedź DP_QUERY żarówki „Sypialnia” (DP24/25 jako stringi z cyframi) */
    dev_state_t s = online();
    s.on = true;
    TEST_ASSERT_TRUE(tuya_dps_apply(
        "{\"devId\":\"532877073c6105ca9945\",\"dps\":{\"20\":false,\"21\":\"white\",\"22\":1000,\"23\":0,"
        "\"24\":\"000003e803e8\",\"25\":\"000e0d0000000000000000c80000\",\"26\":0}}",
        DEV_TYPE_LIGHT, &s));
    TEST_ASSERT_FALSE(s.on);
    TEST_ASSERT_EQUAL_UINT16(1000, s.brightness);
}

void test_apply_light_partial_push_keeps_other_fields(void)
{
    dev_state_t s = online();
    s.on = false;
    s.brightness = 300;
    TEST_ASSERT_TRUE(tuya_dps_apply("{\"dps\":{\"20\":true}}", DEV_TYPE_LIGHT, &s));
    TEST_ASSERT_TRUE(s.on);
    TEST_ASSERT_EQUAL_UINT16(300, s.brightness);
}

void test_apply_plug_with_power(void)
{
    dev_state_t s = online();
    TEST_ASSERT_TRUE(tuya_dps_apply("{\"dps\":{\"1\":true,\"9\":0,\"18\":105,\"19\":234,\"20\":2301}}", DEV_TYPE_PLUG, &s));
    TEST_ASSERT_TRUE(s.on);
    TEST_ASSERT_EQUAL_INT32(234, s.power_dw);
}

void test_apply_tolerates_whitespace(void)
{
    dev_state_t s = online();
    TEST_ASSERT_TRUE(tuya_dps_apply("{ \"dps\" : { \"20\" : true , \"22\" : 640 } }", DEV_TYPE_LIGHT, &s));
    TEST_ASSERT_TRUE(s.on);
    TEST_ASSERT_EQUAL_UINT16(640, s.brightness);
}

void test_apply_does_not_change_link(void)
{
    dev_state_t s = { .link = DEV_LINK_OFFLINE };
    tuya_dps_apply("{\"dps\":{\"20\":true}}", DEV_TYPE_LIGHT, &s);
    TEST_ASSERT_EQUAL_INT(DEV_LINK_OFFLINE, s.link);
}

void test_apply_unknown_dps_or_garbage_returns_false_and_keeps_state(void)
{
    dev_state_t s = online();
    s.on = true;
    s.brightness = 500;
    dev_state_t before = s;
    TEST_ASSERT_FALSE(tuya_dps_apply("{\"dps\":{\"99\":5}}", DEV_TYPE_LIGHT, &s));
    TEST_ASSERT_FALSE(tuya_dps_apply("not json", DEV_TYPE_LIGHT, &s));
    TEST_ASSERT_FALSE(tuya_dps_apply("{\"dps\":{\"20\":", DEV_TYPE_LIGHT, &s)); /* ucięty */
    TEST_ASSERT_FALSE(tuya_dps_apply("", DEV_TYPE_LIGHT, &s));
    TEST_ASSERT_EQUAL_MEMORY(&before, &s, sizeof s);
}

void test_apply_ignores_dps_of_other_type(void)
{
    /* DP1 dla żarówki nic nie znaczy */
    dev_state_t s = online();
    TEST_ASSERT_FALSE(tuya_dps_apply("{\"dps\":{\"1\":true}}", DEV_TYPE_LIGHT, &s));
    TEST_ASSERT_FALSE(s.on);
}

/* ---------- tile-details: barwa, kolor, pomiary gniazdka ---------- */

void test_temperature_sets_white_mode_and_clamps(void)
{
    TEST_ASSERT_EQUAL_INT(strlen("{\"21\":\"white\",\"23\":0}"), tuya_dps_temperature(buf, sizeof buf, 0));
    TEST_ASSERT_EQUAL_STRING("{\"21\":\"white\",\"23\":0}", buf);
    tuya_dps_temperature(buf, sizeof buf, 640);
    TEST_ASSERT_EQUAL_STRING("{\"21\":\"white\",\"23\":640}", buf);
    tuya_dps_temperature(buf, sizeof buf, 5000);
    TEST_ASSERT_EQUAL_STRING("{\"21\":\"white\",\"23\":1000}", buf);
}

void test_colour_hsv_as_12_hex_digits_like_device(void)
{
    tuya_dps_colour(buf, sizeof buf, 0, 1000, 1000); /* ten sam format co DP24 Sypialni: "000003e803e8" */
    TEST_ASSERT_EQUAL_STRING("{\"21\":\"colour\",\"24\":\"000003e803e8\"}", buf);
    tuya_dps_colour(buf, sizeof buf, 240, 1000, 500);
    TEST_ASSERT_EQUAL_STRING("{\"21\":\"colour\",\"24\":\"00f003e801f4\"}", buf);
    tuya_dps_colour(buf, sizeof buf, 999, 2000, 2000); /* obcięcie: h 360, s/v 1000 */
    TEST_ASSERT_EQUAL_STRING("{\"21\":\"colour\",\"24\":\"016803e803e8\"}", buf);
}

void test_apply_light_temperature(void)
{
    dev_state_t s = online();
    s.temp = 999;
    TEST_ASSERT_TRUE(tuya_dps_apply("{\"dps\":{\"23\":250}}", DEV_TYPE_LIGHT, &s));
    TEST_ASSERT_EQUAL_UINT16(250, s.temp);
}

void test_apply_plug_voltage_and_current(void)
{
    dev_state_t s = online();
    s.voltage_dv = -1;
    s.current_ma = -1;
    TEST_ASSERT_TRUE(tuya_dps_apply("{\"dps\":{\"1\":true,\"9\":0,\"18\":105,\"19\":234,\"20\":2301}}", DEV_TYPE_PLUG, &s));
    TEST_ASSERT_EQUAL_INT32(105, s.current_ma);
    TEST_ASSERT_EQUAL_INT32(2301, s.voltage_dv);
    TEST_ASSERT_EQUAL_INT32(234, s.power_dw);
}

/* ---------- control-polish: tryb koloru (AC-3, AC-5, AC-7) ---------- */

void test_apply_light_mode_and_colour(void)
{
    dev_state_t s = online();
    TEST_ASSERT_TRUE(tuya_dps_apply("{\"dps\":{\"21\":\"colour\",\"24\":\"007803e801f4\"}}", DEV_TYPE_LIGHT, &s));
    TEST_ASSERT_TRUE(s.colour);
    TEST_ASSERT_EQUAL_UINT16(120, s.hue);
    TEST_ASSERT_EQUAL_UINT16(1000, s.sat);
    TEST_ASSERT_EQUAL_UINT16(500, s.val);

    TEST_ASSERT_TRUE(tuya_dps_apply("{\"dps\":{\"21\":\"white\"}}", DEV_TYPE_LIGHT, &s));
    TEST_ASSERT_FALSE(s.colour);
    TEST_ASSERT_EQUAL_UINT16(120, s.hue); /* kolor zapamiętany */

    TEST_ASSERT_TRUE(tuya_dps_apply("{\"dps\":{\"21\":\"scene\",\"24\":\"00F00064000A\"}}", DEV_TYPE_LIGHT, &s));
    TEST_ASSERT_FALSE(s.colour);
    TEST_ASSERT_EQUAL_UINT16(240, s.hue); /* wielkie litery hex */
    TEST_ASSERT_EQUAL_UINT16(100, s.sat);
    TEST_ASSERT_EQUAL_UINT16(10, s.val);
}

void test_apply_bad_colour_is_ignored(void)
{
    dev_state_t s = online();
    s.hue = 33;
    s.sat = 44;
    s.val = 55;
    tuya_dps_apply("{\"dps\":{\"24\":\"007803e801f\"}}", DEV_TYPE_LIGHT, &s);  /* 11 znaków */
    tuya_dps_apply("{\"dps\":{\"24\":\"zz7803e801f4\"}}", DEV_TYPE_LIGHT, &s); /* nie hex */
    TEST_ASSERT_EQUAL_UINT16(33, s.hue);
    TEST_ASSERT_EQUAL_UINT16(44, s.sat);
    TEST_ASSERT_EQUAL_UINT16(55, s.val);
}

void test_light_level_command_white_and_colour(void)
{
    dev_state_t s = online();
    s.colour = false;
    s.hue = 240;
    s.sat = 500;
    tuya_dps_light_level(buf, sizeof buf, &s, 500);
    TEST_ASSERT_EQUAL_STRING("{\"22\":500}", buf);

    s.colour = true;
    tuya_dps_light_level(buf, sizeof buf, &s, 300);
    TEST_ASSERT_EQUAL_STRING("{\"21\":\"colour\",\"24\":\"00f001f4012c\"}", buf);
    tuya_dps_light_level(buf, sizeof buf, &s, 5);
    TEST_ASSERT_EQUAL_STRING("{\"21\":\"colour\",\"24\":\"00f001f4000a\"}", buf); /* min. 10 */
    tuya_dps_light_level(buf, sizeof buf, &s, 2000);
    TEST_ASSERT_EQUAL_STRING("{\"21\":\"colour\",\"24\":\"00f001f403e8\"}", buf); /* max. 1000 */
}

void test_palette_uses_current_level(void)
{
    dev_state_t s = online();
    s.colour = false;
    s.brightness = 500;
    s.val = 900;
    tuya_dps_palette(buf, sizeof buf, &s, 0);
    TEST_ASSERT_EQUAL_STRING("{\"21\":\"colour\",\"24\":\"000003e801f4\"}", buf); /* biel: v = DP22 */

    s.colour = true;
    s.val = 250;
    tuya_dps_palette(buf, sizeof buf, &s, 120);
    TEST_ASSERT_EQUAL_STRING("{\"21\":\"colour\",\"24\":\"007803e800fa\"}", buf); /* kolor: v = bieżące V */

    s.val = 0;
    tuya_dps_palette(buf, sizeof buf, &s, 120);
    TEST_ASSERT_EQUAL_STRING("{\"21\":\"colour\",\"24\":\"007803e8000a\"}", buf); /* min. 10 */
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_control_json_matches_tinytuya_format);
    RUN_TEST(test_query_json_matches_tinytuya_format);
    RUN_TEST(test_control_json_truncates_like_snprintf);
    RUN_TEST(test_blind_control_commands);
    RUN_TEST(test_blind_control_rejects_unknown_value);
    RUN_TEST(test_blind_position_rounds_to_step_10_and_clamps);
    RUN_TEST(test_switch_uses_dp_per_type);
    RUN_TEST(test_brightness_clamped_to_10_1000);
    RUN_TEST(test_toggle_blind);
    RUN_TEST(test_toggle_moving_blind_stops);
    RUN_TEST(test_toggle_light_and_plug);
    RUN_TEST(test_toggle_unknown_or_offline_sends_nothing);
    RUN_TEST(test_apply_blind_push);
    RUN_TEST(test_apply_blind_moving_keeps_position);
    RUN_TEST(test_blind_dp1_sets_motion_direction);
    RUN_TEST(test_link_after_fail_needs_two_failures_before_first_online);
    RUN_TEST(test_apply_full_blind_status_from_device);
    RUN_TEST(test_apply_full_light_status_from_device);
    RUN_TEST(test_apply_light_partial_push_keeps_other_fields);
    RUN_TEST(test_apply_plug_with_power);
    RUN_TEST(test_apply_tolerates_whitespace);
    RUN_TEST(test_apply_does_not_change_link);
    RUN_TEST(test_apply_unknown_dps_or_garbage_returns_false_and_keeps_state);
    RUN_TEST(test_apply_ignores_dps_of_other_type);
    RUN_TEST(test_temperature_sets_white_mode_and_clamps);
    RUN_TEST(test_colour_hsv_as_12_hex_digits_like_device);
    RUN_TEST(test_apply_light_temperature);
    RUN_TEST(test_apply_plug_voltage_and_current);
    RUN_TEST(test_apply_light_mode_and_colour);
    RUN_TEST(test_apply_bad_colour_is_ignored);
    RUN_TEST(test_light_level_command_white_and_colour);
    RUN_TEST(test_palette_uses_current_level);
    return UNITY_END();
}
