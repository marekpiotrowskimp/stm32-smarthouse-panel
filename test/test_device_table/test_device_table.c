/*
 * Kontrakt tabeli urządzeń (spec: display-tiles, docs/SPEC.md O5):
 * 15 sterowalnych urządzeń w kolejności pomieszczeń, bez czujników bateryjnych.
 */
#include <string.h>
#include <unity.h>

#include "device_table.h"

void setUp(void) {}
void tearDown(void) {}

static const struct {
    const char *name;
    dev_type_t type;
} EXPECTED[] = {
    /* control-polish AC-11: rolety, światła, gniazdka; w obrębie typu kolejność pomieszczeń */
    { "Salon roleta", DEV_TYPE_BLIND },
    { "Salon s1", DEV_TYPE_BLIND },
    { "Salon s2", DEV_TYPE_BLIND },
    { "Salon s3", DEV_TYPE_BLIND },
    { "Kuchnia", DEV_TYPE_BLIND },
    { "Dziecko roleta", DEV_TYPE_BLIND },
    { "Biuro roleta 2", DEV_TYPE_BLIND },
    { "Pralnia", DEV_TYPE_BLIND },
    { "Kotłownia", DEV_TYPE_BLIND },
    { "Lampka Salon", DEV_TYPE_LIGHT },
    { "Sypialnia", DEV_TYPE_LIGHT },
    { "Dziecko lampa", DEV_TYPE_LIGHT },
    { "Salon duże okno", DEV_TYPE_PLUG },
    { "Biurko", DEV_TYPE_PLUG },
    { "Oświetlenie na zewnątrz", DEV_TYPE_PLUG },
};
#define EXPECTED_COUNT (sizeof EXPECTED / sizeof EXPECTED[0])

void test_table_has_15_devices(void)
{
    TEST_ASSERT_EQUAL_UINT(15, g_device_count);
}

void test_names_and_types_blinds_then_lights_then_plugs(void)
{
    TEST_ASSERT_EQUAL_UINT(EXPECTED_COUNT, g_device_count);
    for (size_t i = 0; i < EXPECTED_COUNT; i++) {
        TEST_ASSERT_EQUAL_STRING_MESSAGE(EXPECTED[i].name, g_devices[i].name, EXPECTED[i].name);
        TEST_ASSERT_EQUAL_INT_MESSAGE(EXPECTED[i].type, g_devices[i].type, EXPECTED[i].name);
    }
}

void test_battery_sensors_are_excluded(void)
{
    for (size_t i = 0; i < g_device_count; i++) {
        TEST_ASSERT_FALSE(strcmp(g_devices[i].name, "Garaż") == 0);
        TEST_ASSERT_FALSE(strcmp(g_devices[i].name, "Czujnik ruchu") == 0);
    }
}

void test_every_device_has_tuya_id_and_trimmed_name(void)
{
    TEST_ASSERT_EQUAL_UINT(15, g_device_count);
    for (size_t i = 0; i < g_device_count; i++) {
        size_t id_len = strlen(g_devices[i].id);
        size_t name_len = strlen(g_devices[i].name);
        TEST_ASSERT_TRUE_MESSAGE(id_len >= 20 && id_len <= 22, g_devices[i].name);
        TEST_ASSERT_TRUE_MESSAGE(name_len > 0 && g_devices[i].name[name_len - 1] != ' ', g_devices[i].name);
        for (size_t j = 0; j < i; j++)
            TEST_ASSERT_FALSE_MESSAGE(strcmp(g_devices[i].id, g_devices[j].id) == 0, "powtórzony devId");
    }
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_table_has_15_devices);
    RUN_TEST(test_names_and_types_blinds_then_lights_then_plugs);
    RUN_TEST(test_battery_sensors_are_excluded);
    RUN_TEST(test_every_device_has_tuya_id_and_trimmed_name);
    return UNITY_END();
}
