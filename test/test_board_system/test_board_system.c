/*
 * Testy sprzętowe niezawodności (spec: system-reliability, docs/SPEC.md R-SYS-1).
 * Uwaga: IWDG raz uruchomiony działa do resetu – po testach pętla końcowa dalej zgłasza żywotność.
 */
#include <unity.h>

#include "app_platform.h"
#include "stm32f7xx_hal.h"
#include "sys_util.h"

#define RCC_CSR_RESET_FLAGS 0xFE000000u /* BORRSTF..LPWRRSTF (bity 25..31) */

static int s_id = -1;

void setUp(void) {}
void tearDown(void) {}

/* ---------- AC-5: przyczyna resetu ---------- */

void test_reset_flags_captured_and_cleared(void)
{
    uint32_t f = app_reset_flags();
    /* po wgraniu ST-Link resetuje przez NRST albo SYSRESETREQ; po włączeniu zasilania – POR */
    TEST_ASSERT_NOT_EQUAL(0u, f & (SYS_RST_PIN | SYS_RST_SFT | SYS_RST_POR));
    TEST_ASSERT_EQUAL_HEX32(0u, RCC->CSR & RCC_CSR_RESET_FLAGS); /* wyczyszczone (RMVF) */
    TEST_ASSERT_EQUAL_STRING("", app_reset_detail());            /* nie był to reset z watchdoga */
}

/* ---------- AC-4: IWDG ~4 s, odświeżany, gdy zadania żyją ---------- */

void test_watchdog_runs_4s_and_is_fed(void)
{
    s_id = app_wdg_register("test");
    TEST_ASSERT_TRUE(s_id >= 0);
    TEST_ASSERT_FALSE(app_watchdog_running());
    app_watchdog_start();
    app_delay_ms(100);
    TEST_ASSERT_TRUE(app_watchdog_running());
    while (IWDG->SR != 0) { /* rejestry PR / RLR czytelne po zakończeniu aktualizacji */
    }
    TEST_ASSERT_EQUAL_UINT32(4u, IWDG->PR);     /* /64 */
    TEST_ASSERT_EQUAL_UINT32(1999u, IWDG->RLR); /* (1999 + 1) * 64 / 32 kHz = 4,0 s */

    uint32_t t0 = app_millis();
    for (unsigned i = 0; i < 12; i++) { /* 6 s > 4 s IWDG */
        app_wdg_alive(s_id);
        app_delay_ms(500);
    }
    TEST_ASSERT_GREATER_OR_EQUAL_UINT32(6000u, app_millis() - t0); /* bez resetu licznik by się wyzerował */
}

static void test_task(void *arg)
{
    (void)arg;
    app_delay_ms(2000); /* czas na otwarcie portu szeregowego po resecie */
    UNITY_BEGIN();
    RUN_TEST(test_reset_flags_captured_and_cleared);
    RUN_TEST(test_watchdog_runs_4s_and_is_fed);
    UNITY_END();
    for (;;) { /* IWDG działa dalej */
        app_wdg_alive(s_id);
        app_delay_ms(500);
    }
}

int main(void)
{
    app_board_init();
    app_rtos_start(test_task);
    return 0;
}
