/*
 * Klient lokalnego protokołu Tuya 3.3 na płytce (spec: tuya-control, docs/SPEC.md §5, R-TUYA-3..6).
 * Własne zadanie FreeRTOS: trwałe połączenie TCP 6668 z każdym urządzeniem, DP_QUERY po połączeniu,
 * odbiór pushy (cmd 8), heartbeat co 10 s, ponowne łączenie z backoffem, nasłuch UDP 6667 (wykrywanie IP).
 * Wymaga działającej sieci (app_net_start). Funkcje API są bezpieczne wątkowo.
 */
#ifndef TUYA_CLIENT_H
#define TUYA_CLIENT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "device_keys.h"
#include "ui_model.h"

/* Wywoływane z zadania klienta po każdej zmianie stanu urządzenia idx (także link online/offline). */
typedef void (*tuya_client_listener_t)(size_t idx);

/*
 * Startuje klienta dla n urządzeń (tablice muszą żyć do końca programu).
 * Wszystkie urządzenia zaczynają w DEV_LINK_UNKNOWN. 0 = OK.
 */
int tuya_client_start(const dev_info_t *devs, const tuya_endpoint_t *eps, size_t n, tuya_client_listener_t listener);

/* Kopia bieżącego stanu urządzenia; false dla złego indeksu. */
bool tuya_client_get_state(size_t idx, dev_state_t *out);

/* Kolejkuje komendę CONTROL z danym JSON-em DP (np. z tuya_dps_toggle). 0 = przyjęto; < 0 = zły indeks / offline / brak miejsca. */
int tuya_client_send_dps(size_t idx, const char *dps_json);

/* HAL tick ostatniej poprawnie odebranej ramki od urządzenia (0 = nigdy). */
uint32_t tuya_client_last_rx_ms(size_t idx);

/* Bieżący adres IP urządzenia (z konfiguracji lub z wykrywania UDP), 0 = nieznany. */
uint32_t tuya_client_ip(size_t idx);

/* Opcjonalny log diagnostyczny (wołany z zadania klienta; bez kluczy). NULL wyłącza. */
typedef void (*tuya_client_log_t)(const char *line);
void tuya_client_set_log(tuya_client_log_t log);

#endif /* TUYA_CLIENT_H */
