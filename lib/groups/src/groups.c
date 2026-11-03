/* Grupy wirtualne – model, stan zbiorczy, fan-out, lista kafelków, format zapisu (spec: virtual-groups). */
#include "groups.h"

#include <stdio.h>
#include <string.h>

#include "tuya_dps.h"

int groups_find_device(const dev_info_t *devs, size_t n, const char *dev_id)
{
    for (size_t i = 0; i < n; i++)
        if (strcmp(devs[i].id, dev_id) == 0)
            return (int)i;
    return -1;
}

/* ---------- Stan zbiorczy ---------- */

void groups_summary(const group_cfg_t *g, const dev_info_t *devs, const dev_state_t *states, size_t n,
                    group_summary_t *out)
{
    unsigned sum_pct = 0;
    memset(out, 0, sizeof *out);
    for (unsigned m = 0; m < g->member_count && m < GROUP_MEMBERS_MAX; m++) {
        int d = groups_find_device(devs, n, g->members[m]);
        if (d < 0) {
            out->missing++;
            continue;
        }
        out->members++;
        const dev_state_t *st = &states[d];
        if (st->link == DEV_LINK_UNKNOWN)
            out->unknown++;
        if (st->link != DEV_LINK_ONLINE)
            continue;
        out->online++;
        if (st->on)
            out->on++;
        if (st->open_pct >= 50) /* jak pojedynczy kafelek (tuya_dps_toggle) */
            out->open++;
        if (st->moving)
            out->any_moving = true;
        sum_pct += st->open_pct;
    }
    if (out->online > 0)
        out->avg_pct = (uint8_t)((sum_pct + out->online / 2u) / out->online);
}

void groups_tile_view(const group_cfg_t *g, const group_summary_t *s, group_view_t *out)
{
    memset(out, 0, sizeof *out);
    out->badge = s->members;
    ui_tile_view_t *v = &out->view;

    /* ikona bazowa / wg stanu zbiorczego – przez widok pojedynczego urządzenia */
    dev_state_t agg = { .link = DEV_LINK_ONLINE, .power_dw = -1, .voltage_dv = -1, .current_ma = -1 };
    agg.on = s->on > 0;
    agg.brightness = 1000;
    agg.open_pct = s->avg_pct;
    agg.moving = s->any_moving;
    if (s->online == 0)
        agg.link = s->unknown > 0 ? DEV_LINK_UNKNOWN : DEV_LINK_OFFLINE;
    ui_tile_view(g->type, &agg, v);
    if (s->online == 0)
        return; /* dimmed, "Offline" / "Łączenie…" */

    if (g->type == DEV_TYPE_LIGHT && s->on > 0)
        v->glow_pct = s->on == s->online ? 100 : 50;

    if (s->online < s->members) {
        snprintf(v->status, sizeof v->status, "%u/%u online", (unsigned)s->online, (unsigned)s->members);
        return;
    }
    if (g->type == DEV_TYPE_BLIND) {
        if (s->any_moving)
            snprintf(v->status, sizeof v->status, "W ruchu");
        else if (s->avg_pct == 0)
            snprintf(v->status, sizeof v->status, "Zamknięte");
        else if (s->avg_pct >= 100)
            snprintf(v->status, sizeof v->status, "Otwarte");
        else
            snprintf(v->status, sizeof v->status, "Średnio %u%%", (unsigned)s->avg_pct);
    } else {
        if (s->on == 0)
            snprintf(v->status, sizeof v->status, "Wył.");
        else if (s->on == s->online)
            snprintf(v->status, sizeof v->status, "Wszystkie wł.");
        else
            snprintf(v->status, sizeof v->status, "%u/%u wł.", (unsigned)s->on, (unsigned)s->online);
    }
}

/* ---------- Komendy ---------- */

size_t groups_command(const group_cfg_t *g, const dev_info_t *devs, const dev_state_t *states, size_t n,
                      uint16_t mask, const char *dps, group_cmd_t *out, size_t cap)
{
    size_t k = 0;
    for (unsigned m = 0; m < g->member_count && m < GROUP_MEMBERS_MAX && k < cap; m++) {
        if (!(mask & (1u << m)))
            continue;
        int d = groups_find_device(devs, n, g->members[m]);
        if (d < 0 || states[d].link != DEV_LINK_ONLINE)
            continue;
        out[k].idx = (size_t)d;
        snprintf(out[k].dps, sizeof out[k].dps, "%s", dps);
        k++;
    }
    return k;
}

size_t groups_toggle(const group_cfg_t *g, const dev_info_t *devs, const dev_state_t *states, size_t n,
                     group_cmd_t *out, size_t cap)
{
    group_summary_t s;
    groups_summary(g, devs, states, n, &s);
    char dps[48];
    if (g->type == DEV_TYPE_BLIND)
        tuya_dps_blind_control(dps, sizeof dps, s.any_moving ? "stop" : s.open > 0 ? "close" : "open");
    else
        tuya_dps_switch(dps, sizeof dps, g->type, s.on == 0);
    return groups_command(g, devs, states, n, 0xFFFFu, dps, out, cap);
}

/* ---------- Konfiguracja ---------- */

static void add_member(group_cfg_t *g, const char *dev_id)
{
    if (g->member_count < GROUP_MEMBERS_MAX)
        snprintf(g->members[g->member_count++], GROUP_DEVID_LEN, "%s", dev_id);
}

void groups_defaults(const dev_info_t *devs, size_t n, groups_t *out)
{
    static const char *const salon[] = { "Salon roleta", "Salon s1", "Salon s2", "Salon s3" };
    memset(out, 0, sizeof *out);

    group_cfg_t *g = &out->g[out->count];
    *g = (group_cfg_t){ .id = 1, .name = "Salon – rolety", .type = DEV_TYPE_BLIND, .position = 0 };
    for (unsigned i = 0; i < sizeof salon / sizeof salon[0]; i++)
        for (size_t d = 0; d < n; d++)
            if (devs[d].type == DEV_TYPE_BLIND && strcmp(devs[d].name, salon[i]) == 0)
                add_member(g, devs[d].id);
    if (g->member_count >= GROUP_MEMBERS_MIN)
        out->count++;

    g = &out->g[out->count];
    *g = (group_cfg_t){ .id = 2, .name = "Wszystkie rolety", .type = DEV_TYPE_BLIND, .position = 1 };
    for (size_t d = 0; d < n; d++)
        if (devs[d].type == DEV_TYPE_BLIND)
            add_member(g, devs[d].id);
    if (g->member_count >= GROUP_MEMBERS_MIN)
        out->count++;
    else
        memset(g, 0, sizeof *g);
}

bool groups_can_add(const groups_t *gs)
{
    return gs->count < GROUP_MAX;
}

uint8_t groups_free_id(const groups_t *gs)
{
    for (unsigned id = 1; id <= 255; id++) {
        bool used = false;
        for (unsigned i = 0; i < gs->count; i++)
            if (gs->g[i].id == id)
                used = true;
        if (!used)
            return (uint8_t)id;
    }
    return 0;
}

bool groups_delete(groups_t *gs, uint8_t id)
{
    for (unsigned i = 0; i < gs->count; i++) {
        if (gs->g[i].id != id)
            continue;
        memmove(&gs->g[i], &gs->g[i + 1], (gs->count - i - 1u) * sizeof gs->g[0]);
        gs->count--;
        memset(&gs->g[gs->count], 0, sizeof gs->g[0]);
        return true;
    }
    return false;
}

/* ---------- Lista kafelków ---------- */

size_t groups_tile_list(const groups_t *gs, size_t n_devs, tile_ref_t *out, size_t cap)
{
    /* kolejność grup: rosnąco wg position, przy równych – dalej w gs (nowsza) pierwsza */
    uint8_t order[GROUP_MAX];
    unsigned ng = gs->count < GROUP_MAX ? gs->count : GROUP_MAX;
    for (unsigned i = 0; i < ng; i++)
        order[i] = (uint8_t)i;
    for (unsigned i = 1; i < ng; i++) {
        uint8_t v = order[i];
        unsigned j = i;
        while (j > 0) {
            const group_cfg_t *a = &gs->g[order[j - 1]], *b = &gs->g[v];
            bool after = a->position > b->position || (a->position == b->position && order[j - 1] < v);
            if (!after)
                break;
            order[j] = order[j - 1];
            j--;
        }
        order[j] = v;
    }

    /* scalanie: grupa k na indeks max(position, poprzedni + 1), urządzenia wypełniają resztę */
    size_t total = n_devs + ng;
    size_t k = 0, d = 0, n = 0;
    long prev = -1;
    for (size_t pos = 0; pos < total && n < cap; pos++) {
        bool group_here = false;
        if (k < ng) {
            long want = gs->g[order[k]].position;
            if (want <= prev)
                want = prev + 1;
            group_here = (long)pos >= want || d >= n_devs;
        }
        if (group_here) {
            out[n++] = (tile_ref_t){ TILE_GROUP, order[k] };
            prev = (long)pos;
            k++;
        } else {
            out[n++] = (tile_ref_t){ TILE_DEVICE, (uint8_t)d };
            d++;
        }
    }
    if (n < cap)
        out[n++] = (tile_ref_t){ TILE_ADD, 0 };
    return n;
}

/* ---------- Format zapisu ---------- */

static void put_u32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static uint32_t get_u32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

/* CRC-32 IEEE przyrostowo (ten sam wynik co tuya_crc32 dla złożonych danych) – bez bufora pośredniego */
static uint32_t crc32_update(uint32_t crc, const uint8_t *p, size_t n)
{
    crc = ~crc;
    while (n--) {
        crc ^= *p++;
        for (unsigned k = 0; k < 8; k++)
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}

/* CRC z 16 bajtów nagłówka (bez pola crc) i payloadu */
static uint32_t slot_crc(const uint8_t *buf, size_t payload_len)
{
    return crc32_update(crc32_update(0, buf, 16), buf + GROUPS_HDR_LEN, payload_len);
}

int groups_encode(const groups_t *gs, uint32_t seq, uint8_t *buf, size_t cap)
{
    unsigned count = gs->count;
    size_t plen = 1u + count * GROUPS_RECORD_LEN;
    if (count > GROUP_MAX || cap < GROUPS_HDR_LEN + plen)
        return -1;
    memset(buf, 0, GROUPS_HDR_LEN + plen);
    put_u32(buf, GROUPS_MAGIC);
    put_u32(buf + 4, GROUPS_VERSION);
    put_u32(buf + 8, seq);
    put_u32(buf + 12, (uint32_t)plen);

    uint8_t *p = buf + GROUPS_HDR_LEN;
    *p++ = (uint8_t)count;
    for (unsigned i = 0; i < count; i++, p += GROUPS_RECORD_LEN) {
        const group_cfg_t *g = &gs->g[i];
        p[0] = g->id;
        p[1] = (uint8_t)g->type;
        p[2] = g->member_count;
        p[3] = g->position;
        memcpy(p + 4, g->name, GROUP_NAME_LEN);
        memcpy(p + 4 + GROUP_NAME_LEN, g->members, sizeof g->members);
    }
    put_u32(buf + 16, slot_crc(buf, plen));
    return (int)(GROUPS_HDR_LEN + plen);
}

static bool terminated(const char *s, size_t cap)
{
    return memchr(s, '\0', cap) != NULL;
}

/* Walidacja slotu bez kopiowania (oszczędność RAM na STM32): liczba grup albo < 0 */
static int slot_check(const uint8_t *buf, size_t len)
{
    if (len < GROUPS_HDR_LEN + 1 || get_u32(buf) != GROUPS_MAGIC || get_u32(buf + 4) != GROUPS_VERSION)
        return -1;
    uint32_t plen = get_u32(buf + 12);
    if (plen < 1 || plen > 1u + GROUP_MAX * GROUPS_RECORD_LEN || plen > len - GROUPS_HDR_LEN)
        return -2;
    if (slot_crc(buf, plen) != get_u32(buf + 16))
        return -3;

    const uint8_t *rec = buf + GROUPS_HDR_LEN + 1;
    unsigned count = buf[GROUPS_HDR_LEN];
    if (count > GROUP_MAX || plen != 1u + count * GROUPS_RECORD_LEN)
        return -4;
    for (unsigned i = 0; i < count; i++) {
        const uint8_t *p = rec + i * GROUPS_RECORD_LEN;
        unsigned mc = p[2];
        if (p[0] == 0 || p[1] > DEV_TYPE_PLUG || mc > GROUP_MEMBERS_MAX)
            return -5;
        if (!terminated((const char *)p + 4, GROUP_NAME_LEN))
            return -6;
        for (unsigned m = 0; m < mc; m++)
            if (!terminated((const char *)p + 4 + GROUP_NAME_LEN + m * GROUP_DEVID_LEN, GROUP_DEVID_LEN))
                return -6;
        for (unsigned j = 0; j < i; j++)
            if (rec[j * GROUPS_RECORD_LEN] == p[0])
                return -7;
    }
    return (int)count;
}

int groups_decode(const uint8_t *buf, size_t len, groups_t *out, uint32_t *seq)
{
    int count = slot_check(buf, len);
    if (count < 0)
        return count;
    memset(out, 0, sizeof *out);
    out->count = (uint8_t)count;
    const uint8_t *p = buf + GROUPS_HDR_LEN + 1;
    for (int i = 0; i < count; i++, p += GROUPS_RECORD_LEN) {
        group_cfg_t *g = &out->g[i];
        g->id = p[0];
        g->type = (dev_type_t)p[1];
        g->member_count = p[2];
        g->position = p[3];
        memcpy(g->name, p + 4, GROUP_NAME_LEN);
        memcpy(g->members, p + 4 + GROUP_NAME_LEN, sizeof g->members);
    }
    *seq = get_u32(buf + 8);
    return 0;
}

int groups_load(const uint8_t *slot0, const uint8_t *slot1, size_t len, groups_t *out, uint32_t *seq)
{
    bool ok_a = slot_check(slot0, len) >= 0;
    bool ok_b = slot_check(slot1, len) >= 0;
    uint32_t sa = ok_a ? get_u32(slot0 + 8) : 0, sb = ok_b ? get_u32(slot1 + 8) : 0;
    if (ok_a && (!ok_b || (int32_t)(sa - sb) > 0))
        return groups_decode(slot0, len, out, seq) == 0 ? 0 : -1;
    if (ok_b)
        return groups_decode(slot1, len, out, seq) == 0 ? 1 : -1;
    return -1;
}

int groups_next_slot(int current)
{
    return current == 0 ? 1 : 0;
}

size_t groups_command_level(const group_cfg_t *g, const dev_info_t *devs, const dev_state_t *states, size_t n,
                            uint16_t mask, unsigned value, group_cmd_t *out, size_t cap)
{
    size_t k = 0;
    for (unsigned m = 0; m < g->member_count && m < GROUP_MEMBERS_MAX && k < cap; m++) {
        if (!(mask & (1u << m)))
            continue;
        int d = groups_find_device(devs, n, g->members[m]);
        if (d < 0 || states[d].link != DEV_LINK_ONLINE)
            continue;
        out[k].idx = (size_t)d;
        tuya_dps_light_level(out[k].dps, sizeof out[k].dps, &states[d], value); /* tryb i kolor członka */
        k++;
    }
    return k;
}
