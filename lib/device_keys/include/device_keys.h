/*
 * Dane połączenia lokalnego Tuya dla urządzeń z g_devices (ta sama kolejność i liczba).
 * src/device_keys.c jest GENEROWANY przez tools/gen_device_table.py z tools/tuya_devices.json
 * i zawiera local_key – nie może trafić do gita (spec: tuya-control, docs/SPEC.md R-SYS-3).
 */
#ifndef DEVICE_KEYS_H
#define DEVICE_KEYS_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint32_t ip;      /* kolejność hosta (NET_IP4); 0 = nieznany – tylko wykrywanie UDP */
    char     key[17]; /* local_key, 16 znaków + '\0' */
} tuya_endpoint_t;

extern const tuya_endpoint_t g_device_endpoints[];
extern const size_t g_device_endpoint_count;

#endif /* DEVICE_KEYS_H */
