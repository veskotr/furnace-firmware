# Furnace firmware

ESP-IDF/FreeRTOS firmware for an ESP32-based furnace controller.  It includes
heater/SSR control, Modbus temperature acquisition, a Nextion HMI, fan control,
event/command dispatching, and persistent logging.

This repository controls equipment that can energize heating hardware. A
successful build is not evidence of safe wiring, GPIO polarity, sensor
freshness, interlocks, or de-energization timing. Do not flash, connect, or
operate a furnace without an approved hardware-validation procedure.

## Start here

- [Repository map](docs/codex/REPOSITORY_MAP.md) — startup order, tasks,
  module ownership, and end-to-end flows.
- [Engineering standards](docs/codex/ENGINEERING_STANDARDS.md) — safety and
  concurrency requirements for changes.
- [Firmware findings](docs/analysis/FIRMWARE_FINDINGS.md) — source-backed
  defects, corrections, residual risks, and validation status.
- [Repository audit plan](docs/analysis/REPOSITORY_AUDIT_PLAN.md) — ongoing
  mapping and verification work.
- [Revision-two system architecture](docs/architecture/BOARD-REV2-SYSTEM-ARCHITECTURE.md)
  — accepted target boundaries and explicitly unresolved product policies.
- [Revision-two component standard](docs/architecture/COMPONENT-CONTRACT-STANDARD.md)
  — ownership, lifecycle, asynchronous API, and snapshot rules for the later
  rewrite.

## Requirements

The tracked resolved configuration was generated with ESP-IDF 5.5.4 for the
ESP32. `main/idf_component.yml` declares the LittleFS and ESP-Modbus managed
dependencies. Use an initialized ESP-IDF environment and let the component
manager resolve dependencies before building.

## Build

From an ESP-IDF shell:

```sh
idf.py build
```

For the recorded local ESP-IDF 5.5.4 fallback when `idf.py`'s Python
environment is inconsistent:

```sh
IDF_PATH=/home/vesko/.espressif/v5.5.4/esp-idf cmake -S . -B build -G Ninja \
  -DPYTHON=/home/vesko/.espressif/tools/python/v5.5.4/venv/bin/python3 \
  -DPYTHON_DEPS_CHECKED=1
IDF_PATH=/home/vesko/.espressif/v5.5.4/esp-idf cmake --build build -j2
```

Flashing and monitoring require explicit authorization and a board-specific
validation plan:

```sh
idf.py -p PORT flash monitor
```

## Configuration notes

`sdkconfig.defaults` currently mirrors one complete resolved `sdkconfig`:
ESP32, 2 MiB flash, Nextion HMI enabled, and the run indicator/PID optional
features disabled. The root build excludes the legacy SPI/MAX31865 temperature
monitor from production discovery.

There is a documented partition/storage mismatch in the tracked configuration:
the selected ESP-IDF single-app table lacks the `storage` LittleFS partition
that the logger requests. See [AUDIT-008](docs/analysis/AUDIT-008-configuration-build.md)
before treating the configuration as deployment-ready.

## Repository checks

After changing the repository's Codex setup, run:

```sh
python3 tools/codex/verify_repository_setup.py
```

Use source and actual command output as authority over this overview.
