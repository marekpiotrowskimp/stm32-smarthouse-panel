/*
 * Testy sprzętowe klienta Tuya na prawdziwych urządzeniach użytkownika (spec: tuya-control).
 * Bez widocznych skutków: tylko odczyt stanu i komenda z NIEZMIENIONĄ wartością.
 * Środowisko: płytka w LAN AP2; żarówka „Sypialnia” 192.168.0.242 (AP2), roleta „Salon roleta”
 * 192.168.1.10 (AP1 przez NAT); klucze z wygenerowanego device_keys.c.
 */
#include <stdio.h>
#include <string.h>
#include <unity.h>

#include "app_platform.h"
#include "device_keys.h"
#include "device_table.h"
#include "tuya_client.h"
#include "tuya_dps.h"

enum { IDX_BULB, IDX_BLIND, IDX_FAKE, N_DEV };

static dev_info_t devs[N_DEV];
static tuya_endpoint_t eps[N_DEV];
static volatile uint32_t listener_calls[N_DEV];

void setUp(void) {}
void tearDown(void) {}

static void on_change(size_t idx)
{
    if (idx < N_DEV)
        listener_calls[idx]++;
}

static int find(const char *name)
{
    for (size_t i = 0; i < g_device_count; i++)
        if (strcmp(g_devices[i].name, name) == 0)
            return (int)i;
    return -1;
}

/* Czeka na stan link urządzenia; zwraca czas oczekiwania albo UINT32_MAX. */
static uint32_t wait_link(size_t idx, dev_link_t link, uint32_t timeout_ms)
{
    uint32_t t0 = app_millis();
    dev_state_t st;
    for (;;) {
        tuya_client_get_state(idx, &st);
        if (st.link == link)
            return app_millis() - t0;
        if (app_millis() - t0 >= timeout_ms)
            return UINT32_MAX;
        app_delay_ms(50);
    }
}

/* ---------- przygotowanie: sieć + konfiguracja ---------- */

void test_network_up_and_config_present(void)
{
    TEST_ASSERT_EQUAL_INT(0, app_net_start());
    net_status_t ns;
    uint32_t t0 = app_millis();
    do {
        app_delay_ms(100);
        app_net_get_status(&ns);
    } while (!ns.has_ip && app_millis() - t0 < 10000);
    TEST_ASSERT_TRUE_MESSAGE(ns.has_ip, "brak DHCP");

    int bulb = find("Sypialnia");
    int blind = find("Salon roleta");
    TEST_ASSERT_TRUE(bulb >= 0 && blind >= 0);
    TEST_ASSERT_EQUAL_UINT_MESSAGE(g_device_count, g_device_endpoint_count, "device_keys.c nie wygenerowany");

    devs[IDX_BULB] = g_devices[bulb];
    eps[IDX_BULB] = g_device_endpoints[bulb];
    eps[IDX_BULB].ip = 0; /* IP żarówki AP2 ma przyjść z wykrywania UDP 6667 */
    devs[IDX_BLIND] = g_devices[blind];
    eps[IDX_BLIND] = g_device_endpoints[blind];
    devs[IDX_FAKE] = (dev_info_t){ "Nieistniejące", "bf00000000000000fake00", DEV_TYPE_LIGHT };
    eps[IDX_FAKE] = (tuya_endpoint_t){ NET_IP4(192, 168, 0, 250), "0123456789abcdef" };
}

/* ---------- AC-5: start i stan początkowy ---------- */

void test_client_start_ok_and_states_unknown(void)
{
    TEST_ASSERT_EQUAL_INT(0, tuya_client_start(devs, eps, N_DEV, on_change));
    dev_state_t st;
    TEST_ASSERT_TRUE(tuya_client_get_state(IDX_BLIND, &st));
    TEST_ASSERT_FALSE(tuya_client_get_state(N_DEV, &st)); /* zły indeks */
}

/* ---------- AC-6: AP1 przez NAT ---------- */

void test_ap1_blind_online_with_state_within_5s(void)
{
    TEST_ASSERT_LESS_THAN_UINT32(5000u, wait_link(IDX_BLIND, DEV_LINK_ONLINE, 5000));
    dev_state_t st;
    tuya_client_get_state(IDX_BLIND, &st);
    TEST_ASSERT_LESS_OR_EQUAL_UINT8(100, st.open_pct);
    TEST_ASSERT_EQUAL_HEX32(NET_IP4(192, 168, 1, 10), tuya_client_ip(IDX_BLIND));
    TEST_ASSERT_NOT_EQUAL(0u, tuya_client_last_rx_ms(IDX_BLIND));
    TEST_ASSERT_GREATER_THAN_UINT32(0u, listener_calls[IDX_BLIND]);
}

/* ---------- AC-7: wykrywanie UDP + AP2 ---------- */

/*
 * Żarówka nie nadaje broadcastów, gdy ma połączenie lokalne, a po resecie płytki trzyma
 * nieaktualne połączenie z poprzedniego uruchomienia ok. 60 s – stąd limit 75 s
 * (od czystego stanu IP przychodzi w 3–6 s).
 */
void test_ap2_bulb_found_by_discovery_and_online_within_75s(void)
{
    TEST_ASSERT_LESS_THAN_UINT32(75000u, wait_link(IDX_BULB, DEV_LINK_ONLINE, 75000));
    TEST_ASSERT_EQUAL_HEX32(NET_IP4(192, 168, 0, 242), tuya_client_ip(IDX_BULB));
    dev_state_t st;
    tuya_client_get_state(IDX_BULB, &st);
    TEST_ASSERT_TRUE(st.brightness >= 10 && st.brightness <= 1000);
}

/* ---------- AC-8: urządzenie nieosiągalne ---------- */

void test_unreachable_device_offline_within_6s(void)
{
    TEST_ASSERT_LESS_THAN_UINT32(6000u, wait_link(IDX_FAKE, DEV_LINK_OFFLINE, 6000));
}

void test_send_to_offline_device_rejected(void)
{
    TEST_ASSERT_LESS_THAN_INT(0, tuya_client_send_dps(IDX_FAKE, "{\"20\":true}"));
    TEST_ASSERT_LESS_THAN_INT(0, tuya_client_send_dps(N_DEV, "{\"20\":true}"));
}

/* ---------- AC-9: sterowanie – pełny obieg bez zmiany stanu ---------- */

void test_control_roundtrip_with_unchanged_value(void)
{
    dev_state_t before;
    TEST_ASSERT_TRUE(tuya_client_get_state(IDX_BULB, &before));
    TEST_ASSERT_EQUAL_INT(DEV_LINK_ONLINE, before.link);

    char dps[24];
    tuya_dps_switch(dps, sizeof dps, DEV_TYPE_LIGHT, before.on); /* ta sama wartość */
    app_delay_ms(20);
    uint32_t t0 = app_millis();
    TEST_ASSERT_EQUAL_INT(0, tuya_client_send_dps(IDX_BULB, dps));

    uint32_t waited = 0;
    while (tuya_client_last_rx_ms(IDX_BULB) <= t0 && waited < 2000) {
        app_delay_ms(20);
        waited += 20;
    }
    TEST_ASSERT_GREATER_THAN_UINT32(t0, tuya_client_last_rx_ms(IDX_BULB)); /* odpowiedź w <= 2 s */

    dev_state_t after;
    tuya_client_get_state(IDX_BULB, &after);
    TEST_ASSERT_EQUAL_INT(DEV_LINK_ONLINE, after.link);
    TEST_ASSERT_EQUAL(before.on, after.on);
}

/* ---------- AC-10: heartbeat utrzymuje połączenie ---------- */

void test_heartbeat_keeps_connection_for_25s(void)
{
    app_delay_ms(25000);
    dev_state_t st;
    tuya_client_get_state(IDX_BLIND, &st);
    TEST_ASSERT_EQUAL_INT(DEV_LINK_ONLINE, st.link);
    TEST_ASSERT_LESS_THAN_UINT32(15000u, app_millis() - tuya_client_last_rx_ms(IDX_BLIND));
    tuya_client_get_state(IDX_BULB, &st);
    TEST_ASSERT_EQUAL_INT(DEV_LINK_ONLINE, st.link);
    TEST_ASSERT_LESS_THAN_UINT32(15000u, app_millis() - tuya_client_last_rx_ms(IDX_BULB));
}

static void test_task(void *arg)
{
    (void)arg;
    app_delay_ms(2000); /* czas na otwarcie portu szeregowego po resecie */
    UNITY_BEGIN();
    RUN_TEST(test_network_up_and_config_present);
    RUN_TEST(test_client_start_ok_and_states_unknown);
    RUN_TEST(test_ap1_blind_online_with_state_within_5s);
    RUN_TEST(test_ap2_bulb_found_by_discovery_and_online_within_75s);
    RUN_TEST(test_unreachable_device_offline_within_6s);
    RUN_TEST(test_send_to_offline_device_rejected);
    RUN_TEST(test_control_roundtrip_with_unchanged_value);
    RUN_TEST(test_heartbeat_keeps_connection_for_25s);
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
