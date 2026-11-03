/*
 * Pamięć QSPI N25Q128A przez BSP płytki (tryb pośredni, odczyt i zapis CPU – bez DMA i cache) oraz
 * zapis grup w 2 slotach naprzemiennie (spec: virtual-groups, docs/SPEC.md §4.4, R-GRP-6/7).
 */
#include "app_storage.h"

#include <stdbool.h>
#include <string.h>

#include "stm32746g_discovery_qspi.h"

#define FLASH_SIZE     N25Q128A_FLASH_SIZE
#define SUBSECTOR_SIZE N25Q128A_SUBSECTOR_SIZE

/* bufory slotów – za duże na stos zadania */
static uint8_t s_slot[2][GROUPS_SLOT_SIZE];
static groups_t s_tmp;

static bool in_range(uint32_t addr, size_t len)
{
    return len <= FLASH_SIZE && addr <= FLASH_SIZE - len;
}

int app_storage_init(void)
{
    return BSP_QSPI_Init() == QSPI_OK ? 0 : -1;
}

int app_storage_read(uint32_t addr, void *buf, size_t len)
{
    if (!in_range(addr, len))
        return -1;
    if (len == 0)
        return 0;
    return BSP_QSPI_Read(buf, addr, (uint32_t)len) == QSPI_OK ? 0 : -2;
}

int app_storage_erase(uint32_t addr, size_t len)
{
    if (addr % SUBSECTOR_SIZE != 0 || !in_range(addr, len))
        return -1;
    for (uint32_t a = addr; a < addr + len; a += SUBSECTOR_SIZE)
        if (BSP_QSPI_Erase_Block(a) != QSPI_OK)
            return -2;
    return 0;
}

int app_storage_write(uint32_t addr, const void *buf, size_t len)
{
    if (!in_range(addr, len))
        return -1;
    if (len == 0)
        return 0;
    /* BSP_QSPI_Write przyjmuje wskaźnik bez const, ale danych nie zmienia */
    return BSP_QSPI_Write((uint8_t *)(uintptr_t)buf, addr, (uint32_t)len) == QSPI_OK ? 0 : -2;
}

/* Wczytuje oba sloty do s_slot; wynik groups_load (0 / 1 / -1) */
static int load_slots(uint32_t slot0, uint32_t slot1, groups_t *out, uint32_t *seq)
{
    if (app_storage_read(slot0, s_slot[0], GROUPS_SLOT_SIZE) != 0 ||
        app_storage_read(slot1, s_slot[1], GROUPS_SLOT_SIZE) != 0)
        return -2;
    return groups_load(s_slot[0], s_slot[1], GROUPS_SLOT_SIZE, out, seq);
}

int app_storage_groups_load(uint32_t slot0, uint32_t slot1, groups_t *out)
{
    uint32_t seq;
    int r = load_slots(slot0, slot1, out, &seq);
    return r < 0 ? -1 : r;
}

int app_storage_groups_save(uint32_t slot0, uint32_t slot1, const groups_t *gs)
{
    uint32_t seq = 0;
    int cur = load_slots(slot0, slot1, &s_tmp, &seq);
    if (cur == -2)
        return -1;
    int next = groups_next_slot(cur);
    uint32_t addr = next == 0 ? slot0 : slot1;

    int len = groups_encode(gs, cur < 0 ? 1u : seq + 1u, s_slot[0], sizeof s_slot[0]);
    if (len < 0)
        return -2;
    /* bieżący slot zostaje nietknięty – przerwany zapis psuje tylko slot docelowy */
    if (app_storage_erase(addr, GROUPS_SLOT_SIZE) != 0 || app_storage_write(addr, s_slot[0], (size_t)len) != 0)
        return -3;
    return next;
}
