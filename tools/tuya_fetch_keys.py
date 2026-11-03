"""Pobiera listę urządzeń Tuya wraz z local_key przez autoryzację QR (Tuya Smart / Smart Life).

Użycie: tools/.venv/bin/python tools/tuya_fetch_keys.py <USER_CODE>
Kod QR zapisuje się do tools/tuya_qr.png - zeskanuj go w aplikacji Tuya Smart.
Wynik: tools/tuya_devices.json (zawiera klucze - nie commitować).
"""
import json
import sys
import time
from pathlib import Path

import qrcode
from tuya_sharing import LoginControl, Manager

CLIENT_ID = "HA_3y9q4ak7g4ephrvke"  # publiczny client id integracji Home Assistant
SCHEMA = "haauthorize"
OUT_DIR = Path(__file__).parent


def main(user_code: str) -> None:
    login = LoginControl()
    resp = login.qr_code(CLIENT_ID, SCHEMA, user_code)
    if not resp.get("success"):
        sys.exit(f"Błąd generowania QR: {resp}")
    qr_token = resp["result"]["qrcode"]

    qr_path = OUT_DIR / "tuya_qr.png"
    qrcode.make(f"tuyaSmart--qrLogin?token={qr_token}").save(qr_path)
    print(f"QR zapisany: {qr_path} - zeskanuj w aplikacji Tuya Smart", flush=True)

    deadline = time.time() + 300
    while time.time() < deadline:
        ok, info = login.login_result(qr_token, CLIENT_ID, user_code)
        if ok:
            break
        time.sleep(3)
    else:
        sys.exit("Timeout - QR nie został zeskanowany w ciągu 5 minut")
    qr_path.unlink(missing_ok=True)

    manager = Manager(CLIENT_ID, user_code, info["terminal_id"], info["endpoint"], info)
    manager.update_device_cache()

    devices = []
    for d in manager.device_map.values():
        devices.append({
            "name": d.name,
            "id": d.id,
            "local_key": d.local_key,
            "category": d.category,
            "product_id": d.product_id,
            "product_name": d.product_name,
            "online": d.online,
            "sub": d.sub,
            "status": d.status,
            "dps": {
                dp: {"code": s.get("status_code"), "type": s.get("config_item", {}).get("valueType"),
                     "values": s.get("config_item", {}).get("valueDesc")}
                for dp, s in (d.local_strategy or {}).items()
            },
        })

    out = OUT_DIR / "tuya_devices.json"
    out.write_text(json.dumps(devices, indent=2, ensure_ascii=False))
    print(f"Zapisano {len(devices)} urządzeń do {out}")


if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    main(sys.argv[1])
