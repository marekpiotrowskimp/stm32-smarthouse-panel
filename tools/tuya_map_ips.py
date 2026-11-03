"""Przypisuje urządzeniom z tuya_devices.json adresy LAN: nasłuch UDP + próba kluczy na znanych IP."""
import json
import sys
from pathlib import Path

import tinytuya

DEV_FILE = Path(__file__).parent / "tuya_devices.json"
devices = json.loads(DEV_FILE.read_text())
by_id = {d["id"]: d for d in devices}

# 1) wykrywanie przez broadcast UDP (tylko ta sama podsieć)
found = tinytuya.deviceScan(verbose=False, maxretry=8)
for ip, info in found.items():
    if info.get("gwId") in by_id:
        d = by_id[info["gwId"]]
        d["ip"], d["version"] = ip, info.get("version", "3.3")

# 2) adresy spoza podsieci (podane w argumentach) - próbujemy kluczy pozostałych urządzeń
for ip in sys.argv[1:]:
    if any(d.get("ip") == ip for d in devices):
        continue
    for d in devices:
        if d.get("ip") or d["sub"]:
            continue
        dev = tinytuya.Device(d["id"], ip, d["local_key"], version=3.3, connection_timeout=2, connection_retry_limit=1)
        st = dev.status()
        dev.close()
        if st and "dps" in st:
            d["ip"], d["version"] = ip, "3.3"
            break

DEV_FILE.write_text(json.dumps(devices, indent=2, ensure_ascii=False))
for d in devices:
    print(f"{d['name'][:26]:26} {d.get('ip', '-'):15} online={d['online']}")
