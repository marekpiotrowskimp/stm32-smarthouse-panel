# stm32-smarthouse-panel

Ścienny panel sterowania urządzeniami **Tuya** na płytce **STM32F746G-DISCO** — działa **lokalnie przez Ethernet**, bez chmury.

- kafelki urządzeń (rolety, oświetlenie, gniazdka), dotknięcie = przełączenie, przytrzymanie = szczegóły (jasność, barwa, pozycja rolety)
- grupy wirtualne tworzone na ekranie
- FreeRTOS + lwIP + LVGL 9, lokalny protokół Tuya 3.3 (AES-128)
- obudowa naścienna drukowana w 3D (OpenSCAD) — [enclosure/](enclosure/)

Specyfikacja: [docs/SPEC.md](docs/SPEC.md).

## Konfiguracja urządzeń

Klucze lokalne Tuya **nie są** w repozytorium:

```sh
cd tools && python3 -m venv .venv && source .venv/bin/activate && pip install tinytuya
python tuya_fetch_keys.py        # pobiera listę urządzeń + local_key → tuya_devices.json (w .gitignore)
python gen_device_table.py       # generuje lib/device_table (nazwy, ID) i lib/device_keys (IP + klucze, w .gitignore)
```

`lib/device_table/src/device_table.c` w repozytorium zawiera **przykładowe** ID urządzeń.

## Build

PlatformIO: `pio run -t upload`, testy: `pio test -e native`.
