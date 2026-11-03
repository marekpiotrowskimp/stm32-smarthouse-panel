/*
 * Kontrakt ui_idle (spec: system-reliability, docs/SPEC.md §6.6): po 60 s bez dotyku powrót na stronę 1,
 * po 5 min przyciemnienie; pierwszy dotyk po przyciemnieniu tylko wybudza.
 */
#include <unity.h>

#include "ui_idle.h"

static ui_idle_t p;

void setUp(void)
{
    ui_idle_init(&p, 1000);
}

void tearDown(void) {}

static unsigned tick(uint32_t now)
{
    return ui_idle_tick(&p, now, UI_IDLE_HOME_MS, UI_IDLE_DIM_MS);
}

/* ---------- AC-7 ---------- */

void test_go_home_then_dim_once_per_idle_period(void)
{
    TEST_ASSERT_EQUAL_UINT(UI_IDLE_NONE, tick(1000 + 59999));
    TEST_ASSERT_EQUAL_UINT(UI_IDLE_GO_HOME, tick(1000 + 60000));
    TEST_ASSERT_EQUAL_UINT(UI_IDLE_NONE, tick(1000 + 60001));   /* raz */
    TEST_ASSERT_EQUAL_UINT(UI_IDLE_NONE, tick(1000 + 299999));
    TEST_ASSERT_EQUAL_UINT(UI_IDLE_DIM, tick(1000 + 300000));
    TEST_ASSERT_TRUE(p.dimmed);
    TEST_ASSERT_EQUAL_UINT(UI_IDLE_NONE, tick(1000 + 400000));  /* raz */
}

void test_both_events_at_once_after_long_gap(void)
{
    /* np. zadanie GUI wywołane dopiero po 6 min – oba zdarzenia naraz */
    TEST_ASSERT_EQUAL_UINT(UI_IDLE_GO_HOME | UI_IDLE_DIM, tick(1000 + 360000));
    TEST_ASSERT_EQUAL_UINT(UI_IDLE_NONE, tick(1000 + 360001));
}

void test_touch_restarts_period_and_wraparound(void)
{
    TEST_ASSERT_EQUAL_UINT(UI_IDLE_GO_HOME, tick(61000));
    TEST_ASSERT_FALSE(ui_idle_touch(&p, 70000)); /* nie przyciemniony – zwykły dotyk */
    TEST_ASSERT_EQUAL_UINT(UI_IDLE_NONE, tick(70000 + 59999));
    TEST_ASSERT_EQUAL_UINT(UI_IDLE_GO_HOME, tick(70000 + 60000)); /* nowy okres */

    ui_idle_init(&p, 0xFFFFF000u);
    TEST_ASSERT_EQUAL_UINT(UI_IDLE_NONE, tick(0x0000D000u));                    /* 0x1000 + 0xD000 = 57 344 ms */
    TEST_ASSERT_EQUAL_UINT(UI_IDLE_GO_HOME, tick(0xFFFFF000u + 60000u));         /* po przekręceniu */
    TEST_ASSERT_EQUAL_UINT(UI_IDLE_DIM, tick(0xFFFFF000u + 300000u));
}

void test_custom_short_timeouts(void)
{
    TEST_ASSERT_EQUAL_UINT(UI_IDLE_NONE, ui_idle_tick(&p, 1999, 1000, 2000));
    TEST_ASSERT_EQUAL_UINT(UI_IDLE_GO_HOME, ui_idle_tick(&p, 2000, 1000, 2000));
    TEST_ASSERT_EQUAL_UINT(UI_IDLE_DIM, ui_idle_tick(&p, 3000, 1000, 2000));
}

/* ---------- AC-8 ---------- */

void test_first_touch_after_dim_only_wakes(void)
{
    TEST_ASSERT_EQUAL_UINT(UI_IDLE_GO_HOME | UI_IDLE_DIM, tick(1000 + 300000));
    TEST_ASSERT_TRUE(ui_idle_touch(&p, 400000));  /* wybudza */
    TEST_ASSERT_FALSE(p.dimmed);
    TEST_ASSERT_FALSE(ui_idle_touch(&p, 400020)); /* kolejne próbki / dotknięcia – normalne */
    TEST_ASSERT_EQUAL_UINT(UI_IDLE_NONE, tick(400020 + 59999));
    TEST_ASSERT_EQUAL_UINT(UI_IDLE_GO_HOME, tick(400020 + 60000));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_go_home_then_dim_once_per_idle_period);
    RUN_TEST(test_both_events_at_once_after_long_gap);
    RUN_TEST(test_touch_restarts_period_and_wraparound);
    RUN_TEST(test_custom_short_timeouts);
    RUN_TEST(test_first_touch_after_dim_only_wakes);
    return UNITY_END();
}
