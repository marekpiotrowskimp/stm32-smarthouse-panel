/*
 * Treść komend Tuya (JSON) i odczyt stanu z odpowiedzi – logika bez sprzętu (spec: tuya-control,
 * docs/SPEC.md §4.1, §5, §6.4). Numery DP wg urządzeń użytkownika:
 *   BLIND (clkg/tdq): DP1 control "open"|"stop"|"close", DP2 percent_control 0..100 (krok 10, 100 = otwarta)
 *   LIGHT (dj):       DP20 switch_led bool, DP22 bright_value_v2 10..1000
 *   PLUG  (cz):       DP1 switch_1 bool, DP19 cur_power (0,1 W)
 *
 * Funkcje budujące mają semantykę snprintf: zawsze '\0' (cap > 0), zwracają pełną długość.
 */
#ifndef TUYA_DPS_H
#define TUYA_DPS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ui_model.h"

/* {"devId":"<id>","uid":"<id>","t":"<t>","dps":<dps_json>} – payload ramki CONTROL */
int tuya_json_control(char *buf, size_t cap, const char *dev_id, uint32_t t, const char *dps_json);

/* {"gwId":"<id>","devId":"<id>","uid":"<id>","t":"<t>"} – payload ramki DP_QUERY */
int tuya_json_query(char *buf, size_t cap, const char *dev_id, uint32_t t);

/* {"1":"open"} | {"1":"stop"} | {"1":"close"}; inna wartość -> 0 i pusty bufor */
int tuya_dps_blind_control(char *buf, size_t cap, const char *control);

/* {"2":NN} – zaokrąglone do kroku 10, obcięte do 0..100 */
int tuya_dps_blind_position(char *buf, size_t cap, unsigned open_pct);

/* LIGHT {"20":true|false}, PLUG {"1":true|false}; BLIND -> 0 i pusty bufor */
int tuya_dps_switch(char *buf, size_t cap, dev_type_t type, bool on);

/* {"22":NNN} – obcięte do 10..1000 */
int tuya_dps_brightness(char *buf, size_t cap, unsigned value);

/* {"21":"white","23":NNN} – barwa bieli 0 (ciepła)..1000 (zimna), obcięta do zakresu (spec: tile-details) */
int tuya_dps_temperature(char *buf, size_t cap, unsigned value);

/*
 * {"21":"colour","24":"hhhhssssvvvv"} – HSV jako 3 x 4 cyfry hex (małe litery), jak w DP24 urządzeń:
 * h 0..360, s i v 0..1000 (obcięte do zakresu). Np. h=0, s=1000, v=1000 -> "000003e803e8".
 */
int tuya_dps_colour(char *buf, size_t cap, unsigned h, unsigned s, unsigned v);

/*
 * Jasność value (obcięta do 10..1000) w bieżącym trybie żarówki: biel -> {"22":value};
 * kolor -> {"21":"colour","24":...} z bieżącym hue i sat oraz v = value.
 */
int tuya_dps_light_level(char *buf, size_t cap, const dev_state_t *st, unsigned value);

/* Kolor z palety: {"21":"colour","24":...} z hue, s = 1000, v = ui_light_level(st) (min. 10). */
int tuya_dps_palette(char *buf, size_t cap, const dev_state_t *st, unsigned hue);

/*
 * Komenda pojedynczego dotknięcia kafelka (§6.4):
 *   BLIND w ruchu -> stop; open_pct >= 50 -> close; inaczej open
 *   LIGHT / PLUG  -> przeciwny stan on
 *   stan nieznany / offline -> 0 i pusty bufor (brak komendy)
 */
int tuya_dps_toggle(char *buf, size_t cap, dev_type_t type, const dev_state_t *st);

/*
 * Aktualizuje st polami obecnymi w JSON-ie odpowiedzi / pusha ({"devId":..,"dps":{..},..} lub sam {"dps":..}).
 * Pola nieobecne zostają bez zmian; nie zmienia st->link.
 * BLIND: DP1 "open"/"close" -> moving = true, opening = true/false; "stop" -> moving = false; DP2 -> open_pct.
 * LIGHT: DP20 -> on, DP21 -> colour ("colour" = true, inny tekst = false), DP22 -> brightness, DP23 -> temp,
 *        DP24 "hhhhssssvvvv" (12 cyfr hex) -> hue, sat, val (inny format pomijany).  PLUG: DP1 -> on, DP18 -> current_ma, DP19 -> power_dw,
 * DP20 -> voltage_dv.
 * Zwraca true, gdy rozpoznano co najmniej jeden DP danego typu; dla błędnego JSON-a false i st bez zmian.
 */
bool tuya_dps_apply(const char *json, dev_type_t type, dev_state_t *st);

/*
 * Stan łącza po nieudanej próbie połączenia (consecutive_fails = kolejne porażki, licząc tę, >= 1;
 * licznik zerowany po pierwszej poprawnej ramce). Urządzenie jeszcze nieznane (UNKNOWN) zostaje UNKNOWN
 * przy 1. porażce, od 2. -> OFFLINE; ONLINE / OFFLINE -> OFFLINE.
 */
dev_link_t tuya_link_after_fail(dev_link_t cur, unsigned consecutive_fails);

#endif /* TUYA_DPS_H */
