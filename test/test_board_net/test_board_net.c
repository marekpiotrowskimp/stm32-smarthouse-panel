/*
 * Testy sprzętowe na STM32F746G-DISCO (spec: stm32-boot-net, docs/SPEC.md R-NET-1).
 * Wymagania środowiska: płytka podłączona przez ST-LINK, kabel Ethernet w LAN AP2 (192.168.0.0/24),
 * AP1 (192.168.1.1) osiągalny przez NAT AP2. Wyniki Unity idą przez VCP (USART1, 115200).
 */
#include <unity.h>

#include "app_platform.h"
#include "net_util.h"
#include "stm32f7xx_hal.h"

#define AP2_ROUTER   NET_IP4(192, 168, 0, 1)
#define AP1_ROUTER   NET_IP4(192, 168, 1, 1)
#define AP2_SUBNET   NET_IP4(192, 168, 0, 0)
#define MASK_24      NET_IP4(255, 255, 255, 0)

static net_status_t st;

void setUp(void) {}
void tearDown(void) {}

/* Czeka, aż warunek na stanie sieci będzie spełniony; zwraca czas oczekiwania lub UINT32_MAX. */
static uint32_t wait_status(bool (*cond)(const net_status_t *), uint32_t timeout_ms)
{
    uint32_t t0 = app_millis();
    for (;;) {
        app_net_get_status(&st);
        if (cond(&st))
            return app_millis() - t0;
        if (app_millis() - t0 >= timeout_ms)
            return UINT32_MAX;
        app_delay_ms(100);
    }
}

static bool is_link_up(const net_status_t *s) { return s->link_up; }
static bool has_ip(const net_status_t *s) { return s->has_ip; }

/* ---------- AC-3: FreeRTOS ---------- */

void test_tests_run_inside_freertos_task(void)
{
    TEST_ASSERT_TRUE(app_rtos_is_running());
}

void test_system_clock_is_216mhz(void)
{
    TEST_ASSERT_EQUAL_UINT32(216000000u, HAL_RCC_GetSysClockFreq());
}

/* ---------- AC-4: Ethernet + DHCP ---------- */

void test_net_start_returns_ok(void)
{
    TEST_ASSERT_EQUAL_INT(0, app_net_start());
}

void test_ethernet_link_up_within_5s(void)
{
    TEST_ASSERT_LESS_THAN_UINT32(5000u, wait_status(is_link_up, 5000));
}

void test_dhcp_lease_from_ap2_within_10s(void)
{
    TEST_ASSERT_LESS_THAN_UINT32(10000u, wait_status(has_ip, 10000));
    TEST_ASSERT_EQUAL_HEX32(AP2_SUBNET, st.ip & MASK_24);
    TEST_ASSERT_EQUAL_HEX32(MASK_24, st.netmask);
    TEST_ASSERT_EQUAL_HEX32(AP2_ROUTER, st.gw);
    TEST_ASSERT_NOT_EQUAL(0u, st.ip & 0xFFu);
    TEST_ASSERT_NOT_EQUAL(255u, st.ip & 0xFFu);
}

/* ---------- AC-5: MAC z UID ---------- */

void test_interface_mac_is_derived_from_mcu_uid(void)
{
    const uint32_t uid[3] = { HAL_GetUIDw0(), HAL_GetUIDw1(), HAL_GetUIDw2() };
    uint8_t exp[6] = { 0 };
    net_mac_from_uid(uid, exp);
    app_net_get_status(&st);
    TEST_ASSERT_EQUAL_HEX8(0x02, st.mac[0]);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(exp, st.mac, 6);
}

/* ---------- AC-6: łączność TCP ---------- */

void test_tcp_connect_to_ap2_router_port_80(void)
{
    TEST_ASSERT_EQUAL_INT(0, app_net_tcp_probe(AP2_ROUTER, 80, 3000));
}

void test_tcp_connect_to_ap1_router_through_nat(void)
{
    TEST_ASSERT_EQUAL_INT(0, app_net_tcp_probe(AP1_ROUTER, 80, 3000));
}

void test_tcp_connect_to_closed_port_fails_within_3s(void)
{
    uint32_t t0 = app_millis();
    int r = app_net_tcp_probe(AP2_ROUTER, 1, 3000);
    uint32_t dt = app_millis() - t0;
    TEST_ASSERT_TRUE(app_rtos_is_running()); /* bez działającej sieci ten test nic nie mówi */
    TEST_ASSERT_LESS_THAN_INT(0, r);
    TEST_ASSERT_LESS_OR_EQUAL_UINT32(3500u, dt);
}

static void test_task(void *arg)
{
    (void)arg;
    app_delay_ms(2000); /* czas na otwarcie portu szeregowego po resecie */
    UNITY_BEGIN();
    RUN_TEST(test_tests_run_inside_freertos_task);
    RUN_TEST(test_system_clock_is_216mhz);
    RUN_TEST(test_net_start_returns_ok);
    RUN_TEST(test_ethernet_link_up_within_5s);
    RUN_TEST(test_dhcp_lease_from_ap2_within_10s);
    RUN_TEST(test_interface_mac_is_derived_from_mcu_uid);
    RUN_TEST(test_tcp_connect_to_ap2_router_port_80);
    RUN_TEST(test_tcp_connect_to_ap1_router_through_nat);
    RUN_TEST(test_tcp_connect_to_closed_port_fails_within_3s);
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
