/*
 * Grupy wirtualne – model, stan zbiorczy, komendy fan-out, lista kafelków i format zapisu
 * (docs/SPEC.md §4.4, §6.2, §6.4, R-GRP-2..7, R-GRP-9; spec: virtual-groups). Bez sprzętu.
 */
#ifndef GROUPS_H
#define GROUPS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ui_model.h"

#define GROUP_MAX         16
#define GROUP_MEMBERS_MAX 16
#define GROUP_MEMBERS_MIN 2
#define GROUP_NAME_LEN    32 /* z '\0' – max 31 bajtów UTF-8 */
#define GROUP_DEVID_LEN   24 /* z '\0' */

typedef struct {
    uint8_t    id;                                   /* 1..255, unikalny; 0 = wolny */
    char       name[GROUP_NAME_LEN];                 /* UTF-8 */
    dev_type_t type;                                 /* wszyscy członkowie tego typu */
    uint8_t    member_count;
    char       members[GROUP_MEMBERS_MAX][GROUP_DEVID_LEN]; /* devId Tuya */
    uint8_t    position;                             /* miejsce na liście kafelków (0 = pierwszy) */
} group_cfg_t;

typedef struct {
    uint8_t     count;
    group_cfg_t g[GROUP_MAX];
} groups_t;

/* Indeks urządzenia o danym devId albo -1 (np. urządzenie zniknęło z konfiguracji). */
int groups_find_device(const dev_info_t *devs, size_t n, const char *dev_id);

/* ---------- Stan zbiorczy (§4.4) ---------- */

typedef struct {
    uint8_t members;    /* członkowie obecni w konfiguracji */
    uint8_t missing;    /* devId spoza konfiguracji – pomijani */
    uint8_t online;
    uint8_t unknown;    /* członkowie o stanie nieznanym (jeszcze bez połączenia) */
    uint8_t on;         /* LIGHT / PLUG: online i wł. */
    uint8_t open;       /* BLIND: online i open_pct >= 50 (jak tuya_dps_toggle) */
    bool    any_moving; /* BLIND: którykolwiek online w ruchu */
    uint8_t avg_pct;    /* BLIND: średnia open_pct członków online (zaokrąglona), 0 gdy brak */
} group_summary_t;

void groups_summary(const group_cfg_t *g, const dev_info_t *devs, const dev_state_t *states, size_t n,
                    group_summary_t *out);

/*
 * Wygląd kafelka grupy (§6.2): ikona typu wg stanu zbiorczego, badge = liczba członków (members).
 *   brak członków online        -> >= 1 nieznany: dimmed, "Łączenie…"; inaczej dimmed, "Offline" (offline_mark)
 *   część offline               -> "3/4 online"
 *   LIGHT / PLUG (wszyscy online): "Wszystkie wł." | "2/4 wł." | "Wył."; ikona ON gdy >= 1 wł.,
 *                                  LIGHT glow_pct 100 (wszystkie) / 50 (część)
 *   BLIND (wszyscy online)      : "W ruchu" | "Zamknięte" (śr. 0) | "Otwarte" (śr. 100) | "Średnio 40%";
 *                                  ikona jak dla rolety o open_pct = średnia
 */
typedef struct {
    ui_tile_view_t view;
    uint8_t        badge;
} group_view_t;

void groups_tile_view(const group_cfg_t *g, const group_summary_t *s, group_view_t *out);

/* ---------- Komendy (fan-out, §6.4) ---------- */

typedef struct {
    size_t idx;      /* indeks urządzenia */
    char   dps[48];  /* JSON DP */
} group_cmd_t;

/*
 * Jedno dotknięcie kafelka grupy -> komenda dla każdego członka online (offline pomijani, R-GRP-9):
 *   BLIND: którykolwiek w ruchu -> "stop";
 *          >= 1 otwarty (open_pct >= 50, jak pojedynczy kafelek) -> "close"; inaczej "open"
 *   LIGHT / PLUG: >= 1 wł. -> wył.; inaczej wł.
 * Zwraca liczbę komend (<= cap), w kolejności members.
 */
size_t groups_toggle(const group_cfg_t *g, const dev_info_t *devs, const dev_state_t *states, size_t n,
                     group_cmd_t *out, size_t cap);

/* Ta sama komenda dps dla członków zaznaczonych w mask (bit i = members[i]) i online. */
size_t groups_command(const group_cfg_t *g, const dev_info_t *devs, const dev_state_t *states, size_t n,
                      uint16_t mask, const char *dps, group_cmd_t *out, size_t cap);

/* Jasność value dla zaznaczonych członków online – każdemu tuya_dps_light_level wg jego trybu i koloru. */
size_t groups_command_level(const group_cfg_t *g, const dev_info_t *devs, const dev_state_t *states, size_t n,
                            uint16_t mask, unsigned value, group_cmd_t *out, size_t cap);

/* ---------- Konfiguracja ---------- */

/*
 * Grupy startowe (O2): id 1 "Salon – rolety" = Salon roleta, Salon s1, Salon s2, Salon s3 (pozycja 0);
 * id 2 "Wszystkie rolety" = wszystkie urządzenia BLIND w kolejności devs (pozycja 1).
 * Grupa, która miałaby < 2 członków, jest pomijana.
 */
void groups_defaults(const dev_info_t *devs, size_t n, groups_t *out);

bool groups_can_add(const groups_t *gs);

/* Najmniejszy wolny id 1..255 (0 = brak). */
uint8_t groups_free_id(const groups_t *gs);

/* Usuwa grupę o id; true = usunięto. Kolejność pozostałych bez zmian. */
bool groups_delete(groups_t *gs, uint8_t id);

/* ---------- Lista kafelków (§6.6) ---------- */

typedef enum { TILE_DEVICE, TILE_GROUP, TILE_ADD } tile_kind_t;

typedef struct {
    tile_kind_t kind;
    uint8_t     index; /* indeks urządzenia (TILE_DEVICE) lub grupy w gs->g (TILE_GROUP) */
} tile_ref_t;

/*
 * Urządzenia w kolejności, grupy na swoich pozycjach, na końcu kafelek TILE_ADD „＋ Nowa grupa”.
 * Grupy sortowane rosnąco wg position, przy równych – nowsza (dalej w gs) pierwsza; każda trafia na
 * indeks max(position, indeks poprzedniej grupy + 1), a gdy to poza listą – na koniec (przed TILE_ADD).
 * Zwraca liczbę kafelków (<= cap).
 */
size_t groups_tile_list(const groups_t *gs, size_t n_devs, tile_ref_t *out, size_t cap);

/* ---------- Format zapisu we flash (2 sloty naprzemiennie, §4.4, R-GRP-6/7) ---------- */

#define GROUPS_SLOT_SIZE 8192u        /* 2 podsektory QSPI po 4 KB */
#define GROUPS_MAGIC     0x31505247u  /* "GRP1" */
#define GROUPS_VERSION   1u

#define GROUPS_HDR_LEN    20u  /* magic u32 | version u32 | seq u32 | payload_len u32 | crc32 u32 (LE) */
#define GROUPS_RECORD_LEN 420u /* id u8 | type u8 | member_count u8 | position u8 | name[32] | members[16][24] */

/*
 * Slot = nagłówek + payload; payload = count u8 + count rekordów GROUPS_RECORD_LEN.
 * crc32 = CRC-32 IEEE (ta sama funkcja co tuya_crc32, liczona przyrostowo – bez bufora pośredniego)
 * z pierwszych 16 bajtów nagłówka i payloadu.
 * Zwraca długość lub < 0 (za mały bufor).
 */
int groups_encode(const groups_t *gs, uint32_t seq, uint8_t *buf, size_t cap);

/*
 * 0 = poprawny slot (out, seq wypełnione); < 0 = zły magic / wersja / długość / CRC / treść
 * (count > 16, member_count > 16, zły typ, id 0 lub powtórzone, brak '\0' w nazwie lub devId).
 * Przy błędzie out bez zmian.
 */
int groups_decode(const uint8_t *buf, size_t len, groups_t *out, uint32_t *seq);

/*
 * Wybór slotu: poprawny o nowszym seq (porównanie z przepełnieniem: (int32_t)(a - b) > 0).
 * Zwraca 0 / 1 = użyty slot, -1 = oba złe (out bez zmian – wołający bierze groups_defaults).
 */
int groups_load(const uint8_t *slot0, const uint8_t *slot1, size_t len, groups_t *out, uint32_t *seq);

/* Slot do następnego zapisu: przeciwny do bieżącego (-1 -> 0). */
int groups_next_slot(int current);

#endif /* GROUPS_H */
