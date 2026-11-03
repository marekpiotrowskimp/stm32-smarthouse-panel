# stm32-smarthouse-panel

A wall-mounted control panel for **Tuya** smart-home devices on an **STM32F746G-DISCO** board — works **locally over Ethernet**, without the cloud.

## Features

- Device tiles (blinds, lights, sockets): tap = toggle, long press = details (brightness, color temperature, blind position)
- Virtual groups created on the screen (e.g. "all ground-floor blinds")
- FreeRTOS + lwIP + LVGL 9, local Tuya 3.3 protocol (AES-128)
- 3D-printed wall enclosure (OpenSCAD) — [enclosure/](enclosure/)

Specification (in Polish): [docs/SPEC.md](docs/SPEC.md).

## Device configuration

Tuya local keys are **not** in the repository:

```sh
cd tools && python3 -m venv .venv && source .venv/bin/activate && pip install tinytuya
python tuya_fetch_keys.py        # fetches the device list + local_key → tuya_devices.json (git-ignored)
python gen_device_table.py       # generates lib/device_table (names, IDs) and lib/device_keys (IPs + keys, git-ignored)
```

`lib/device_table/src/device_table.c` in the repository contains **sample** device IDs.

## Build and test

PlatformIO:

```sh
pio run -e disco_f746ng -t upload   # firmware (requires the generated lib/device_keys)
pio test -e native                  # unit tests on the host
pio test -e disco_f746ng            # hardware tests (test_board_*) on the board
```

## License

MIT — see [LICENSE](LICENSE).
