/* Niezawodność – logika bez sprzętu (spec: system-reliability). */
#include "sys_util.h"

#include <stdio.h>
#include <string.h>

void sys_wdg_init(sys_wdg_t *w)
{
    memset(w, 0, sizeof *w);
}

int sys_wdg_register(sys_wdg_t *w, const char *name, uint32_t now)
{
    if (w->count >= SYS_WDG_MAX_TASKS)
        return -1;
    int id = w->count++;
    w->name[id] = name;
    w->last[id] = now;
    return id;
}

void sys_wdg_alive(sys_wdg_t *w, int id, uint32_t now)
{
    if (id >= 0 && id < w->count)
        w->last[id] = now;
}

int sys_wdg_check(const sys_wdg_t *w, uint32_t now)
{
    for (int i = 0; i < w->count; i++)
        if (now - w->last[i] >= SYS_WDG_STALL_MS) /* różnica modulo 2^32 – odporna na przekręcenie */
            return i;
    return -1;
}

const char *sys_reset_reason(uint32_t flags)
{
    if (flags & SYS_RST_IWDG)
        return "watchdog";
    if (flags & SYS_RST_WWDG)
        return "watchdog okienkowy";
    if (flags & SYS_RST_SFT)
        return "programowy";
    if (flags & SYS_RST_LPWR)
        return "niskie zasilanie";
    if (flags & (SYS_RST_POR | SYS_RST_BOR))
        return "zasilanie"; /* POR ustawia też PIN */
    if (flags & SYS_RST_PIN)
        return "przycisk RESET";
    return "nieznany";
}

int sys_reset_line(char *buf, size_t cap, uint32_t flags, const char *detail)
{
    if (detail != NULL && detail[0] != '\0')
        return snprintf(buf, cap, "RESET: %s (zadanie: %s)", sys_reset_reason(flags), detail);
    return snprintf(buf, cap, "RESET: %s", sys_reset_reason(flags));
}

bool sys_heap_ok(size_t min_ever, size_t total)
{
    return total > 0 && min_ever * 5u >= total; /* >= 20% */
}

int sys_heap_line(char *buf, size_t cap, size_t free_now, size_t min_ever, size_t total)
{
    unsigned pct = total > 0 ? (unsigned)(min_ever * 100u / total) : 0;
    return snprintf(buf, cap, "HEAP free=%lu min=%lu (%u%%) %s", (unsigned long)free_now, (unsigned long)min_ever,
                    pct, sys_heap_ok(min_ever, total) ? "OK" : "NISKO");
}
