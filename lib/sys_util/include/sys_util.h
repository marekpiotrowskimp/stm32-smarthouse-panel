/*
 * Niezawodność – logika bez sprzętu (docs/SPEC.md R-SYS-1, R-SYS-2; spec: system-reliability):
 * nadzór żywotności zadań (przed odświeżeniem IWDG), opis przyczyny resetu, linia logu sterty.
 */
#ifndef SYS_UTIL_H
#define SYS_UTIL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* ---------- Nadzór zadań ---------- */

#define SYS_WDG_MAX_TASKS 8
#define SYS_WDG_STALL_MS  3000u /* zadanie milczące tyle ms = zawieszone (IWDG ~4 s) */

typedef struct {
    const char *name[SYS_WDG_MAX_TASKS];
    uint32_t    last[SYS_WDG_MAX_TASKS]; /* ostatnie zgłoszenie [ms] */
    uint8_t     count;
} sys_wdg_t;

void sys_wdg_init(sys_wdg_t *w);

/* Rejestracja zadania (czas zgłoszenia = now); identyfikator 0.. albo -1 (brak miejsca). */
int sys_wdg_register(sys_wdg_t *w, const char *name, uint32_t now);

/* Zgłoszenie żywotności; zły identyfikator ignorowany. */
void sys_wdg_alive(sys_wdg_t *w, int id, uint32_t now);

/* -1 = wszystkie zgłosiły się < SYS_WDG_STALL_MS temu (lub brak zadań); inaczej indeks pierwszego milczącego. */
int sys_wdg_check(const sys_wdg_t *w, uint32_t now);

/* ---------- Przyczyna resetu ---------- */

/* Flagi niezależne od rejestrów (mapowane z RCC->CSR w app_platform) */
#define SYS_RST_PIN  (1u << 0)
#define SYS_RST_POR  (1u << 1)
#define SYS_RST_SFT  (1u << 2)
#define SYS_RST_IWDG (1u << 3)
#define SYS_RST_WWDG (1u << 4)
#define SYS_RST_LPWR (1u << 5)
#define SYS_RST_BOR  (1u << 6)

/*
 * Pierwszeństwo: IWDG "watchdog", WWDG "watchdog okienkowy", SFT "programowy", LPWR "niskie zasilanie",
 * POR / BOR "zasilanie", PIN "przycisk RESET", brak flag "nieznany".
 */
const char *sys_reset_reason(uint32_t flags);

/* "RESET: <przyczyna> (zadanie: <detail>)" albo "RESET: <przyczyna>" (detail NULL / ""); semantyka snprintf. */
int sys_reset_line(char *buf, size_t cap, uint32_t flags, const char *detail);

/* ---------- Sterta ---------- */

/* true, gdy najmniejsza wolna sterta od startu >= 20% całej (R-SYS-2). */
bool sys_heap_ok(size_t min_ever, size_t total);

/* "HEAP free=<free> min=<min> (<min*100/total>%) OK|NISKO"; semantyka snprintf. */
int sys_heap_line(char *buf, size_t cap, size_t free_now, size_t min_ever, size_t total);

#endif /* SYS_UTIL_H */
