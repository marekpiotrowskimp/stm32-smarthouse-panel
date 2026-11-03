/* Narzędzia sieciowe niezależne od sprzętu (docs/SPEC.md R-NET-1). */
#include "net_util.h"

#include <stdio.h>

/* CRC-32 IEEE 802.3 (jak zlib.crc32) */
static uint32_t crc32_ieee(const uint8_t *data, size_t len)
{
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int b = 0; b < 8; b++)
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}

void net_mac_from_uid(const uint32_t uid[3], uint8_t mac[6])
{
    uint8_t bytes[12];
    for (unsigned w = 0; w < 3; w++)
        for (unsigned b = 0; b < 4; b++)
            bytes[w * 4 + b] = (uint8_t)(uid[w] >> (8 * b)); /* little-endian, jak w pamięci MCU */

    uint32_t crc = crc32_ieee(bytes, sizeof bytes);
    mac[0] = 0x02; /* lokalnie administrowany, unicast */
    mac[1] = 0x80;
    mac[2] = 0xE1;
    mac[3] = (uint8_t)(crc >> 16);
    mac[4] = (uint8_t)(crc >> 8);
    mac[5] = (uint8_t)crc;
}

static unsigned prefix_len(uint32_t netmask)
{
    unsigned n = 0;
    while (netmask & 0x80000000u) {
        n++;
        netmask <<= 1;
    }
    return n;
}

int net_format_status(char *buf, size_t cap, const net_status_t *st)
{
    char mac[18];
    snprintf(mac, sizeof mac, "%02X:%02X:%02X:%02X:%02X:%02X",
             st->mac[0], st->mac[1], st->mac[2], st->mac[3], st->mac[4], st->mac[5]);

    if (!st->link_up)
        return snprintf(buf, cap, "NET link=down mac=%s", mac);
    if (!st->has_ip)
        return snprintf(buf, cap, "NET link=up ip=none mac=%s", mac);

    return snprintf(buf, cap, "NET link=up ip=%u.%u.%u.%u/%u gw=%u.%u.%u.%u mac=%s",
                    (unsigned)(st->ip >> 24), (unsigned)(st->ip >> 16) & 0xFFu,
                    (unsigned)(st->ip >> 8) & 0xFFu, (unsigned)st->ip & 0xFFu,
                    prefix_len(st->netmask),
                    (unsigned)(st->gw >> 24), (unsigned)(st->gw >> 16) & 0xFFu,
                    (unsigned)(st->gw >> 8) & 0xFFu, (unsigned)st->gw & 0xFFu,
                    mac);
}
