/*
 * Lista urządzeń panelu w kolejności kafelków (docs/SPEC.md §4.3, O5; spec: display-tiles).
 * Tylko nazwa, devId i typ – bez local_key (klucze trafią do konfiguracji poza gitem w E3).
 * Generowane przez tools/gen_device_table.py z tools/tuya_devices.json.
 */
#ifndef DEVICE_TABLE_H
#define DEVICE_TABLE_H

#include <stddef.h>

#include "ui_model.h"

extern const dev_info_t g_devices[];
extern const size_t g_device_count;

#endif /* DEVICE_TABLE_H */
