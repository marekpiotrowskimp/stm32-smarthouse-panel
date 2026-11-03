/*
 * Kontrakt modelu grup wirtualnych (spec: virtual-groups, docs/SPEC.md §4.4, §6.2, §6.4, R-GRP-2..7, 9).
 */
#include <string.h>
#include <unity.h>

#include "device_table.h"
#include "groups.h"
#include "tuya_proto.h"

enum { R1, R2, R3, R4, L1, L2, P1, N_DEV };

static const dev_info_t DEVS[N_DEV] = {
    { "Roleta 1", "id01", DEV_TYPE_BLIND }, { "Roleta 2", "id02", DEV_TYPE_BLIND },
    { "Roleta 3", "id03", DEV_TYPE_BLIND }, { "Roleta 4", "id04", DEV_TYPE_BLIND },
    { "Lampa 1", "id05", DEV_TYPE_LIGHT },  { "Lampa 2", "id06", DEV_TYPE_LIGHT },
    { "Gniazdo", "id07", DEV_TYPE_PLUG },
};
static dev_state_t st[N_DEV];
static group_cfg_t blinds, lights;
static group_cmd_t cmds[GROUP_MEMBERS_MAX];
static uint8_t slot0[GROUPS_SLOT_SIZE], slot1[GROUPS_SLOT_SIZE];

static void add_member(group_cfg_t *g, const char *id)
{
    strcpy(g->members[g->member_count++], id);
}

void setUp(void)
{
    for (unsigned i = 0; i < N_DEV; i++)
        st[i] = (dev_state_t){ .link = DEV_LINK_ONLINE, .power_dw = -1, .voltage_dv = -1, .current_ma = -1 };
    memset(&blinds, 0, sizeof blinds);
    blinds.id = 1;
    strcpy(blinds.name, "Rolety");
    blinds.type = DEV_TYPE_BLIND;
    add_member(&blinds, "id01");
    add_member(&blinds, "id02");
    add_member(&blinds, "id03");
    add_member(&blinds, "id04");
    memset(&lights, 0, sizeof lights);
    lights.id = 2;
    strcpy(lights.name, "Światła");
    lights.type = DEV_TYPE_LIGHT;
    add_member(&lights, "id05");
    add_member(&lights, "id06");
    memset(cmds, 0, sizeof cmds);
}

void tearDown(void) {}

static groups_t two_groups(void)
{
    groups_t gs = { 0 };
    gs.g[0] = blinds;
    gs.g[0].position = 0;
    gs.g[1] = lights;
    gs.g[1].position = 3;
    gs.count = 2;
    return gs;
}

/* ---------- AC-1: stan zbiorczy ---------- */

void test_find_device_by_devid(void)
{
    TEST_ASSERT_EQUAL_INT(L2, groups_find_device(DEVS, N_DEV, "id06"));
    TEST_ASSERT_EQUAL_INT(-1, groups_find_device(DEVS, N_DEV, "idXX"));
}

void test_summary_blinds_average_of_online_members(void)
{
    st[R1].open_pct = 0;
    st[R2].open_pct = 50;
    st[R3].open_pct = 100;
    st[R4].link = DEV_LINK_OFFLINE;
    st[R4].open_pct = 30;
    group_summary_t s;
    groups_summary(&blinds, DEVS, st, N_DEV, &s);
    TEST_ASSERT_EQUAL_UINT8(4, s.members);
    TEST_ASSERT_EQUAL_UINT8(0, s.missing);
    TEST_ASSERT_EQUAL_UINT8(3, s.online);
    TEST_ASSERT_EQUAL_UINT8(2, s.open);
    TEST_ASSERT_EQUAL_UINT8(50, s.avg_pct);
    TEST_ASSERT_FALSE(s.any_moving);
    st[R2].moving = true;
    groups_summary(&blinds, DEVS, st, N_DEV, &s);
    TEST_ASSERT_TRUE(s.any_moving);
}

void test_summary_skips_missing_devid(void)
{
    add_member(&lights, "idXX"); /* urządzenie zniknęło z konfiguracji */
    st[L1].on = true;
    group_summary_t s;
    groups_summary(&lights, DEVS, st, N_DEV, &s);
    TEST_ASSERT_EQUAL_UINT8(2, s.members);
    TEST_ASSERT_EQUAL_UINT8(1, s.missing);
    TEST_ASSERT_EQUAL_UINT8(2, s.online);
    TEST_ASSERT_EQUAL_UINT8(1, s.on);
}

/* ---------- AC-2: wygląd kafelka grupy (§6.2) ---------- */

static group_view_t view_of(const group_cfg_t *g)
{
    group_summary_t s;
    group_view_t v;
    groups_summary(g, DEVS, st, N_DEV, &s);
    groups_tile_view(g, &s, &v);
    return v;
}

void test_view_lights_all_some_none(void)
{
    st[L1].on = st[L2].on = true;
    group_view_t v = view_of(&lights);
    TEST_ASSERT_EQUAL_STRING("Wszystkie wł.", v.view.status);
    TEST_ASSERT_EQUAL_INT(UI_ICON_BULB_ON, v.view.icon);
    TEST_ASSERT_EQUAL_UINT8(100, v.view.glow_pct);
    TEST_ASSERT_EQUAL_UINT8(2, v.badge);
    st[L2].on = false;
    v = view_of(&lights);
    TEST_ASSERT_EQUAL_STRING("1/2 wł.", v.view.status);
    TEST_ASSERT_EQUAL_UINT8(50, v.view.glow_pct);
    st[L1].on = false;
    v = view_of(&lights);
    TEST_ASSERT_EQUAL_STRING("Wył.", v.view.status);
    TEST_ASSERT_EQUAL_INT(UI_ICON_BULB_OFF, v.view.icon);
    TEST_ASSERT_FALSE(v.view.dimmed);
}

void test_view_blinds_average_and_motion(void)
{
    st[R1].open_pct = 20;
    st[R2].open_pct = 40;
    st[R3].open_pct = 60;
    st[R4].open_pct = 40;
    group_view_t v = view_of(&blinds);
    TEST_ASSERT_EQUAL_STRING("Średnio 40%", v.view.status);
    TEST_ASSERT_EQUAL_INT(UI_ICON_BLIND_PARTIAL, v.view.icon);
    TEST_ASSERT_EQUAL_UINT8(4, v.badge);
    for (unsigned i = R1; i <= R4; i++)
        st[i].open_pct = 0;
    TEST_ASSERT_EQUAL_STRING("Zamknięte", view_of(&blinds).view.status);
    for (unsigned i = R1; i <= R4; i++)
        st[i].open_pct = 100;
    TEST_ASSERT_EQUAL_STRING("Otwarte", view_of(&blinds).view.status);
    st[R3].moving = true;
    v = view_of(&blinds);
    TEST_ASSERT_EQUAL_STRING("W ruchu", v.view.status);
    TEST_ASSERT_EQUAL_INT(UI_ICON_BLIND_MOVING, v.view.icon);
}

void test_view_partly_offline_shows_online_count(void)
{
    st[R4].link = DEV_LINK_OFFLINE; /* R-GRP-9 */
    group_view_t v = view_of(&blinds);
    TEST_ASSERT_EQUAL_STRING("3/4 online", v.view.status);
    TEST_ASSERT_FALSE(v.view.dimmed);
}

void test_view_all_offline_is_dimmed(void)
{
    st[L1].link = st[L2].link = DEV_LINK_OFFLINE;
    group_view_t v = view_of(&lights);
    TEST_ASSERT_TRUE(v.view.dimmed);
    TEST_ASSERT_EQUAL_STRING("Offline", v.view.status);
    TEST_ASSERT_EQUAL_INT(UI_ICON_BULB_OFF, v.view.icon);
    TEST_ASSERT_TRUE(v.view.offline_mark); /* tile-state-ux AC-10: jak urządzenie offline */
    TEST_ASSERT_TRUE(v.view.warn);
}

/* tile-state-ux AC-10: żaden członek online, część jeszcze nieznana -> „Łączenie…” bez ostrzeżenia */
void test_view_unknown_members_shows_connecting(void)
{
    st[R1].link = st[R2].link = DEV_LINK_UNKNOWN;
    st[R3].link = st[R4].link = DEV_LINK_OFFLINE;
    group_summary_t s;
    group_view_t v;
    groups_summary(&blinds, DEVS, st, N_DEV, &s);
    TEST_ASSERT_EQUAL_UINT8(4, s.members);
    TEST_ASSERT_EQUAL_UINT8(0, s.online);
    TEST_ASSERT_EQUAL_UINT8(2, s.unknown);
    groups_tile_view(&blinds, &s, &v);
    TEST_ASSERT_TRUE(v.view.dimmed);
    TEST_ASSERT_EQUAL_STRING("Łączenie…", v.view.status);
    TEST_ASSERT_FALSE(v.view.warn);
    TEST_ASSERT_FALSE(v.view.offline_mark);
    TEST_ASSERT_EQUAL_UINT8(4, v.badge);
}

/* ---------- AC-3: dotknięcie kafelka grupy = fan-out (§6.4) ---------- */

void test_toggle_closed_blinds_open_online_members_only(void)
{
    st[R4].link = DEV_LINK_OFFLINE; /* R-GRP-2, R-GRP-9 */
    size_t n = groups_toggle(&blinds, DEVS, st, N_DEV, cmds, GROUP_MEMBERS_MAX);
    TEST_ASSERT_EQUAL_UINT(3, n);
    TEST_ASSERT_EQUAL_UINT(R1, cmds[0].idx);
    TEST_ASSERT_EQUAL_UINT(R2, cmds[1].idx);
    TEST_ASSERT_EQUAL_UINT(R3, cmds[2].idx);
    for (size_t i = 0; i < n; i++)
        TEST_ASSERT_EQUAL_STRING("{\"1\":\"open\"}", cmds[i].dps);
}

/* control-polish AC-2: „otwarta” od 50% – jak pojedynczy kafelek (było: > 0) */
void test_toggle_blinds_open_below_50_close_from_50(void)
{
    st[R3].open_pct = 10;
    TEST_ASSERT_EQUAL_UINT(4, groups_toggle(&blinds, DEVS, st, N_DEV, cmds, GROUP_MEMBERS_MAX));
    for (size_t i = 0; i < 4; i++)
        TEST_ASSERT_EQUAL_STRING("{\"1\":\"open\"}", cmds[i].dps);
    st[R3].open_pct = 50; /* R-GRP-3 */
    TEST_ASSERT_EQUAL_UINT(4, groups_toggle(&blinds, DEVS, st, N_DEV, cmds, GROUP_MEMBERS_MAX));
    for (size_t i = 0; i < 4; i++)
        TEST_ASSERT_EQUAL_STRING("{\"1\":\"close\"}", cmds[i].dps);
}

void test_summary_open_counts_from_50(void)
{
    st[R1].open_pct = 49;
    st[R2].open_pct = 50;
    group_summary_t s;
    groups_summary(&blinds, DEVS, st, N_DEV, &s);
    TEST_ASSERT_EQUAL_UINT8(1, s.open);
}

/* control-polish AC-6: suwak jasności grupy – każdy członek w swoim trybie, z własnym kolorem */
void test_command_level_uses_each_member_mode(void)
{
    st[L1].on = st[L2].on = true;
    st[L2].colour = true;
    st[L2].hue = 120;
    st[L2].sat = 1000;
    size_t n = groups_command_level(&lights, DEVS, st, N_DEV, 0x3, 500, cmds, GROUP_MEMBERS_MAX);
    TEST_ASSERT_EQUAL_UINT(2, n);
    TEST_ASSERT_EQUAL_UINT(L1, cmds[0].idx);
    TEST_ASSERT_EQUAL_STRING("{\"22\":500}", cmds[0].dps);
    TEST_ASSERT_EQUAL_UINT(L2, cmds[1].idx);
    TEST_ASSERT_EQUAL_STRING("{\"21\":\"colour\",\"24\":\"007803e801f4\"}", cmds[1].dps);

    TEST_ASSERT_EQUAL_UINT(1, groups_command_level(&lights, DEVS, st, N_DEV, 0x2, 500, cmds, GROUP_MEMBERS_MAX));
    TEST_ASSERT_EQUAL_UINT(L2, cmds[0].idx); /* tylko zaznaczony */
    st[L2].link = DEV_LINK_OFFLINE;
    TEST_ASSERT_EQUAL_UINT(1, groups_command_level(&lights, DEVS, st, N_DEV, 0x3, 500, cmds, GROUP_MEMBERS_MAX));
    TEST_ASSERT_EQUAL_UINT(L1, cmds[0].idx); /* offline pominięty */
}

void test_toggle_any_moving_blind_stops_all(void)
{
    st[R1].open_pct = 100;
    st[R2].moving = true; /* R-GRP-4 */
    TEST_ASSERT_EQUAL_UINT(4, groups_toggle(&blinds, DEVS, st, N_DEV, cmds, GROUP_MEMBERS_MAX));
    for (size_t i = 0; i < 4; i++)
        TEST_ASSERT_EQUAL_STRING("{\"1\":\"stop\"}", cmds[i].dps);
}

void test_toggle_lights_and_plugs(void)
{
    st[L2].on = true;
    TEST_ASSERT_EQUAL_UINT(2, groups_toggle(&lights, DEVS, st, N_DEV, cmds, GROUP_MEMBERS_MAX));
    TEST_ASSERT_EQUAL_STRING("{\"20\":false}", cmds[0].dps);
    TEST_ASSERT_EQUAL_STRING("{\"20\":false}", cmds[1].dps);
    st[L2].on = false;
    groups_toggle(&lights, DEVS, st, N_DEV, cmds, GROUP_MEMBERS_MAX);
    TEST_ASSERT_EQUAL_STRING("{\"20\":true}", cmds[0].dps);

    group_cfg_t plugs = { .id = 3, .name = "Gniazdka", .type = DEV_TYPE_PLUG };
    add_member(&plugs, "id07");
    add_member(&plugs, "idXX"); /* brak urządzenia – pomijany */
    TEST_ASSERT_EQUAL_UINT(1, groups_toggle(&plugs, DEVS, st, N_DEV, cmds, GROUP_MEMBERS_MAX));
    TEST_ASSERT_EQUAL_UINT(P1, cmds[0].idx);
    TEST_ASSERT_EQUAL_STRING("{\"1\":true}", cmds[0].dps);
}

void test_toggle_respects_cap(void)
{
    TEST_ASSERT_EQUAL_UINT(2, groups_toggle(&blinds, DEVS, st, N_DEV, cmds, 2));
    TEST_ASSERT_EQUAL_UINT(R2, cmds[1].idx);
}

void test_command_only_selected_online_members(void)
{
    /* R-UI-7 / R-GRP-5: komenda tylko do zaznaczonych */
    TEST_ASSERT_EQUAL_UINT(1, groups_command(&lights, DEVS, st, N_DEV, 0x0001, "{\"22\":500}", cmds, GROUP_MEMBERS_MAX));
    TEST_ASSERT_EQUAL_UINT(L1, cmds[0].idx);
    TEST_ASSERT_EQUAL_STRING("{\"22\":500}", cmds[0].dps);
    st[R4].link = DEV_LINK_OFFLINE;
    TEST_ASSERT_EQUAL_UINT(1, groups_command(&blinds, DEVS, st, N_DEV, 0x000C, "{\"1\":\"open\"}", cmds, GROUP_MEMBERS_MAX));
    TEST_ASSERT_EQUAL_UINT(R3, cmds[0].idx);
}

/* ---------- AC-4: grupy startowe (O2) i operacje na liście ---------- */

void test_defaults_from_device_table(void)
{
    groups_t gs;
    groups_defaults(g_devices, g_device_count, &gs);
    TEST_ASSERT_EQUAL_UINT8(2, gs.count);
    TEST_ASSERT_EQUAL_UINT8(1, gs.g[0].id);
    TEST_ASSERT_EQUAL_STRING("Salon – rolety", gs.g[0].name);
    TEST_ASSERT_EQUAL_INT(DEV_TYPE_BLIND, gs.g[0].type);
    TEST_ASSERT_EQUAL_UINT8(0, gs.g[0].position);
    static const char *salon[] = { "Salon roleta", "Salon s1", "Salon s2", "Salon s3" };
    TEST_ASSERT_EQUAL_UINT8(4, gs.g[0].member_count);
    for (unsigned i = 0; i < 4; i++) {
        int d = -1;
        for (size_t k = 0; k < g_device_count; k++)
            if (strcmp(g_devices[k].name, salon[i]) == 0)
                d = (int)k;
        TEST_ASSERT_TRUE(d >= 0);
        TEST_ASSERT_EQUAL_STRING(g_devices[d].id, gs.g[0].members[i]);
    }
    TEST_ASSERT_EQUAL_UINT8(2, gs.g[1].id);
    TEST_ASSERT_EQUAL_STRING("Wszystkie rolety", gs.g[1].name);
    TEST_ASSERT_EQUAL_UINT8(1, gs.g[1].position);
    unsigned nb = 0;
    for (size_t k = 0; k < g_device_count; k++) {
        if (g_devices[k].type != DEV_TYPE_BLIND)
            continue;
        TEST_ASSERT_EQUAL_STRING(g_devices[k].id, gs.g[1].members[nb]); /* kolejność tabeli urządzeń */
        nb++;
    }
    TEST_ASSERT_EQUAL_UINT(9, nb); /* 9 rolet w konfiguracji („Salon duże okno” to gniazdko) */
    TEST_ASSERT_EQUAL_UINT8(nb, gs.g[1].member_count);
}

void test_can_add_free_id_and_delete(void)
{
    groups_t gs = two_groups();
    TEST_ASSERT_TRUE(groups_can_add(&gs));
    TEST_ASSERT_EQUAL_UINT8(3, groups_free_id(&gs));
    gs.g[0].id = 5;
    TEST_ASSERT_EQUAL_UINT8(1, groups_free_id(&gs));
    TEST_ASSERT_TRUE(groups_delete(&gs, 5));
    TEST_ASSERT_EQUAL_UINT8(1, gs.count);
    TEST_ASSERT_EQUAL_STRING("Światła", gs.g[0].name);
    TEST_ASSERT_FALSE(groups_delete(&gs, 9));
    gs.count = GROUP_MAX;
    TEST_ASSERT_FALSE(groups_can_add(&gs));
}

/* ---------- AC-5: lista kafelków ---------- */

void test_tile_list_inserts_groups_at_positions_and_add_tile_last(void)
{
    groups_t gs = two_groups(); /* „Rolety” na 0, „Światła” na 3 */
    tile_ref_t t[32];
    size_t n = groups_tile_list(&gs, N_DEV, t, 32);
    TEST_ASSERT_EQUAL_UINT(N_DEV + 2 + 1, n);
    TEST_ASSERT_EQUAL_INT(TILE_GROUP, t[0].kind);
    TEST_ASSERT_EQUAL_UINT8(0, t[0].index);
    TEST_ASSERT_EQUAL_INT(TILE_DEVICE, t[1].kind);
    TEST_ASSERT_EQUAL_UINT8(R1, t[1].index);
    TEST_ASSERT_EQUAL_INT(TILE_DEVICE, t[2].kind);
    TEST_ASSERT_EQUAL_UINT8(R2, t[2].index);
    TEST_ASSERT_EQUAL_INT(TILE_GROUP, t[3].kind);
    TEST_ASSERT_EQUAL_UINT8(1, t[3].index);
    TEST_ASSERT_EQUAL_INT(TILE_DEVICE, t[4].kind);
    TEST_ASSERT_EQUAL_UINT8(R3, t[4].index);
    TEST_ASSERT_EQUAL_INT(TILE_ADD, t[n - 1].kind);
    TEST_ASSERT_EQUAL_INT(TILE_DEVICE, t[n - 2].kind);
    TEST_ASSERT_EQUAL_UINT8(P1, t[n - 2].index);
}

void test_tile_list_position_beyond_end_and_ties(void)
{
    groups_t gs = two_groups();
    gs.g[0].position = 200;
    gs.g[1].position = 200;
    tile_ref_t t[32];
    size_t n = groups_tile_list(&gs, N_DEV, t, 32);
    TEST_ASSERT_EQUAL_INT(TILE_GROUP, t[N_DEV].kind);
    TEST_ASSERT_EQUAL_UINT8(1, t[N_DEV].index); /* remis: nowsza (dalej w gs) pierwsza */
    TEST_ASSERT_EQUAL_UINT8(0, t[N_DEV + 1].index);
    TEST_ASSERT_EQUAL_INT(TILE_ADD, t[n - 1].kind);

    gs.g[0].position = 0; /* nowa grupa na pozycji zajętej przez starszą trafia przed nią */
    gs.g[1].position = 0;
    groups_tile_list(&gs, N_DEV, t, 32);
    TEST_ASSERT_EQUAL_UINT8(1, t[0].index);
    TEST_ASSERT_EQUAL_UINT8(0, t[1].index);
    TEST_ASSERT_EQUAL_INT(TILE_DEVICE, t[2].kind);

    groups_t none = { 0 };
    TEST_ASSERT_EQUAL_UINT(N_DEV + 1, groups_tile_list(&none, N_DEV, t, 32));
    TEST_ASSERT_EQUAL_UINT(4, groups_tile_list(&gs, N_DEV, t, 4)); /* cap */
}

/* ---------- AC-6: zapis we flash (R-GRP-6, R-GRP-7) ---------- */

static void assert_groups_equal(const groups_t *a, const groups_t *b)
{
    TEST_ASSERT_EQUAL_UINT8(a->count, b->count);
    for (unsigned i = 0; i < a->count; i++) {
        TEST_ASSERT_EQUAL_UINT8(a->g[i].id, b->g[i].id);
        TEST_ASSERT_EQUAL_STRING(a->g[i].name, b->g[i].name);
        TEST_ASSERT_EQUAL_INT(a->g[i].type, b->g[i].type);
        TEST_ASSERT_EQUAL_UINT8(a->g[i].position, b->g[i].position);
        TEST_ASSERT_EQUAL_UINT8(a->g[i].member_count, b->g[i].member_count);
        for (unsigned m = 0; m < a->g[i].member_count; m++)
            TEST_ASSERT_EQUAL_STRING(a->g[i].members[m], b->g[i].members[m]);
    }
}

static void put_u32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

/* Przelicza CRC slotu po ręcznej zmianie payloadu */
static void fix_crc(uint8_t *slot)
{
    uint32_t plen = (uint32_t)slot[12] | (uint32_t)slot[13] << 8 | (uint32_t)slot[14] << 16 | (uint32_t)slot[15] << 24;
    static uint8_t tmp[GROUPS_SLOT_SIZE];
    memcpy(tmp, slot, 16);
    memcpy(tmp + 16, slot + GROUPS_HDR_LEN, plen);
    put_u32(slot + 16, tuya_crc32(tmp, 16 + plen));
}

void test_encode_layout_and_roundtrip(void)
{
    groups_t gs = two_groups(), out;
    memset(&out, 0, sizeof out);
    int len = groups_encode(&gs, 7, slot0, sizeof slot0);
    TEST_ASSERT_EQUAL_INT(GROUPS_HDR_LEN + 1 + 2 * GROUPS_RECORD_LEN, len);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(((uint8_t[]){ 'G', 'R', 'P', '1', 1, 0, 0, 0, 7, 0, 0, 0 }), slot0, 12);
    TEST_ASSERT_EQUAL_UINT8(2, slot0[GROUPS_HDR_LEN]); /* count */
    uint32_t seq = 0;
    TEST_ASSERT_EQUAL_INT(0, groups_decode(slot0, (size_t)len, &out, &seq));
    TEST_ASSERT_EQUAL_UINT32(7, seq);
    assert_groups_equal(&gs, &out);
}

void test_encode_full_16x16_fits_slot(void)
{
    groups_t gs = { 0 };
    for (unsigned i = 0; i < GROUP_MAX; i++) {
        gs.g[i] = blinds;
        gs.g[i].id = (uint8_t)(i + 1);
        gs.g[i].member_count = GROUP_MEMBERS_MAX;
        for (unsigned m = 0; m < GROUP_MEMBERS_MAX; m++)
            strcpy(gs.g[i].members[m], "bf00000000000000000000"); /* 22 znaki */
    }
    gs.count = GROUP_MAX;
    TEST_ASSERT_TRUE(groups_encode(&gs, 1, slot0, sizeof slot0) <= (int)GROUPS_SLOT_SIZE);
    TEST_ASSERT_TRUE(groups_encode(&gs, 1, slot0, sizeof slot0) > 0);
    TEST_ASSERT_TRUE(groups_encode(&gs, 1, slot0, 100) < 0);
}

void test_decode_rejects_corruption_and_keeps_output(void)
{
    groups_t gs = two_groups(), out = { .count = 9 };
    uint32_t seq = 123;
    int len = groups_encode(&gs, 7, slot0, sizeof slot0);
    TEST_ASSERT_TRUE_MESSAGE(len > 0, "encode");

    memset(slot1, 0xFF, sizeof slot1); /* wymazany flash */
    TEST_ASSERT_TRUE(groups_decode(slot1, sizeof slot1, &out, &seq) < 0);

    memcpy(slot1, slot0, (size_t)len);
    slot1[GROUPS_HDR_LEN + 40] ^= 0x01; /* bit w nazwie – CRC */
    TEST_ASSERT_TRUE(groups_decode(slot1, sizeof slot1, &out, &seq) < 0);

    TEST_ASSERT_TRUE(groups_decode(slot0, (size_t)len - 10, &out, &seq) < 0); /* ucięty */

    memcpy(slot1, slot0, (size_t)len);
    slot1[GROUPS_HDR_LEN] = 17; /* count > 16 przy poprawnym CRC */
    fix_crc(slot1);
    TEST_ASSERT_TRUE(groups_decode(slot1, sizeof slot1, &out, &seq) < 0);

    memcpy(slot1, slot0, (size_t)len);
    slot1[GROUPS_HDR_LEN + 1 + 2] = 17; /* member_count > 16 */
    fix_crc(slot1);
    TEST_ASSERT_TRUE(groups_decode(slot1, sizeof slot1, &out, &seq) < 0);

    memcpy(slot1, slot0, (size_t)len);
    memset(slot1 + GROUPS_HDR_LEN + 1 + 4, 'A', GROUP_NAME_LEN); /* nazwa bez '\0' */
    fix_crc(slot1);
    TEST_ASSERT_TRUE(groups_decode(slot1, sizeof slot1, &out, &seq) < 0);

    memcpy(slot1, slot0, (size_t)len);
    slot1[GROUPS_HDR_LEN + 1 + GROUPS_RECORD_LEN] = slot1[GROUPS_HDR_LEN + 1]; /* powtórzone id */
    fix_crc(slot1);
    TEST_ASSERT_TRUE(groups_decode(slot1, sizeof slot1, &out, &seq) < 0);

    TEST_ASSERT_EQUAL_UINT8(9, out.count); /* bez zmian */
    TEST_ASSERT_EQUAL_UINT32(123, seq);
}

void test_decode_rejects_17_groups_with_consistent_length(void)
{
    /* count = 17 przy długości i CRC zgodnych z 17 rekordami – odrzuca sam limit liczby grup */
    static groups_t gs, out;
    memset(&gs, 0, sizeof gs);
    for (unsigned i = 0; i < GROUP_MAX; i++) {
        gs.g[i] = blinds;
        gs.g[i].id = (uint8_t)(i + 1);
    }
    gs.count = GROUP_MAX;
    int len = groups_encode(&gs, 1, slot0, sizeof slot0);
    TEST_ASSERT_TRUE_MESSAGE(len > 0, "encode");
    uint8_t *rec17 = slot0 + len;
    memcpy(rec17, slot0 + GROUPS_HDR_LEN + 1, GROUPS_RECORD_LEN);
    rec17[0] = 17; /* unikalne id */
    slot0[GROUPS_HDR_LEN] = 17;
    put_u32(slot0 + 12, 1u + 17u * GROUPS_RECORD_LEN);
    fix_crc(slot0);
    uint32_t seq;
    TEST_ASSERT_TRUE(groups_decode(slot0, sizeof slot0, &out, &seq) < 0);
}

void test_view_badge_zero_when_all_members_missing(void)
{
    group_cfg_t gone = { .id = 9, .name = "Stara", .type = DEV_TYPE_BLIND };
    add_member(&gone, "idXX");
    add_member(&gone, "idYY");
    group_view_t v = view_of(&gone);
    TEST_ASSERT_EQUAL_UINT8(0, v.badge);
    TEST_ASSERT_TRUE(v.view.dimmed);
}

void test_load_picks_newer_valid_slot(void)
{
    groups_t a = two_groups(), b = two_groups(), out;
    strcpy(b.g[0].name, "Parter");
    groups_encode(&a, 1, slot0, sizeof slot0);
    groups_encode(&b, 2, slot1, sizeof slot1);
    uint32_t seq;
    TEST_ASSERT_EQUAL_INT(1, groups_load(slot0, slot1, GROUPS_SLOT_SIZE, &out, &seq));
    TEST_ASSERT_EQUAL_STRING("Parter", out.g[0].name);
    TEST_ASSERT_EQUAL_UINT32(2, seq);
}

void test_load_falls_back_when_newest_slot_is_torn(void)
{
    /* R-GRP-7: zapis przerwany utratą zasilania – nowszy slot uszkodzony, starszy zostaje */
    groups_t a = two_groups(), b = two_groups(), out;
    strcpy(b.g[0].name, "Parter");
    groups_encode(&a, 1, slot0, sizeof slot0);
    int len = groups_encode(&b, 2, slot1, sizeof slot1);
    TEST_ASSERT_TRUE_MESSAGE(len > 0, "encode");
    memset(slot1 + len / 2, 0xFF, GROUPS_SLOT_SIZE - (size_t)(len / 2));
    uint32_t seq;
    TEST_ASSERT_EQUAL_INT(0, groups_load(slot0, slot1, GROUPS_SLOT_SIZE, &out, &seq));
    TEST_ASSERT_EQUAL_STRING("Rolety", out.g[0].name);
    TEST_ASSERT_EQUAL_UINT32(1, seq);
}

void test_load_both_bad_returns_minus_one(void)
{
    groups_t out = { .count = 9 };
    uint32_t seq = 5;
    memset(slot0, 0xFF, sizeof slot0);
    memset(slot1, 0x00, sizeof slot1);
    TEST_ASSERT_EQUAL_INT(-1, groups_load(slot0, slot1, GROUPS_SLOT_SIZE, &out, &seq));
    TEST_ASSERT_EQUAL_UINT8(9, out.count);
}

void test_load_seq_wraparound(void)
{
    groups_t a = two_groups(), b = two_groups(), out;
    strcpy(b.g[0].name, "Nowsza");
    groups_encode(&a, 0xFFFFFFFFu, slot0, sizeof slot0);
    groups_encode(&b, 0, slot1, sizeof slot1);
    uint32_t seq;
    TEST_ASSERT_EQUAL_INT(1, groups_load(slot0, slot1, GROUPS_SLOT_SIZE, &out, &seq));
    TEST_ASSERT_EQUAL_STRING("Nowsza", out.g[0].name);
}

void test_next_slot_alternates(void)
{
    TEST_ASSERT_EQUAL_INT(0, groups_next_slot(-1));
    TEST_ASSERT_EQUAL_INT(1, groups_next_slot(0));
    TEST_ASSERT_EQUAL_INT(0, groups_next_slot(1));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_find_device_by_devid);
    RUN_TEST(test_summary_blinds_average_of_online_members);
    RUN_TEST(test_summary_skips_missing_devid);
    RUN_TEST(test_view_lights_all_some_none);
    RUN_TEST(test_view_blinds_average_and_motion);
    RUN_TEST(test_view_partly_offline_shows_online_count);
    RUN_TEST(test_view_all_offline_is_dimmed);
    RUN_TEST(test_view_unknown_members_shows_connecting);
    RUN_TEST(test_toggle_closed_blinds_open_online_members_only);
    RUN_TEST(test_toggle_blinds_open_below_50_close_from_50);
    RUN_TEST(test_summary_open_counts_from_50);
    RUN_TEST(test_command_level_uses_each_member_mode);
    RUN_TEST(test_toggle_any_moving_blind_stops_all);
    RUN_TEST(test_toggle_lights_and_plugs);
    RUN_TEST(test_toggle_respects_cap);
    RUN_TEST(test_command_only_selected_online_members);
    RUN_TEST(test_defaults_from_device_table);
    RUN_TEST(test_can_add_free_id_and_delete);
    RUN_TEST(test_tile_list_inserts_groups_at_positions_and_add_tile_last);
    RUN_TEST(test_tile_list_position_beyond_end_and_ties);
    RUN_TEST(test_encode_layout_and_roundtrip);
    RUN_TEST(test_encode_full_16x16_fits_slot);
    RUN_TEST(test_decode_rejects_corruption_and_keeps_output);
    RUN_TEST(test_decode_rejects_17_groups_with_consistent_length);
    RUN_TEST(test_view_badge_zero_when_all_members_missing);
    RUN_TEST(test_load_picks_newer_valid_slot);
    RUN_TEST(test_load_falls_back_when_newest_slot_is_torn);
    RUN_TEST(test_load_both_bad_returns_minus_one);
    RUN_TEST(test_load_seq_wraparound);
    RUN_TEST(test_next_slot_alternates);
    return UNITY_END();
}
