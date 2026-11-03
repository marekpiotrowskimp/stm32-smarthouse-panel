/*
 * Kontrakt sys_util (spec: system-reliability, docs/SPEC.md R-SYS-1, R-SYS-2):
 * nadzór zadań przed odświeżeniem IWDG, przyczyna resetu, linia logu sterty.
 */
#include <string.h>
#include <unity.h>

#include "sys_util.h"

static sys_wdg_t w;
static char buf[96];

void setUp(void)
{
    sys_wdg_init(&w);
    memset(buf, 0x7F, sizeof buf);
}

void tearDown(void) {}

/* ---------- AC-1: nadzór zadań ---------- */

void test_wdg_all_alive_and_stalled_task(void)
{
    int a = sys_wdg_register(&w, "main", 1000);
    int b = sys_wdg_register(&w, "tuya", 1000);
    TEST_ASSERT_EQUAL_INT(0, a);
    TEST_ASSERT_EQUAL_INT(1, b);
    TEST_ASSERT_EQUAL_INT(-1, sys_wdg_check(&w, 1000));
    sys_wdg_alive(&w, a, 2500);
    TEST_ASSERT_EQUAL_INT(-1, sys_wdg_check(&w, 3999)); /* b milczy 2999 ms */
    TEST_ASSERT_EQUAL_INT(b, sys_wdg_check(&w, 4000));  /* b milczy 3000 ms -> zawieszone */
    TEST_ASSERT_EQUAL_STRING("tuya", w.name[b]);
    sys_wdg_alive(&w, b, 4000);
    TEST_ASSERT_EQUAL_INT(-1, sys_wdg_check(&w, 4000));
    TEST_ASSERT_EQUAL_INT(a, sys_wdg_check(&w, 5500));  /* teraz a milczy 3000 ms */
}

void test_wdg_wraparound_and_limits(void)
{
    TEST_ASSERT_EQUAL_INT(-1, sys_wdg_check(&w, 123)); /* brak zadań */
    int a = sys_wdg_register(&w, "gui", 0xFFFFF800u);
    TEST_ASSERT_EQUAL_INT(0, a);
    TEST_ASSERT_EQUAL_INT(-1, sys_wdg_check(&w, 0x00000100u)); /* 0x900 = 2304 ms (mod 2^32) */
    TEST_ASSERT_EQUAL_INT(a, sys_wdg_check(&w, 0x00000400u));  /* 0xC00 = 3072 ms */
    sys_wdg_alive(&w, a, 0xFFFFFF00u);
    TEST_ASSERT_EQUAL_INT(-1, sys_wdg_check(&w, 0x00000A00u));  /* 2816 ms po przekręceniu */
    TEST_ASSERT_EQUAL_INT(a, sys_wdg_check(&w, 0x00000C00u));   /* 3328 ms */
    sys_wdg_alive(&w, 7, 0x00000C00u);                          /* nieznany id – bez efektu */
    sys_wdg_alive(&w, -1, 0x00000C00u);
    TEST_ASSERT_EQUAL_INT(a, sys_wdg_check(&w, 0x00000C00u));

    sys_wdg_init(&w);
    for (int i = 0; i < SYS_WDG_MAX_TASKS; i++)
        TEST_ASSERT_EQUAL_INT(i, sys_wdg_register(&w, "t", 0));
    TEST_ASSERT_EQUAL_INT(-1, sys_wdg_register(&w, "9.", 0)); /* ponad 8 */
    TEST_ASSERT_EQUAL_UINT8(SYS_WDG_MAX_TASKS, w.count);
}

/* ---------- AC-2, AC-3: przyczyna resetu ---------- */

void test_reset_reason_priority(void)
{
    TEST_ASSERT_EQUAL_STRING("watchdog", sys_reset_reason(SYS_RST_IWDG | SYS_RST_PIN));
    TEST_ASSERT_EQUAL_STRING("watchdog okienkowy", sys_reset_reason(SYS_RST_WWDG | SYS_RST_PIN));
    TEST_ASSERT_EQUAL_STRING("programowy", sys_reset_reason(SYS_RST_SFT | SYS_RST_PIN));
    TEST_ASSERT_EQUAL_STRING("niskie zasilanie", sys_reset_reason(SYS_RST_LPWR | SYS_RST_PIN));
    TEST_ASSERT_EQUAL_STRING("zasilanie", sys_reset_reason(SYS_RST_POR | SYS_RST_BOR | SYS_RST_PIN));
    TEST_ASSERT_EQUAL_STRING("zasilanie", sys_reset_reason(SYS_RST_BOR | SYS_RST_PIN));
    TEST_ASSERT_EQUAL_STRING("przycisk RESET", sys_reset_reason(SYS_RST_PIN));
    TEST_ASSERT_EQUAL_STRING("nieznany", sys_reset_reason(0));
    TEST_ASSERT_EQUAL_STRING("watchdog", sys_reset_reason(SYS_RST_IWDG | SYS_RST_SFT | SYS_RST_POR));
}

void test_reset_line_with_and_without_detail(void)
{
    int n = sys_reset_line(buf, sizeof buf, SYS_RST_IWDG | SYS_RST_PIN, "tuya");
    TEST_ASSERT_EQUAL_STRING("RESET: watchdog (zadanie: tuya)", buf);
    TEST_ASSERT_EQUAL_INT((int)strlen(buf), n);
    sys_reset_line(buf, sizeof buf, SYS_RST_POR | SYS_RST_PIN, NULL);
    TEST_ASSERT_EQUAL_STRING("RESET: zasilanie", buf);
    sys_reset_line(buf, sizeof buf, SYS_RST_PIN, "");
    TEST_ASSERT_EQUAL_STRING("RESET: przycisk RESET", buf);
    sys_reset_line(buf, 8, SYS_RST_IWDG, "gui"); /* obcięcie jak snprintf */
    TEST_ASSERT_EQUAL_STRING("RESET: ", buf);
}

/* ---------- AC-12: sterta ---------- */

void test_heap_line_ok_and_low_threshold(void)
{
    sys_heap_line(buf, sizeof buf, 40000, 30000, 65536);
    TEST_ASSERT_EQUAL_STRING("HEAP free=40000 min=30000 (45%) OK", buf);
    sys_heap_line(buf, sizeof buf, 20000, 13107, 65536); /* 19,99% */
    TEST_ASSERT_EQUAL_STRING("HEAP free=20000 min=13107 (19%) NISKO", buf);
    TEST_ASSERT_FALSE(sys_heap_ok(13107, 65536));
    TEST_ASSERT_TRUE(sys_heap_ok(13108, 65536)); /* 20,0003% */
    sys_heap_line(buf, sizeof buf, 13108, 13108, 65536);
    TEST_ASSERT_EQUAL_STRING("HEAP free=13108 min=13108 (20%) OK", buf);
    TEST_ASSERT_FALSE(sys_heap_ok(0, 0)); /* brak sterty – nie OK */
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_wdg_all_alive_and_stalled_task);
    RUN_TEST(test_wdg_wraparound_and_limits);
    RUN_TEST(test_reset_reason_priority);
    RUN_TEST(test_reset_line_with_and_without_detail);
    RUN_TEST(test_heap_line_ok_and_low_threshold);
    return UNITY_END();
}
