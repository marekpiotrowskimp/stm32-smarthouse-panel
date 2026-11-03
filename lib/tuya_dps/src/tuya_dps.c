/* Treść komend Tuya i odczyt stanu z odpowiedzi (spec: tuya-control). */
#include "tuya_dps.h"

#include <stdio.h>
#include <string.h>

#define JSON_MAX_DEPTH 8

static int empty(char *buf, size_t cap)
{
    if (cap > 0)
        buf[0] = '\0';
    return 0;
}

int tuya_json_control(char *buf, size_t cap, const char *dev_id, uint32_t t, const char *dps_json)
{
    return snprintf(buf, cap, "{\"devId\":\"%s\",\"uid\":\"%s\",\"t\":\"%lu\",\"dps\":%s}",
                    dev_id, dev_id, (unsigned long)t, dps_json);
}

int tuya_json_query(char *buf, size_t cap, const char *dev_id, uint32_t t)
{
    return snprintf(buf, cap, "{\"gwId\":\"%s\",\"devId\":\"%s\",\"uid\":\"%s\",\"t\":\"%lu\"}",
                    dev_id, dev_id, dev_id, (unsigned long)t);
}

int tuya_dps_blind_control(char *buf, size_t cap, const char *control)
{
    if (strcmp(control, "open") != 0 && strcmp(control, "stop") != 0 && strcmp(control, "close") != 0)
        return empty(buf, cap);
    return snprintf(buf, cap, "{\"1\":\"%s\"}", control);
}

int tuya_dps_blind_position(char *buf, size_t cap, unsigned open_pct)
{
    if (open_pct > 100)
        open_pct = 100;
    return snprintf(buf, cap, "{\"2\":%u}", (open_pct + 5) / 10 * 10);
}

int tuya_dps_switch(char *buf, size_t cap, dev_type_t type, bool on)
{
    const char *v = on ? "true" : "false";
    switch (type) {
    case DEV_TYPE_LIGHT: return snprintf(buf, cap, "{\"20\":%s}", v);
    case DEV_TYPE_PLUG:  return snprintf(buf, cap, "{\"1\":%s}", v);
    default:             return empty(buf, cap);
    }
}

int tuya_dps_brightness(char *buf, size_t cap, unsigned value)
{
    if (value < 10)
        value = 10;
    if (value > 1000)
        value = 1000;
    return snprintf(buf, cap, "{\"22\":%u}", value);
}

int tuya_dps_temperature(char *buf, size_t cap, unsigned value)
{
    if (value > 1000)
        value = 1000;
    return snprintf(buf, cap, "{\"21\":\"white\",\"23\":%u}", value);
}

int tuya_dps_colour(char *buf, size_t cap, unsigned h, unsigned s, unsigned v)
{
    if (h > 360)
        h = 360;
    if (s > 1000)
        s = 1000;
    if (v > 1000)
        v = 1000;
    return snprintf(buf, cap, "{\"21\":\"colour\",\"24\":\"%04x%04x%04x\"}", h, s, v);
}

int tuya_dps_toggle(char *buf, size_t cap, dev_type_t type, const dev_state_t *st)
{
    if (st->link != DEV_LINK_ONLINE)
        return empty(buf, cap);
    if (type == DEV_TYPE_BLIND) {
        const char *c = st->moving ? "stop" : st->open_pct >= 50 ? "close" : "open";
        return tuya_dps_blind_control(buf, cap, c);
    }
    return tuya_dps_switch(buf, cap, type, !st->on);
}

/* ---------- minimalny parser JSON (dane z sieci: zawsze ograniczony bufor, bez rekurencji ponad JSON_MAX_DEPTH) ---------- */

typedef enum { JV_OTHER, JV_STRING, JV_NUMBER, JV_BOOL } jv_kind_t;

typedef struct {
    jv_kind_t kind;
    char      str[16]; /* JV_STRING, obcięty */
    long      num;     /* JV_NUMBER, część całkowita */
    bool      b;       /* JV_BOOL */
} jvalue_t;

static void skip_ws(const char **p)
{
    while (**p == ' ' || **p == '\t' || **p == '\r' || **p == '\n')
        (*p)++;
}

/* Łańcuch w cudzysłowie; out (może być NULL) dostaje treść obciętą do cap-1 znaków. */
static bool parse_string(const char **p, char *out, size_t cap)
{
    const char *s = *p;
    size_t n = 0;
    if (*s != '"')
        return false;
    s++;
    for (;;) {
        char c = *s++;
        if (c == '\0')
            return false;
        if (c == '"')
            break;
        if (c == '\\') {
            if (*s == '\0')
                return false;
            c = *s++;
        }
        if (out && n + 1 < cap)
            out[n++] = c;
    }
    if (out && cap > 0)
        out[n] = '\0';
    *p = s;
    return true;
}

static bool parse_number(const char **p, long *out)
{
    const char *s = *p;
    bool neg = false;
    long v = 0;
    if (*s == '-') {
        neg = true;
        s++;
    }
    if (*s < '0' || *s > '9')
        return false;
    while (*s >= '0' && *s <= '9') {
        if (v < 100000000L)
            v = v * 10 + (*s - '0');
        s++;
    }
    if (*s == '.') {
        s++;
        while (*s >= '0' && *s <= '9')
            s++;
    }
    if (*s == 'e' || *s == 'E') {
        s++;
        if (*s == '+' || *s == '-')
            s++;
        while (*s >= '0' && *s <= '9')
            s++;
    }
    *out = neg ? -v : v;
    *p = s;
    return true;
}

static bool parse_literal(const char **p, const char *lit)
{
    size_t n = strlen(lit);
    if (strncmp(*p, lit, n) != 0)
        return false;
    *p += n;
    return true;
}

static bool parse_value(const char **p, jvalue_t *v, int depth);

/* Obiekt lub tablica – tylko pominięcie (walidacja składni). */
static bool skip_container(const char **p, int depth)
{
    char open = **p, close = open == '{' ? '}' : ']';
    if (depth >= JSON_MAX_DEPTH)
        return false;
    (*p)++;
    skip_ws(p);
    if (**p == close) {
        (*p)++;
        return true;
    }
    for (;;) {
        jvalue_t v;
        if (open == '{') {
            if (!parse_string(p, NULL, 0))
                return false;
            skip_ws(p);
            if (**p != ':')
                return false;
            (*p)++;
            skip_ws(p);
        }
        if (!parse_value(p, &v, depth + 1))
            return false;
        skip_ws(p);
        if (**p == ',') {
            (*p)++;
            skip_ws(p);
            continue;
        }
        if (**p != close)
            return false;
        (*p)++;
        return true;
    }
}

static bool parse_value(const char **p, jvalue_t *v, int depth)
{
    v->kind = JV_OTHER;
    switch (**p) {
    case '"':
        v->kind = JV_STRING;
        return parse_string(p, v->str, sizeof v->str);
    case '{':
    case '[':
        return skip_container(p, depth);
    case 't':
        v->kind = JV_BOOL;
        v->b = true;
        return parse_literal(p, "true");
    case 'f':
        v->kind = JV_BOOL;
        v->b = false;
        return parse_literal(p, "false");
    case 'n':
        return parse_literal(p, "null");
    default:
        v->kind = JV_NUMBER;
        return parse_number(p, &v->num);
    }
}

static long clamp(long v, long lo, long hi)
{
    return v < lo ? lo : v > hi ? hi : v;
}

/* DP24 "hhhhssssvvvv" – dokładnie 12 cyfr hex (małe lub wielkie litery); inaczej false i st bez zmian. */
static bool parse_hsv(const char *s, dev_state_t *st)
{
    unsigned part[3] = { 0, 0, 0 };
    if (strlen(s) != 12)
        return false;
    for (unsigned i = 0; i < 12; i++) {
        char c = s[i];
        unsigned d;
        if (c >= '0' && c <= '9')
            d = (unsigned)(c - '0');
        else if (c >= 'a' && c <= 'f')
            d = (unsigned)(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F')
            d = (unsigned)(c - 'A' + 10);
        else
            return false;
        part[i / 4] = part[i / 4] * 16u + d;
    }
    st->hue = (uint16_t)clamp((long)part[0], 0, 360);
    st->sat = (uint16_t)clamp((long)part[1], 0, 1000);
    st->val = (uint16_t)clamp((long)part[2], 0, 1000);
    return true;
}

/* Jeden DP; true, gdy rozpoznany dla typu. */
static bool apply_dp(const char *dp, const jvalue_t *v, dev_type_t type, dev_state_t *st)
{
    switch (type) {
    case DEV_TYPE_BLIND:
        if (strcmp(dp, "1") == 0 && v->kind == JV_STRING) {
            if (strcmp(v->str, "open") == 0 || strcmp(v->str, "close") == 0) {
                st->moving = true;
                st->opening = v->str[0] == 'o';
            }
            else if (strcmp(v->str, "stop") == 0)
                st->moving = false;
            else
                return false;
            return true;
        }
        if (strcmp(dp, "2") == 0 && v->kind == JV_NUMBER) {
            st->open_pct = (uint8_t)clamp(v->num, 0, 100);
            return true;
        }
        return false;
    case DEV_TYPE_LIGHT:
        if (strcmp(dp, "20") == 0 && v->kind == JV_BOOL) {
            st->on = v->b;
            return true;
        }
        if (strcmp(dp, "22") == 0 && v->kind == JV_NUMBER) {
            st->brightness = (uint16_t)clamp(v->num, 10, 1000);
            return true;
        }
        if (strcmp(dp, "23") == 0 && v->kind == JV_NUMBER) {
            st->temp = (uint16_t)clamp(v->num, 0, 1000);
            return true;
        }
        if (strcmp(dp, "21") == 0 && v->kind == JV_STRING) {
            st->colour = strcmp(v->str, "colour") == 0; /* "white", "scene", "music" -> nie kolor */
            return true;
        }
        if (strcmp(dp, "24") == 0 && v->kind == JV_STRING)
            return parse_hsv(v->str, st);
        return false;
    case DEV_TYPE_PLUG:
        if (strcmp(dp, "1") == 0 && v->kind == JV_BOOL) {
            st->on = v->b;
            return true;
        }
        if (strcmp(dp, "19") == 0 && v->kind == JV_NUMBER) {
            st->power_dw = (int32_t)clamp(v->num, 0, 100000000L);
            return true;
        }
        if (strcmp(dp, "18") == 0 && v->kind == JV_NUMBER) {
            st->current_ma = (int32_t)clamp(v->num, 0, 100000000L);
            return true;
        }
        if (strcmp(dp, "20") == 0 && v->kind == JV_NUMBER) {
            st->voltage_dv = (int32_t)clamp(v->num, 0, 100000000L);
            return true;
        }
        return false;
    }
    return false;
}

/* Obiekt "dps" – zmiany do tmp; *found = liczba rozpoznanych DP. */
static bool parse_dps(const char **p, dev_type_t type, dev_state_t *tmp, unsigned *found)
{
    if (**p != '{')
        return false;
    (*p)++;
    skip_ws(p);
    if (**p == '}') {
        (*p)++;
        return true;
    }
    for (;;) {
        char key[8];
        jvalue_t v;
        if (!parse_string(p, key, sizeof key))
            return false;
        skip_ws(p);
        if (**p != ':')
            return false;
        (*p)++;
        skip_ws(p);
        if (!parse_value(p, &v, 2))
            return false;
        if (apply_dp(key, &v, type, tmp))
            (*found)++;
        skip_ws(p);
        if (**p == ',') {
            (*p)++;
            skip_ws(p);
            continue;
        }
        if (**p != '}')
            return false;
        (*p)++;
        return true;
    }
}

bool tuya_dps_apply(const char *json, dev_type_t type, dev_state_t *st)
{
    const char *p = json;
    dev_state_t tmp = *st;
    unsigned found = 0;

    skip_ws(&p);
    if (*p != '{')
        return false;
    p++;
    skip_ws(&p);
    if (*p != '}') {
        for (;;) {
            char key[8];
            if (!parse_string(&p, key, sizeof key))
                return false;
            skip_ws(&p);
            if (*p != ':')
                return false;
            p++;
            skip_ws(&p);
            if (strcmp(key, "dps") == 0) {
                if (!parse_dps(&p, type, &tmp, &found))
                    return false;
            } else {
                jvalue_t v;
                if (!parse_value(&p, &v, 1))
                    return false;
            }
            skip_ws(&p);
            if (*p == ',') {
                p++;
                skip_ws(&p);
                continue;
            }
            if (*p != '}')
                return false;
            break;
        }
    }
    if (found == 0)
        return false;
    tmp.link = st->link;
    *st = tmp;
    return true;
}

dev_link_t tuya_link_after_fail(dev_link_t cur, unsigned consecutive_fails)
{
    if (cur == DEV_LINK_UNKNOWN && consecutive_fails < 2)
        return DEV_LINK_UNKNOWN;
    return DEV_LINK_OFFLINE;
}

int tuya_dps_light_level(char *buf, size_t cap, const dev_state_t *st, unsigned value)
{
    if (value < 10)
        value = 10;
    if (value > 1000)
        value = 1000;
    if (st->colour)
        return tuya_dps_colour(buf, cap, st->hue, st->sat, value);
    return tuya_dps_brightness(buf, cap, value);
}

int tuya_dps_palette(char *buf, size_t cap, const dev_state_t *st, unsigned hue)
{
    unsigned v = ui_light_level(st);
    return tuya_dps_colour(buf, cap, hue, 1000, v < 10 ? 10 : v);
}
