# Furnace controller firmware

ESP32 (ESP-IDF 5.5, FreeRTOS) firmware for an electric composite-curing furnace (program targets 20–170 °C, 15 kW and 25 kW units). It:

- reads five Pt100 RTDs through MS9024 transmitters on Modbus RTU / RS-485;
- runs multi-stage heating programs (ramp, hold, cool, cooldown) with PID plus feedforward control of a time-proportioned SSR and a contactor;
- drives a fan;
- provides a Nextion touch-panel HMI.

This firmware can energize a furnace. Read [AGENTS.md](AGENTS.md) before changing it. Do not flash or energize hardware without authorization.

## Documentation

| Start here | |
| --- | --- |
| [export/furnace-zephyr-starter/README.md](export/furnace-zephyr-starter/README.md) | **Self-contained starter kit for the new Zephyr controller project** (rules, decisions, deferred topics, requirements, lessons, setup) |
| [docs/handoff/README.md](docs/handoff/README.md) | Full functional mapping, method of operation, control math, Modbus/MS9024 register work, configuration catalog, known defects and mistakes, and Zephyr/TDD rewrite guidance |
| [docs/codex/REPOSITORY_MAP.md](docs/codex/REPOSITORY_MAP.md) | Navigation map of the current source (tasks, queues, modules, flows) |
| [docs/analysis/FIRMWARE_FINDINGS.md](docs/analysis/FIRMWARE_FINDINGS.md) | Findings register (F-001 … F-080) |
| [docs/codex/ENGINEERING_STANDARDS.md](docs/codex/ENGINEERING_STANDARDS.md) | Safety and concurrency rules |
| [docs/decisions/](docs/decisions/) | Architecture decision records |

## Build

```sh
idf.py build
idf.py -p PORT flash monitor
```

**Configuration warning:** the tracked `sdkconfig.defaults` is **not** the field-tuned configuration. It also selects a single-app partition table without the LittleFS `storage` partition used for crash logs. See [docs/handoff/05-CONFIGURATION-CATALOG.md](docs/handoff/05-CONFIGURATION-CATALOG.md) (finding F-067) before building an image for a real furnace.

No automated test suite exists yet.
