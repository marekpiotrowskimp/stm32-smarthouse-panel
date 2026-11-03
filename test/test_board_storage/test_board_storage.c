/*
 * Testy sprzętowe zapisu grup w QSPI N25Q128A (spec: virtual-groups, docs/SPEC.md §4.4, R-GRP-6, R-GRP-7).
 * Używają slotów testowych 0xFF8000 / 0xFFA000 – grupy aplikacji (0xFFC000 / 0xFFE000) zostają nietknięte.
 */
#include <string.h>
#include <unity.h>

#include "app_platform.h"
#include "app_storage.h"
#include "groups.h"

#define TEST_SLOT0 0x00FF8000u
#define TEST_SLOT1 0x00FFA000u

static groups_t gs_a, gs_b, out;
static uint8_t raw[GROUPS_SLOT_SIZE];

void setUp(void) {}
void tearDown(void) {}

static void make(groups_t *gs, const char *name)
{
    memset(gs, 0, sizeof *gs);
    gs->count = 1;
    gs->g[0] = (group_cfg_t){ .id = 1, .type = DEV_TYPE_BLIND, .member_count = 2, .position = 3 };
    strcpy(gs->g[0].name, name);
    strcpy(gs->g[0].members[0], "bfdemo0000000000000008");
    strcpy(gs->g[0].members[1], "532877073c6105ca9945");
}

void test_qspi_init(void)
{
    TEST_ASSERT_EQUAL_INT(0, app_storage_init());
    TEST_ASSERT_EQUAL_INT(0, app_storage_erase(TEST_SLOT0, GROUPS_SLOT_SIZE));
    TEST_ASSERT_EQUAL_INT(0, app_storage_erase(TEST_SLOT1, GROUPS_SLOT_SIZE));
    TEST_ASSERT_EQUAL_INT(0, app_storage_read(TEST_SLOT0, raw, 16));
    for (unsigned i = 0; i < 16; i++)
        TEST_ASSERT_EQUAL_HEX8(0xFF, raw[i]); /* skasowany */
}

void test_both_slots_empty_load_returns_minus_one(void)
{
    out.count = 7;
    TEST_ASSERT_EQUAL_INT(-1, app_storage_groups_load(TEST_SLOT0, TEST_SLOT1, &out));
    TEST_ASSERT_EQUAL_UINT8(7, out.count);
}

void test_first_save_goes_to_slot0_and_loads_back(void)
{
    make(&gs_a, "Parter");
    TEST_ASSERT_EQUAL_INT(0, app_storage_groups_save(TEST_SLOT0, TEST_SLOT1, &gs_a));
    TEST_ASSERT_EQUAL_INT(0, app_storage_read(TEST_SLOT0, raw, 4));
    TEST_ASSERT_EQUAL_MEMORY("GRP1", raw, 4);
    memset(&out, 0, sizeof out);
    TEST_ASSERT_EQUAL_INT(0, app_storage_groups_load(TEST_SLOT0, TEST_SLOT1, &out));
    TEST_ASSERT_EQUAL_UINT8(1, out.count);
    TEST_ASSERT_EQUAL_STRING("Parter", out.g[0].name);
    TEST_ASSERT_EQUAL_STRING("532877073c6105ca9945", out.g[0].members[1]);
    TEST_ASSERT_EQUAL_UINT8(3, out.g[0].position);
}

void test_second_save_alternates_to_slot1_keeping_slot0(void)
{
    make(&gs_b, "Piętro");
    TEST_ASSERT_EQUAL_INT(1, app_storage_groups_save(TEST_SLOT0, TEST_SLOT1, &gs_b));
    TEST_ASSERT_EQUAL_INT(1, app_storage_groups_load(TEST_SLOT0, TEST_SLOT1, &out));
    TEST_ASSERT_EQUAL_STRING("Piętro", out.g[0].name);
    /* poprzednia wersja nadal poprawna w slocie 0 */
    TEST_ASSERT_EQUAL_INT(0, app_storage_read(TEST_SLOT0, raw, sizeof raw));
    static groups_t old;
    uint32_t seq;
    TEST_ASSERT_EQUAL_INT(0, groups_decode(raw, sizeof raw, &old, &seq));
    TEST_ASSERT_EQUAL_STRING("Parter", old.g[0].name);
}

void test_torn_write_falls_back_to_previous_version(void)
{
    /* R-GRP-7: utrata zasilania w trakcie zapisu do slotu 0 (nowszego) – po starcie wersja ze slotu 1 */
    static groups_t c;
    make(&c, "Przerwany");
    int len = groups_encode(&c, 1000, raw, sizeof raw);
    TEST_ASSERT_TRUE(len > 0);
    TEST_ASSERT_EQUAL_INT(0, app_storage_erase(TEST_SLOT0, GROUPS_SLOT_SIZE));
    TEST_ASSERT_EQUAL_INT(0, app_storage_write(TEST_SLOT0, raw, (size_t)len / 2)); /* połowa ramki */
    TEST_ASSERT_EQUAL_INT(1, app_storage_groups_load(TEST_SLOT0, TEST_SLOT1, &out));
    TEST_ASSERT_EQUAL_STRING("Piętro", out.g[0].name);
}

void test_groups_survive_qspi_reinit(void)
{
    /* R-GRP-6 (część automatyczna): ponowna inicjalizacja jak po starcie płytki */
    TEST_ASSERT_EQUAL_INT(0, app_storage_init());
    TEST_ASSERT_EQUAL_INT(1, app_storage_groups_load(TEST_SLOT0, TEST_SLOT1, &out));
    TEST_ASSERT_EQUAL_STRING("Piętro", out.g[0].name);
    TEST_ASSERT_EQUAL_UINT8(2, out.g[0].member_count);
}

void test_save_after_torn_write_overwrites_torn_slot(void)
{
    make(&gs_a, "Po awarii");
    TEST_ASSERT_EQUAL_INT(0, app_storage_groups_save(TEST_SLOT0, TEST_SLOT1, &gs_a)); /* bieżący = 1 -> zapis do 0 */
    TEST_ASSERT_EQUAL_INT(0, app_storage_groups_load(TEST_SLOT0, TEST_SLOT1, &out));
    TEST_ASSERT_EQUAL_STRING("Po awarii", out.g[0].name);
}

static void test_task(void *arg)
{
    (void)arg;
    app_delay_ms(2000); /* czas na otwarcie portu szeregowego po resecie */
    UNITY_BEGIN();
    RUN_TEST(test_qspi_init);
    RUN_TEST(test_both_slots_empty_load_returns_minus_one);
    RUN_TEST(test_first_save_goes_to_slot0_and_loads_back);
    RUN_TEST(test_second_save_alternates_to_slot1_keeping_slot0);
    RUN_TEST(test_torn_write_falls_back_to_previous_version);
    RUN_TEST(test_groups_survive_qspi_reinit);
    RUN_TEST(test_save_after_torn_write_overwrites_torn_slot);
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
