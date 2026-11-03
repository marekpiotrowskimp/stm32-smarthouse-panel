#!/usr/bin/env python3
"""
Test 24 h panelu (docs/SPEC.md R-SYS-2, R-SYS-3; spec: system-reliability AC-13).

Nagrywanie logu z UART płytki ze znacznikami czasu (sekundy od startu nagrywania):
    tools/soak_check.py record /dev/cu.usbmodemXXXX soak.log 86400
Sprawdzenie nagrania:
    tools/soak_check.py check soak.log

Sprawdza: nagranie ≥ 24 h, brak restartu po pierwszym „Smarthouse boot”, każda linia HEAP kończy się „OK”,
odstęp linii HEAP (także ostatniej do końca nagrania) ≤ 10 min 30 s, w logu nie występuje żaden local_key
z tools/tuya_devices.json (wypisuje tylko nazwę urządzenia).
Kod wyjścia 0 = OK, 1 = błąd.
"""
import json
import re
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DEVICES = ROOT / "tools" / "tuya_devices.json"
HEAP_GAP_MAX_S = 10 * 60 + 30
MIN_DURATION_S = 24 * 3600 - 60  # 24 h (minuta zapasu na start nagrywania)
LINE_RE = re.compile(r"^\s*(\d+(?:\.\d+)?)\s(.*)$")


def record(port: str, out: str, seconds: float) -> None:
    import serial  # pyserial (penv PlatformIO)

    s = serial.Serial(port, 115200, timeout=0.2)
    t0 = time.time()
    buf = b""
    with open(out, "w", encoding="utf-8") as f:
        while time.time() - t0 < seconds:
            buf += s.read(512)
            while b"\n" in buf:
                line, buf = buf.split(b"\n", 1)
                f.write("%9.2f %s\n" % (time.time() - t0, line.decode("utf-8", "replace").rstrip()))
                f.flush()


def check(path: str) -> int:
    errors = []
    boots = 0
    heap_times = []
    keys = {}
    if DEVICES.exists():
        for d in json.loads(DEVICES.read_text(encoding="utf-8")):
            k = d.get("local_key")
            if k:
                keys[k] = d.get("name", "?").strip()
    leaked = set()
    last_t = 0.0
    for n, raw in enumerate(Path(path).read_text(encoding="utf-8", errors="replace").splitlines(), 1):
        m = LINE_RE.match(raw)
        t, line = (float(m.group(1)), m.group(2)) if m else (None, raw)
        if t is not None:
            last_t = t
        if "Smarthouse boot" in line:
            boots += 1
            if boots > 1:
                errors.append(f"linia {n}: restart ({line.strip()})")
        if line.startswith("RESET:") and boots > 1:
            errors.append(f"linia {n}: {line.strip()}")
        if line.startswith("HEAP "):
            if not line.rstrip().endswith(" OK"):
                errors.append(f"linia {n}: {line.strip()}")
            if t is not None:
                heap_times.append(t)
        for k, name in keys.items():
            if k in line:
                leaked.add(name)
    for a, b in zip(heap_times, heap_times[1:]):
        if b - a > HEAP_GAP_MAX_S:
            errors.append(f"przerwa w logu HEAP {b - a:.0f} s (od {a:.0f} s)")
    if not heap_times:
        errors.append("brak linii HEAP")
    elif last_t - heap_times[-1] > HEAP_GAP_MAX_S:  # zawieszenie bez restartu / zerwany UART pod koniec
        errors.append(f"brak linii HEAP przez ostatnie {last_t - heap_times[-1]:.0f} s nagrania")
    if last_t < MIN_DURATION_S:
        errors.append(f"nagranie krótsze niż {MIN_DURATION_S / 3600:.0f} h (R-SYS-2)")
    for name in sorted(leaked):
        errors.append(f"klucz urządzenia „{name}” w logu (R-SYS-3)")

    print(f"czas nagrania: {last_t / 3600:.1f} h, linie HEAP: {len(heap_times)}, starty: {boots}")
    for e in errors:
        print("BŁĄD:", e)
    print("OK" if not errors else f"{len(errors)} błędów")
    return 0 if not errors else 1


def main() -> int:
    if len(sys.argv) == 5 and sys.argv[1] == "record":
        record(sys.argv[2], sys.argv[3], float(sys.argv[4]))
        return 0
    if len(sys.argv) == 3 and sys.argv[1] == "check":
        return check(sys.argv[2])
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main())
