/*
 * Warstwa platformy STM32F746G-DISCO: zegary, FreeRTOS, Ethernet + lwIP (docs/SPEC.md E1, R-NET-1).
 * Używana przez src/main.c i testy sprzętowe test_board_*.
 */
#ifndef APP_PLATFORM_H
#define APP_PLATFORM_H

#include <stdbool.h>
#include <stdint.h>

#include "net_util.h"

/* Zegary 216 MHz, cache, HAL tick. Wywoływane jako pierwsze w main(). */
void app_board_init(void);

/* Tworzy zadanie main_task i uruchamia scheduler FreeRTOS. Nie wraca. */
void app_rtos_start(void (*main_task)(void *arg));

/* true, gdy wywołane z zadania przy działającym schedulerze FreeRTOS. */
bool app_rtos_is_running(void);

/* Opóźnienie / czas – działają przed i po starcie schedulera. */
void app_delay_ms(uint32_t ms);
uint32_t app_millis(void);

/* Start Ethernetu (LAN8742A, RMII), stosu lwIP i klienta DHCP; nie blokuje. 0 = OK. */
int app_net_start(void);

/* Bieżący stan sieci (link, adres z DHCP, MAC). */
void app_net_get_status(net_status_t *st);

/* Próba połączenia TCP: 0 = połączono (i zamknięto), <0 = odmowa / timeout / brak sieci. */
int app_net_tcp_probe(uint32_t ip, uint16_t port, uint32_t timeout_ms);

/* ---------- Niezawodność (R-SYS-1; spec: system-reliability) ---------- */

/*
 * IWDG ~4 s (LSI, /64, RLR 1999) i zadanie „wdg”, które odświeża go tylko, gdy wszystkie zarejestrowane
 * zadania zgłosiły się w ciągu SYS_WDG_STALL_MS; inaczej zapisuje nazwę zawieszonego zadania i czeka na reset.
 * Raz uruchomionego IWDG nie da się zatrzymać. Wywołać po starcie schedulera.
 */
void app_watchdog_start(void);
bool app_watchdog_running(void);

/* Rejestracja zadania do nadzoru (można przed app_watchdog_start); -1 = brak miejsca. */
int app_wdg_register(const char *name);
void app_wdg_alive(int id);

/* Flagi przyczyny ostatniego resetu (SYS_RST_* z sys_util.h), odczytane i wyczyszczone w app_board_init. */
uint32_t app_reset_flags(void);

/* Szczegół z poprzedniego przebiegu (zawieszone zadanie, „stos: <zadanie>”, „brak pamięci”) albo "". */
const char *app_reset_detail(void);

#endif /* APP_PLATFORM_H */
