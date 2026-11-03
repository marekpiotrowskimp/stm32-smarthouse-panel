/*
 * Kontrakt wygenerowanej konfiguracji połączeń (spec: tuya-control).
 * Sprawdza spójność z g_devices i adresy znane z sieci użytkownika – nie wypisuje kluczy.
 */
#include <ctype.h>
#include <string.h>
#include <unity.h>

#include "device_keys.h"
#include "device_table.h"
#include "net_util.h"

void setUp(void) {}
void tearDown(void) {}

static int find(const char *name)
{
    for (size_t i = 0; i < g_device_count; i++)
        if (strcmp(g_devices[i].name, name) == 0)
            return (int)i;
    return -1;
}

void test_one_endpoint_per_device(void)
{
    TEST_ASSERT_EQUAL_UINT(15, g_device_endpoint_count);
    TEST_ASSERT_EQUAL_UINT(g_device_count, g_device_endpoint_count);
}

void test_every_key_is_16_printable_chars(void)
{
    TEST_ASSERT_EQUAL_UINT(15, g_device_endpoint_count);
    for (size_t i = 0; i < g_device_endpoint_count; i++) {
        const char *k = g_device_endpoints[i].key;
        TEST_ASSERT_EQUAL_UINT_MESSAGE(16, strlen(k), g_devices[i].name);
        for (size_t j = 0; j < 16; j++)
            TEST_ASSERT_TRUE_MESSAGE(isprint((unsigned char)k[j]), g_devices[i].name);
    }
}

void test_ips_follow_device_order(void)
{
    int sypialnia = find("Sypialnia");
    int salon = find("Salon roleta");
    int biuro = find("Biuro roleta 2");
    TEST_ASSERT_TRUE(sypialnia >= 0 && salon >= 0 && biuro >= 0);
    TEST_ASSERT_EQUAL_UINT(15, g_device_endpoint_count);
    TEST_ASSERT_EQUAL_HEX32(NET_IP4(192, 168, 0, 242), g_device_endpoints[sypialnia].ip); /* AP2 */
    TEST_ASSERT_EQUAL_HEX32(NET_IP4(192, 168, 1, 10), g_device_endpoints[salon].ip);      /* AP1 */
    TEST_ASSERT_EQUAL_HEX32(NET_IP4(192, 168, 0, 188), g_device_endpoints[biuro].ip);     /* AP2 */
    int zew = find("Oświetlenie na zewnątrz"); /* control-polish AC-12: adres ze skanu AP1 */
    TEST_ASSERT_TRUE(zew >= 0);
    TEST_ASSERT_EQUAL_HEX32(NET_IP4(192, 168, 1, 11), g_device_endpoints[zew].ip);
}

void test_device_without_known_ip_has_zero(void)
{
    int lampka = find("Lampka Salon"); /* offline przy skanowaniu – brak IP */
    TEST_ASSERT_TRUE(lampka >= 0);
    TEST_ASSERT_EQUAL_UINT(15, g_device_endpoint_count);
    TEST_ASSERT_EQUAL_HEX32(0, g_device_endpoints[lampka].ip);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_one_endpoint_per_device);
    RUN_TEST(test_every_key_is_16_printable_chars);
    RUN_TEST(test_ips_follow_device_order);
    RUN_TEST(test_device_without_known_ip_has_zero);
    return UNITY_END();
}
