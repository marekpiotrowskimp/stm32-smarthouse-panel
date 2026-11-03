"""Generuje z tools/tuya_devices.json:
  lib/device_table/src/device_table.c – nazwa, devId i typ (do repozytorium, bez local_key),
  lib/device_keys/src/device_keys.c   – IP i local_key w tej samej kolejności (NIE do gita, R-SYS-3).

Kolejność kafelków: wg pomieszczeń (docs/SPEC.md O5); urządzenia spoza listy są pomijane
z ostrzeżeniem (np. czujniki bateryjne bez sterowania lokalnego).

Użycie: tools/.venv/bin/python tools/gen_device_table.py
"""
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SRC = ROOT / "tools" / "tuya_devices.json"
OUT = ROOT / "lib" / "device_table" / "src" / "device_table.c"
OUT_KEYS = ROOT / "lib" / "device_keys" / "src" / "device_keys.c"

ORDER = [
    "Salon roleta", "Salon s1", "Salon s2", "Salon s3", "Salon duże okno", "Lampka Salon",
    "Kuchnia", "Sypialnia", "Dziecko roleta", "Dziecko lampa", "Biuro roleta 2", "Biurko",
    "Pralnia", "Kotłownia", "Oświetlenie na zewnątrz",
]

# Kategoria Tuya -> typ panelu
CATEGORY_TYPE = {"clkg": "DEV_TYPE_BLIND", "tdq": "DEV_TYPE_BLIND", "dj": "DEV_TYPE_LIGHT", "cz": "DEV_TYPE_PLUG"}


def c_str(s: str) -> str:
    return json.dumps(s, ensure_ascii=False)


def c_ip(ip) -> str:
    """NET_IP4(a, b, c, d) albo 0, gdy IP nieznane (wykrywanie UDP 6667)."""
    if not ip:
        return "0"
    parts = [int(x) for x in ip.split(".")]
    if len(parts) != 4 or any(not 0 <= x <= 255 for x in parts):
        sys.exit(f"Błędny adres IP: {ip}")
    return "NET_IP4({}, {}, {}, {})".format(*parts)


def main() -> None:
    devices = {d["name"].strip(): d for d in json.loads(SRC.read_text())}

    missing = [n for n in ORDER if n not in devices]
    if missing:
        sys.exit(f"Brak urządzeń w {SRC.name}: {missing}")
    for name in sorted(set(devices) - set(ORDER)):
        print(f"pomijam: {name} ({devices[name]['category']})", file=sys.stderr)

    rows = []
    key_rows = []
    # kafelki: rolety, światła, gniazdka; w obrębie typu kolejność pomieszczeń z ORDER (sortowanie stabilne)
    type_rank = {"DEV_TYPE_BLIND": 0, "DEV_TYPE_LIGHT": 1, "DEV_TYPE_PLUG": 2}
    for name in sorted(ORDER, key=lambda n: type_rank.get(CATEGORY_TYPE.get(devices[n]["category"]), 3)):
        d = devices[name]
        dev_type = CATEGORY_TYPE.get(d["category"])
        if dev_type is None:
            sys.exit(f"Nieobsługiwana kategoria {d['category']} dla {name}")
        rows.append(f"    {{ {c_str(name)}, {c_str(d['id'])}, {dev_type} }},")
        key = d["local_key"]
        if len(key) != 16 or not key.isprintable() or not key.isascii():
            sys.exit(f"Nieprawidłowy local_key dla {name}")
        key_rows.append(f"    {{ {c_ip(d.get('ip'))}, {c_str(key)} }}, /* {name} */")

    OUT.write_text(
        "/* Wygenerowane przez tools/gen_device_table.py – nie edytować ręcznie. */\n"
        '#include "device_table.h"\n\n'
        "const dev_info_t g_devices[] = {\n" + "\n".join(rows) + "\n};\n\n"
        "const size_t g_device_count = sizeof g_devices / sizeof g_devices[0];\n"
    )
    OUT_KEYS.write_text(
        "/* Wygenerowane przez tools/gen_device_table.py – nie edytować ręcznie. ZAWIERA local_key – nie do gita. */\n"
        '#include "device_keys.h"\n'
        '#include "net_util.h"\n\n'
        "const tuya_endpoint_t g_device_endpoints[] = {\n" + "\n".join(key_rows) + "\n};\n\n"
        "const size_t g_device_endpoint_count = sizeof g_device_endpoints / sizeof g_device_endpoints[0];\n"
    )
    print(f"Zapisano {len(rows)} urządzeń do {OUT.relative_to(ROOT)} i {OUT_KEYS.relative_to(ROOT)}")


if __name__ == "__main__":
    main()
