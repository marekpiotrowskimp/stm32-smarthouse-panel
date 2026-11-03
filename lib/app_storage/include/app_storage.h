/*
 * Pamięć QSPI N25Q128A (16 MB) płytki STM32F746G-DISCO i zapis grup (docs/SPEC.md §4.4, R-GRP-6/7;
 * spec: virtual-groups). Kasowanie podsektorami 4 KB. Funkcje wołane z jednego zadania naraz.
 */
#ifndef APP_STORAGE_H
#define APP_STORAGE_H

#include <stddef.h>
#include <stdint.h>

#include "groups.h"

/* Grupy: ostatnie 16 KB pamięci, 2 sloty po GROUPS_SLOT_SIZE (8 KB) */
#define STORAGE_GROUPS_SLOT0 0x00FFC000u
#define STORAGE_GROUPS_SLOT1 0x00FFE000u

/* Inicjalizacja QSPI (można wołać ponownie). 0 = OK. */
int app_storage_init(void);

/* 0 = OK; < 0 = błąd / poza pamięcią. */
int app_storage_read(uint32_t addr, void *buf, size_t len);

/* Kasuje podsektory 4 KB pokrywające [addr, addr + len) (addr wyrównany do 4 KB). */
int app_storage_erase(uint32_t addr, size_t len);

/* Programuje skasowany obszar. */
int app_storage_write(uint32_t addr, const void *buf, size_t len);

/*
 * Odczyt grup z pary slotów (groups_load): 0 / 1 = użyty slot, -1 = oba złe (out bez zmian).
 */
int app_storage_groups_load(uint32_t slot0, uint32_t slot1, groups_t *out);

/*
 * Zapis grup do slotu przeciwnego do bieżącego (groups_next_slot) z seq = bieżący + 1;
 * poprzedni slot zostaje nietknięty (utrata zasilania w trakcie -> po starcie stara wersja).
 * Zwraca numer zapisanego slotu (0 / 1) lub < 0.
 */
int app_storage_groups_save(uint32_t slot0, uint32_t slot1, const groups_t *gs);

#endif /* APP_STORAGE_H */
