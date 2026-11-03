/*
 * Kontrakt net_util (spec: stm32-boot-net, docs/SPEC.md R-NET-1).
 * Oczekiwane MAC policzone w Pythonie: zlib.crc32(struct.pack('<3I', *uid)).
 */
#include <string.h>
#include <unity.h>

#include "net_util.h"

static const uint32_t UID_A[3] = { 0x00350041u, 0x3436510Bu, 0x37363530u };
static const uint32_t UID_B[3] = { 0x00350041u, 0x3436510Bu, 0x37363531u }; /* różni się 1 bitem */
static const uint32_t UID_ZERO[3] = { 0, 0, 0 };

static char line[128];

void setUp(void)
{
    memset(line, 0x7F, sizeof line);
}

void tearDown(void) {}

static net_status_t status_up_with_ip(void)
{
    net_status_t st = {
        .link_up = true, .has_ip = true,
        .ip = NET_IP4(192, 168, 0, 57), .netmask = NET_IP4(255, 255, 255, 0), .gw = NET_IP4(192, 168, 0, 1),
        .mac = { 0x02, 0x80, 0xE1, 0xA9, 0x9B, 0x8F },
    };
    return st;
}

/* ---------- AC-1: MAC z UID ---------- */

void test_mac_from_uid_a_is_02_80_e1_a9_9b_8f(void)
{
    static const uint8_t exp[6] = { 0x02, 0x80, 0xE1, 0xA9, 0x9B, 0x8F };
    uint8_t mac[6] = { 0 };
    net_mac_from_uid(UID_A, mac);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(exp, mac, 6);
}

void test_mac_from_uid_differing_by_one_bit_is_02_80_e1_15_fc_ea(void)
{
    static const uint8_t exp[6] = { 0x02, 0x80, 0xE1, 0x15, 0xFC, 0xEA };
    uint8_t mac[6] = { 0 };
    net_mac_from_uid(UID_B, mac);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(exp, mac, 6);
}

void test_mac_from_zero_uid_is_02_80_e1_d5_c6_6f(void)
{
    static const uint8_t exp[6] = { 0x02, 0x80, 0xE1, 0xD5, 0xC6, 0x6F };
    uint8_t mac[6] = { 0 };
    net_mac_from_uid(UID_ZERO, mac);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(exp, mac, 6);
}

void test_mac_is_locally_administered_unicast_and_not_st_default(void)
{
    static const uint8_t st_default[6] = { 0x00, 0x80, 0xE1, 0x00, 0x00, 0x00 };
    uint8_t mac[6] = { 0 };
    net_mac_from_uid(UID_A, mac);
    TEST_ASSERT_EQUAL_HEX8(0x02, mac[0] & 0x03); /* bit1 = lokalny, bit0 = unicast */
    TEST_ASSERT_FALSE(memcmp(mac, st_default, 6) == 0);
}

/* ---------- AC-2: linia stanu sieci ---------- */

void test_status_line_link_up_with_ip(void)
{
    net_status_t st = status_up_with_ip();
    static const char exp[] = "NET link=up ip=192.168.0.57/24 gw=192.168.0.1 mac=02:80:E1:A9:9B:8F";
    TEST_ASSERT_EQUAL_INT(strlen(exp), net_format_status(line, sizeof line, &st));
    TEST_ASSERT_EQUAL_STRING(exp, line);
}

void test_status_line_link_up_without_ip(void)
{
    net_status_t st = status_up_with_ip();
    st.has_ip = false;
    static const char exp[] = "NET link=up ip=none mac=02:80:E1:A9:9B:8F";
    TEST_ASSERT_EQUAL_INT(strlen(exp), net_format_status(line, sizeof line, &st));
    TEST_ASSERT_EQUAL_STRING(exp, line);
}

void test_status_line_link_down_ignores_ip(void)
{
    net_status_t st = status_up_with_ip();
    st.link_up = false;
    static const char exp[] = "NET link=down mac=02:80:E1:A9:9B:8F";
    TEST_ASSERT_EQUAL_INT(strlen(exp), net_format_status(line, sizeof line, &st));
    TEST_ASSERT_EQUAL_STRING(exp, line);
}

void test_status_line_prefix_length_from_netmask_16(void)
{
    net_status_t st = status_up_with_ip();
    st.ip = NET_IP4(10, 1, 2, 3);
    st.netmask = NET_IP4(255, 255, 0, 0);
    st.gw = NET_IP4(10, 1, 0, 1);
    static const char exp[] = "NET link=up ip=10.1.2.3/16 gw=10.1.0.1 mac=02:80:E1:A9:9B:8F";
    TEST_ASSERT_EQUAL_INT(strlen(exp), net_format_status(line, sizeof line, &st));
    TEST_ASSERT_EQUAL_STRING(exp, line);
}

void test_status_line_truncates_like_snprintf_and_returns_full_length(void)
{
    net_status_t st = status_up_with_ip();
    static const char full[] = "NET link=up ip=192.168.0.57/24 gw=192.168.0.1 mac=02:80:E1:A9:9B:8F";
    TEST_ASSERT_EQUAL_INT(strlen(full), net_format_status(line, 12, &st));
    TEST_ASSERT_EQUAL_STRING("NET link=up", line); /* 11 znaków + '\0' */
}

void test_status_line_with_zero_capacity_writes_nothing(void)
{
    net_status_t st = status_up_with_ip();
    static const char full[] = "NET link=up ip=192.168.0.57/24 gw=192.168.0.1 mac=02:80:E1:A9:9B:8F";
    TEST_ASSERT_EQUAL_INT(strlen(full), net_format_status(line, 0, &st));
    TEST_ASSERT_EQUAL_HEX8(0x7F, (uint8_t)line[0]);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_mac_from_uid_a_is_02_80_e1_a9_9b_8f);
    RUN_TEST(test_mac_from_uid_differing_by_one_bit_is_02_80_e1_15_fc_ea);
    RUN_TEST(test_mac_from_zero_uid_is_02_80_e1_d5_c6_6f);
    RUN_TEST(test_mac_is_locally_administered_unicast_and_not_st_default);
    RUN_TEST(test_status_line_link_up_with_ip);
    RUN_TEST(test_status_line_link_up_without_ip);
    RUN_TEST(test_status_line_link_down_ignores_ip);
    RUN_TEST(test_status_line_prefix_length_from_netmask_16);
    RUN_TEST(test_status_line_truncates_like_snprintf_and_returns_full_length);
    RUN_TEST(test_status_line_with_zero_capacity_writes_nothing);
    return UNITY_END();
}
