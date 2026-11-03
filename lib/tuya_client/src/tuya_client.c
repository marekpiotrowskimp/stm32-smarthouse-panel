/*
 * Klient lokalnego protokołu Tuya 3.3 (spec: tuya-control, docs/SPEC.md §5, R-TUYA-3..6).
 *
 * Jedno zadanie obsługuje wszystkie urządzenia przez nieblokujące sockety lwIP i select():
 *   - połączenie TCP 6668 (timeout 1,5 s), po nim DP_QUERY; online po pierwszej poprawnej ramce;
 *     port źródłowy z losowego (RNG) początku – lwIP po resecie wybiera te same porty, a SYN
 *     z portu nieaktualnego połączenia (sprzed resetu) urządzenie ignoruje,
 *   - pushe (cmd 8) i odpowiedzi aktualizują stan przez tuya_dps_apply,
 *   - heartbeat co 10 s; 3 bez odpowiedzi -> offline i ponowne łączenie,
 *   - nieudane połączenie -> offline, kolejna próba z backoffem 1 -> 2 -> 4 ... -> 30 s,
 *   - śmieci w strumieniu (zły prefiks / długość) -> zerwanie i ponowne połączenie,
 *   - 3 kolejne błędy deszyfrowania -> offline, log „sprawdź klucz”, próba za 30 s,
 *   - UDP 6667: broadcasty urządzeń dają IP urządzeniom bez adresu (lub po zmianie adresu).
 * Stan współdzielony (stan, IP, czas ostatniej ramki, komenda do wysłania) chroni s_lock;
 * listener jest wołany poza blokadą.
 */
#include "tuya_client.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>

#include "FreeRTOS.h"
#include "app_platform.h"
#include "cmsis_os2.h"
#include "lwip/ip4_addr.h"
#include "lwip/sockets.h"
#include "semphr.h"
#include "stm32f7xx.h"
#include "task.h"
#include "tuya_dps.h"
#include "tuya_proto.h"

#define MAX_DEVICES        16u
#define TUYA_PORT          6668u
#define DISCOVERY_PORT     6667u
#define CONNECT_TIMEOUT_MS 1500u
#define FIRST_RX_MS        5000u  /* od połączenia do pierwszej ramki (odpowiedź DP_QUERY) */
#define HEARTBEAT_MS       10000u
#define HEARTBEAT_MISSES   3u
#define BACKOFF_MIN_MS     1000u
#define BACKOFF_MAX_MS     30000u
#define NO_IP_OFFLINE_MS   15000u /* urządzenie bez IP nie zgłosiło się przez UDP -> offline */
#define DECRYPT_ERR_MAX    3u
#define POLL_MS            20u
#define RX_BUF             1024u
#define TX_BUF             512u
#define JSON_BUF           1024u
#define DPS_BUF            128u
#define TASK_STACK_WORDS   1536u

typedef enum { CONN_IDLE, CONN_CONNECTING, CONN_CONNECTED } conn_t;

typedef struct {
    /* tylko zadanie klienta */
    int      sock;
    conn_t   conn;
    uint32_t t_conn;      /* początek łączenia / chwila połączenia */
    uint32_t t_next_try;
    uint32_t backoff_ms;
    uint32_t t_last_hb;
    unsigned hb_missed;
    unsigned decrypt_errs;
    bool     got_frame;   /* w bieżącym połączeniu */
    unsigned fails;       /* kolejne nieudane próby (zerowane po pierwszej poprawnej ramce) */
    uint32_t seq;
    uint8_t  rx[RX_BUF];
    size_t   rx_len;
    uint8_t  key[TUYA_KEY_LEN];

    /* współdzielone – pod s_lock */
    dev_state_t st;
    uint32_t    ip;
    uint32_t    last_rx_ms;
    bool        has_cmd;
    char        cmd[DPS_BUF];
} dev_ctx_t;

static SemaphoreHandle_t s_lock;
static volatile bool s_started;
static const dev_info_t *s_devs;
static size_t s_count;
static tuya_client_listener_t s_listener;
static volatile tuya_client_log_t s_log;
static dev_ctx_t s_ctx[MAX_DEVICES];
static int s_udp = -1;
static uint16_t s_local_port; /* następny port źródłowy TCP */
static uint32_t s_t_start;
static uint32_t s_changed; /* maska urządzeń do zgłoszenia listenerowi */

/* bufory robocze zadania klienta */
static uint8_t s_tx[TX_BUF];
static char s_json[JSON_BUF];
static char s_body[TX_BUF];

static void lock(void)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
}

static void unlock(void)
{
    xSemaphoreGive(s_lock);
}

static void logf_dev(size_t i, const char *what)
{
    tuya_client_log_t log = s_log;
    if (log == NULL)
        return;
    char line[96];
    snprintf(line, sizeof line, "TUYA %s: %s", s_devs[i].name, what);
    log(line);
}

static bool state_eq(const dev_state_t *a, const dev_state_t *b)
{
    return a->link == b->link && a->on == b->on && a->open_pct == b->open_pct && a->moving == b->moving &&
           a->opening == b->opening && a->brightness == b->brightness && a->temp == b->temp &&
           a->power_dw == b->power_dw && a->voltage_dv == b->voltage_dv && a->current_ma == b->current_ma &&
           a->colour == b->colour && a->hue == b->hue && a->sat == b->sat && a->val == b->val;
}

static void set_link(size_t i, dev_link_t link)
{
    lock();
    if (s_ctx[i].st.link != link) {
        s_ctx[i].st.link = link;
        s_changed |= 1u << i;
    }
    unlock();
}

/* ---------- połączenie ---------- */

static void conn_close(dev_ctx_t *c)
{
    if (c->sock >= 0)
        lwip_close(c->sock);
    c->sock = -1;
    c->conn = CONN_IDLE;
    c->rx_len = 0;
}

/* Nieudane połączenie / utrata urządzenia: następna próba po backoffie; offline wg tuya_link_after_fail
 * (urządzenie jeszcze nieznane zostaje „Łączenie…” po pierwszej porażce). */
static void conn_fail(size_t i, uint32_t now, const char *why)
{
    dev_ctx_t *c = &s_ctx[i];
    conn_close(c);
    c->t_next_try = now + c->backoff_ms;
    c->backoff_ms = c->backoff_ms * 2u > BACKOFF_MAX_MS ? BACKOFF_MAX_MS : c->backoff_ms * 2u;
    if (c->fails < UINT_MAX)
        c->fails++;
    if (c->st.link != DEV_LINK_OFFLINE)
        logf_dev(i, why);
    set_link(i, tuya_link_after_fail(c->st.link, c->fails));
}

/* Zerwanie działającego połączenia (urządzenie zamknęło, śmieci): szybka ponowna próba bez zmiany stanu. */
static void conn_drop(size_t i, uint32_t now)
{
    dev_ctx_t *c = &s_ctx[i];
    conn_close(c);
    c->t_next_try = now + BACKOFF_MIN_MS;
}

static int send_frame(size_t i, uint32_t cmd, const char *json)
{
    dev_ctx_t *c = &s_ctx[i];
    int n = tuya_build_frame(++c->seq, cmd, json, c->key, s_tx, sizeof s_tx);
    if (n < 0)
        return n;
    return lwip_send(c->sock, s_tx, (size_t)n, 0) == n ? 0 : -1;
}

#define LOCAL_PORT_MIN 49152u /* zakres portów dynamicznych (IANA) */

/* Losowy początek z RNG (zegar 48 MHz z PLLQ); bez RNG – zakres od początku. */
static void local_port_init(void)
{
    RCC->AHB2ENR |= RCC_AHB2ENR_RNGEN;
    RNG->CR |= RNG_CR_RNGEN;
    uint32_t r = 0;
    for (uint32_t t0 = app_millis(); app_millis() - t0 < 10u;) {
        if (RNG->SR & RNG_SR_DRDY) {
            r = RNG->DR;
            break;
        }
    }
    s_local_port = (uint16_t)(LOCAL_PORT_MIN + r % (65536u - LOCAL_PORT_MIN));
}

static uint16_t next_local_port(void)
{
    uint16_t p = s_local_port;
    s_local_port = p == 65535u ? (uint16_t)LOCAL_PORT_MIN : (uint16_t)(p + 1u);
    return p;
}

static void conn_start(size_t i, uint32_t now)
{
    dev_ctx_t *c = &s_ctx[i];
    lock();
    uint32_t ip = c->ip;
    unlock();

    c->t_conn = now;
    c->sock = lwip_socket(AF_INET, SOCK_STREAM, 0);
    if (c->sock < 0) {
        conn_fail(i, now, "brak socketu");
        return;
    }
    lwip_fcntl(c->sock, F_SETFL, lwip_fcntl(c->sock, F_GETFL, 0) | O_NONBLOCK);

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof addr);
    addr.sin_len = sizeof addr;
    addr.sin_family = AF_INET;
    addr.sin_port = lwip_htons(next_local_port());
    addr.sin_addr.s_addr = lwip_htonl(INADDR_ANY);
    lwip_bind(c->sock, (struct sockaddr *)&addr, sizeof addr); /* zajęty port -> lwIP wybierze sam */
    addr.sin_port = lwip_htons(TUYA_PORT);
    addr.sin_addr.s_addr = lwip_htonl(ip);
    if (lwip_connect(c->sock, (struct sockaddr *)&addr, sizeof addr) != 0 && errno != EINPROGRESS) {
        conn_fail(i, now, "odmowa połączenia");
        return;
    }
    c->conn = CONN_CONNECTING;
}

static void conn_established(size_t i, uint32_t now)
{
    dev_ctx_t *c = &s_ctx[i];
    c->conn = CONN_CONNECTED;
    c->t_conn = now;
    c->t_last_hb = now;
    c->hb_missed = 0;
    c->got_frame = false;
    c->rx_len = 0;
    tuya_json_query(s_body, sizeof s_body, s_devs[i].id, now / 1000u);
    if (send_frame(i, TUYA_CMD_DP_QUERY, s_body) != 0)
        conn_fail(i, now, "błąd wysyłania");
}

/* ---------- odbiór ---------- */

static void handle_frame(size_t i, const tuya_frame_t *f, uint32_t now)
{
    dev_ctx_t *c = &s_ctx[i];
    c->hb_missed = 0;
    if (!c->got_frame) {
        c->got_frame = true;
        c->backoff_ms = BACKOFF_MIN_MS;
        c->fails = 0;
    }

    bool update = false;
    dev_state_t st;
    int n = f->payload_len > 0 ? tuya_decode_payload(f, c->key, s_json, sizeof s_json) : 0;
    if (n == TUYA_ERR_DECRYPT) {
        if (++c->decrypt_errs >= DECRYPT_ERR_MAX) {
            c->decrypt_errs = 0;
            conn_close(c);
            c->t_next_try = now + BACKOFF_MAX_MS;
            c->backoff_ms = BACKOFF_MAX_MS;
            logf_dev(i, "błąd deszyfrowania - sprawdź klucz");
            set_link(i, DEV_LINK_OFFLINE);
        }
        return;
    }
    c->decrypt_errs = 0;
    if (n > 0) {
        lock();
        st = c->st;
        unlock();
        update = tuya_dps_apply(s_json, s_devs[i].type, &st);
    }

    lock();
    c->last_rx_ms = now;
    if (update && !state_eq(&c->st, &st)) {
        st.link = c->st.link;
        c->st = st;
        s_changed |= 1u << i;
    }
    if (c->st.link != DEV_LINK_ONLINE) {
        c->st.link = DEV_LINK_ONLINE;
        s_changed |= 1u << i;
    }
    unlock();
}

static void conn_receive(size_t i, uint32_t now)
{
    dev_ctx_t *c = &s_ctx[i];
    int r = lwip_recv(c->sock, c->rx + c->rx_len, sizeof c->rx - c->rx_len, 0);
    if (r <= 0) {
        if (r < 0 && (errno == EWOULDBLOCK || errno == EAGAIN))
            return;
        if (c->got_frame)
            conn_drop(i, now);
        else
            conn_fail(i, now, "połączenie zamknięte");
        return;
    }
    c->rx_len += (size_t)r;

    while (c->conn == CONN_CONNECTED && c->rx_len > 0) {
        tuya_frame_t f;
        int pr = tuya_parse_frame(c->rx, c->rx_len, &f);
        if (pr == TUYA_ERR_INCOMPLETE) {
            if (c->rx_len == sizeof c->rx)
                conn_drop(i, now); /* ramka większa niż bufor */
            return;
        }
        if (pr == TUYA_ERR_PREFIX || pr == TUYA_ERR_LEN || f.frame_len == 0 || f.frame_len > c->rx_len) {
            conn_drop(i, now); /* brak resynchronizacji w strumieniu – nowe połączenie */
            return;
        }
        if (pr == TUYA_OK)
            handle_frame(i, &f, now);
        if (c->conn != CONN_CONNECTED)
            return;
        /* CRC / SUFFIX: ramka pominięta */
        memmove(c->rx, c->rx + f.frame_len, c->rx_len - f.frame_len);
        c->rx_len -= f.frame_len;
    }
}

/* ---------- wykrywanie UDP 6667 ---------- */

/* Wartość tekstowa pola "key" z płaskiego JSON-a broadcastu. */
static bool json_str_field(const char *json, const char *key, char *out, size_t cap)
{
    char pat[16];
    snprintf(pat, sizeof pat, "\"%s\"", key);
    const char *p = strstr(json, pat);
    if (p == NULL)
        return false;
    p += strlen(pat);
    while (*p == ' ')
        p++;
    if (*p++ != ':')
        return false;
    while (*p == ' ')
        p++;
    if (*p++ != '"')
        return false;
    size_t n = 0;
    while (*p != '"' && *p != '\0' && n + 1 < cap)
        out[n++] = *p++;
    out[n] = '\0';
    return *p == '"';
}

static void discovery_receive(uint32_t now)
{
    int r = lwip_recv(s_udp, s_tx, sizeof s_tx, 0);
    if (r <= 0)
        return;
    if (tuya_decode_discovery(s_tx, (size_t)r, s_json, sizeof s_json) <= 0)
        return;

    char id[32], ip_str[16];
    if (!json_str_field(s_json, "gwId", id, sizeof id) || !json_str_field(s_json, "ip", ip_str, sizeof ip_str))
        return;
    ip4_addr_t a;
    if (!ip4addr_aton(ip_str, &a))
        return;
    uint32_t ip = lwip_ntohl(ip4_addr_get_u32(&a));

    for (size_t i = 0; i < s_count; i++) {
        if (strcmp(s_devs[i].id, id) != 0)
            continue;
        dev_ctx_t *c = &s_ctx[i];
        lock();
        bool changed = c->ip != ip;
        c->ip = ip;
        unlock();
        if (changed) {
            logf_dev(i, "adres z wykrywania UDP");
            conn_close(c);
            c->t_next_try = now;
            c->backoff_ms = BACKOFF_MIN_MS;
        } else if (c->conn == CONN_IDLE && c->st.link == DEV_LINK_OFFLINE) {
            c->t_next_try = now; /* urządzenie wróciło – nie czekaj na backoff */
        }
        return;
    }
}

static void discovery_open(void)
{
    s_udp = lwip_socket(AF_INET, SOCK_DGRAM, 0);
    if (s_udp < 0)
        return;
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof addr);
    addr.sin_len = sizeof addr;
    addr.sin_family = AF_INET;
    addr.sin_port = lwip_htons(DISCOVERY_PORT);
    addr.sin_addr.s_addr = lwip_htonl(INADDR_ANY);
    if (lwip_bind(s_udp, (struct sockaddr *)&addr, sizeof addr) != 0) {
        lwip_close(s_udp);
        s_udp = -1;
        return;
    }
    lwip_fcntl(s_udp, F_SETFL, lwip_fcntl(s_udp, F_GETFL, 0) | O_NONBLOCK);
}

/* ---------- pętla zadania ---------- */

static void service_timers(size_t i, uint32_t now)
{
    dev_ctx_t *c = &s_ctx[i];
    lock();
    uint32_t ip = c->ip;
    unlock();

    switch (c->conn) {
    case CONN_IDLE:
        if (ip == 0) {
            if (c->st.link == DEV_LINK_UNKNOWN && now - s_t_start >= NO_IP_OFFLINE_MS)
                set_link(i, DEV_LINK_OFFLINE);
        } else if ((int32_t)(now - c->t_next_try) >= 0) {
            conn_start(i, now);
        }
        break;
    case CONN_CONNECTING:
        if (now - c->t_conn >= CONNECT_TIMEOUT_MS)
            conn_fail(i, now, "brak odpowiedzi (timeout)");
        break;
    case CONN_CONNECTED:
        if (!c->got_frame && now - c->t_conn >= FIRST_RX_MS) {
            conn_fail(i, now, "brak odpowiedzi na zapytanie");
            break;
        }
        if (now - c->t_last_hb >= HEARTBEAT_MS) {
            if (++c->hb_missed > HEARTBEAT_MISSES) {
                conn_fail(i, now, "brak heartbeatu");
                break;
            }
            c->t_last_hb = now;
            if (send_frame(i, TUYA_CMD_HEART_BEAT, NULL) != 0) {
                conn_fail(i, now, "błąd wysyłania");
                break;
            }
        }
        if (c->got_frame) {
            lock();
            bool has = c->has_cmd;
            if (has) {
                tuya_json_control(s_body, sizeof s_body, s_devs[i].id, now / 1000u, c->cmd);
                c->has_cmd = false;
            }
            unlock();
            if (has && send_frame(i, TUYA_CMD_CONTROL, s_body) != 0)
                conn_fail(i, now, "błąd wysyłania");
        }
        break;
    }
}

static void notify_changes(void)
{
    lock();
    uint32_t changed = s_changed;
    s_changed = 0;
    unlock();
    for (size_t i = 0; i < s_count && changed != 0; i++) {
        if ((changed & (1u << i)) && s_listener != NULL)
            s_listener(i);
    }
}

static void client_task(void *arg)
{
    (void)arg;
    s_t_start = app_millis();
    local_port_init();
    discovery_open();
    int wdg_id = app_wdg_register("tuya");

    for (;;) {
        app_wdg_alive(wdg_id); /* pętla co <= POLL_MS – nadzór IWDG (R-SYS-1) */
        fd_set rd, wr;
        FD_ZERO(&rd);
        FD_ZERO(&wr);
        int maxfd = -1;
        if (s_udp >= 0) {
            FD_SET(s_udp, &rd);
            maxfd = s_udp;
        }
        for (size_t i = 0; i < s_count; i++) {
            dev_ctx_t *c = &s_ctx[i];
            if (c->conn == CONN_CONNECTING)
                FD_SET(c->sock, &wr);
            else if (c->conn == CONN_CONNECTED)
                FD_SET(c->sock, &rd);
            else
                continue;
            if (c->sock > maxfd)
                maxfd = c->sock;
        }

        struct timeval tv = { .tv_sec = 0, .tv_usec = (long)POLL_MS * 1000 };
        int n = maxfd >= 0 ? lwip_select(maxfd + 1, &rd, &wr, NULL, &tv) : 0;
        if (maxfd < 0)
            app_delay_ms(POLL_MS);
        uint32_t now = app_millis();

        if (n > 0) {
            if (s_udp >= 0 && FD_ISSET(s_udp, &rd))
                discovery_receive(now);
            for (size_t i = 0; i < s_count; i++) {
                dev_ctx_t *c = &s_ctx[i];
                if (c->conn == CONN_CONNECTING && FD_ISSET(c->sock, &wr)) {
                    int err = 0;
                    socklen_t len = sizeof err;
                    lwip_getsockopt(c->sock, SOL_SOCKET, SO_ERROR, &err, &len);
                    if (err == 0)
                        conn_established(i, now);
                    else
                        conn_fail(i, now, "odmowa połączenia");
                } else if (c->conn == CONN_CONNECTED && FD_ISSET(c->sock, &rd)) {
                    conn_receive(i, now);
                }
            }
        }

        for (size_t i = 0; i < s_count; i++)
            service_timers(i, now);
        notify_changes();
    }
}

/* ---------- API ---------- */

int tuya_client_start(const dev_info_t *devs, const tuya_endpoint_t *eps, size_t n, tuya_client_listener_t listener)
{
    if (s_started || devs == NULL || eps == NULL || n == 0 || n > MAX_DEVICES)
        return -1;
    if (s_lock == NULL && (s_lock = xSemaphoreCreateMutex()) == NULL)
        return -2;

    s_devs = devs;
    s_count = n;
    s_listener = listener;
    for (size_t i = 0; i < n; i++) {
        dev_ctx_t *c = &s_ctx[i];
        memset(c, 0, sizeof *c);
        c->sock = -1;
        c->conn = CONN_IDLE;
        c->backoff_ms = BACKOFF_MIN_MS;
        c->ip = eps[i].ip;
        c->st.link = DEV_LINK_UNKNOWN;
        c->st.power_dw = -1;
        c->st.voltage_dv = -1;
        c->st.current_ma = -1;
        memcpy(c->key, eps[i].key, TUYA_KEY_LEN);
    }
    if (xTaskCreate(client_task, "tuya", TASK_STACK_WORDS, NULL, osPriorityNormal, NULL) != pdPASS)
        return -3;
    s_started = true;
    return 0;
}

bool tuya_client_get_state(size_t idx, dev_state_t *out)
{
    if (!s_started || idx >= s_count)
        return false;
    lock();
    *out = s_ctx[idx].st;
    unlock();
    return true;
}

int tuya_client_send_dps(size_t idx, const char *dps_json)
{
    if (!s_started || idx >= s_count || dps_json == NULL)
        return -1;
    if (strlen(dps_json) >= DPS_BUF || dps_json[0] == '\0')
        return -3;
    int r = 0;
    lock();
    if (s_ctx[idx].st.link != DEV_LINK_ONLINE) {
        r = -2;
    } else {
        strcpy(s_ctx[idx].cmd, dps_json);
        s_ctx[idx].has_cmd = true; /* nowsza komenda zastępuje niewysłaną */
    }
    unlock();
    return r;
}

uint32_t tuya_client_last_rx_ms(size_t idx)
{
    if (!s_started || idx >= s_count)
        return 0;
    lock();
    uint32_t t = s_ctx[idx].last_rx_ms;
    unlock();
    return t;
}

uint32_t tuya_client_ip(size_t idx)
{
    if (!s_started || idx >= s_count)
        return 0;
    lock();
    uint32_t ip = s_ctx[idx].ip;
    unlock();
    return ip;
}

void tuya_client_set_log(tuya_client_log_t log)
{
    s_log = log;
}
