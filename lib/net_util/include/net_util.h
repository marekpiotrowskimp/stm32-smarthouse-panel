/*
 * Narzędzia sieciowe niezależne od sprzętu (docs/SPEC.md R-NET-1).
 * Kompilowane zarówno na hoście (testy), jak i na STM32.
 */
#ifndef NET_UTIL_H
#define NET_UTIL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Adres IPv4 w kolejności hosta: NET_IP4(192,168,0,1) == 0xC0A80001 */
#define NET_IP4(a, b, c, d) \
    (((uint32_t)(a) << 24) | ((uint32_t)(b) << 16) | ((uint32_t)(c) << 8) | (uint32_t)(d))

typedef struct {
    bool     link_up;   /* link Ethernet (PHY) */
    bool     has_ip;    /* adres przydzielony przez DHCP */
    uint32_t ip;        /* kolejność hosta */
    uint32_t netmask;
    uint32_t gw;
    uint8_t  mac[6];
} net_status_t;

/*
 * Unikalny MAC z 96-bitowego UID mikrokontrolera:
 *   02:80:E1:c2:c1:c0
 *   02 – adres lokalnie administrowany, unicast; 80:E1 – jak OUI STMicroelectronics
 *   c2..c0 – młodsze 24 bity CRC-32 (IEEE) z 12 bajtów UID (uid[0..2], little-endian)
 */
void net_mac_from_uid(const uint32_t uid[3], uint8_t mac[6]);

/*
 * Linia stanu sieci do logu VCP (semantyka snprintf: zawsze '\0' gdy cap > 0,
 * zwraca pełną długość linii bez '\0', nawet gdy bufor był za mały):
 *   "NET link=down mac=02:80:E1:A9:9B:8F"
 *   "NET link=up ip=none mac=02:80:E1:A9:9B:8F"
 *   "NET link=up ip=192.168.0.57/24 gw=192.168.0.1 mac=02:80:E1:A9:9B:8F"
 */
int net_format_status(char *buf, size_t cap, const net_status_t *st);

#endif /* NET_UTIL_H */
