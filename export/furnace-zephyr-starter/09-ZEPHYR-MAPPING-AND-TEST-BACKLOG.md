# 09 — Zephyr mapping, module structure and test backlog

This is guidance, not a design decision. The structure below is a suggestion for the new project's first architecture discussion. Zephyr facility names must be checked against the release the new project pins. Hardware-dependent parts follow the integration-pending rule ([01](01-RULES.md) §3): interface and fake first, marker and fail-safe stub where the facts are unknown.

## 1. Suggested module decomposition

```
app/            main, thread wiring, devicetree-driven instantiation
api/            versioned controller API (commands, state, telemetry, faults) + HMI transport adapter(s)
core/           pure C, host-tested
  program/      model, validation, serialization (+ .prg importer)
  profile/      stage FSM, ramp planner (linear + ease-out), duration estimator
  control/      pid (instance), feedforward model, ssr window planner
  guards/       overshoot (phase-aware), stall (sustained lag), hold deviation, absolute limit
  sensing/      freshness, quorum, aggregation, ms9024 codec
  run/          run-state FSM (IDLE, RUNNING{phase}, PAUSED{reasons}, ABORTED{cause}, COMPLETED, STOPPED, MANUAL)
platform/       Zephyr adapters (hardware-specific parts: TODO(INTEGRATION T-xx) until supplied, D-14)
  actuator/     SSR + contactor driver, inhibit reasons, epoch token
  sensor_bus/   Modbus RTU client owner thread
  storage/      settings (hours, prefs), program store
  supervisor/   absolute over-temperature, freshness watchdog, task_wdt integration
  diag/         logging policy, crash record, shell commands, bus scan/dump (read-only)
tests/          unit (host), integration (native_sim + emulators), plant-model simulation
```

## 2. Legacy mechanism → Zephyr facility

| Current (ESP-IDF/FreeRTOS) | Zephyr candidate | Note |
| --- | --- | --- |
| FreeRTOS tasks + task notifications | `k_thread` (static `K_THREAD_DEFINE`), `k_sem` / `k_event` / `k_poll` | Static threads, R12 |
| Private `esp_event` loop | zbus channels (typed) or per-consumer `k_msgq` | zbus listeners run in the publisher's context; use subscribers or msgq for anything non-trivial. Keep safety paths off the bus (R4) |
| Command dispatcher queue | Run-FSM-owned `k_msgq` with explicit overflow policy | No re-entrant execution (F-002) |
| `esp_timer` (PID tick, expiry) | `k_timer` (expiry runs in ISR context) + `k_work` / thread wake | Use ISR-safe work in the expiry (GPIO set + atomic) or wake the supervisor thread |
| `gpio_master_driver` + Kconfig pins | Devicetree `gpios` with `GPIO_ACTIVE_HIGH/LOW`, `gpio_dt_spec`, `gpio_pin_set_dt` | Polarity in devicetree (R13) |
| esp-modbus master | Zephyr `modbus` subsystem, RTU client, `zephyr,modbus-serial` node with `de-gpios` | Verify half-duplex DE timing on the chosen SoC |
| NVS (`user_prefs`, `prog_registry`) | Settings subsystem over NVS or ZMS | Versioned keys (R14); migration from the ESP NVS layout if boards are reused |
| LittleFS crash dumps | `CONFIG_FILE_SYSTEM_LITTLEFS` and/or `CONFIG_DEBUG_COREDUMP` (flash backend), `retention`/`retained_mem`, `hwinfo_get_reset_cause()` | R15 |
| Async logger | Zephyr logging, deferred mode, overflow drop | R4/R5 |
| `esp_console` | Zephyr shell (`log-ls`, `log-file`, sensor dump, status) | |
| Health monitor (disabled) | `task_wdt` subsystem with hardware watchdog fallback | R11 |
| `sdkconfig` / Kconfig | `prj.conf` + board/variant overlays + project Kconfig | One file per variant (R13) |
| ESP32 target | Zephyr supports ESP32 via the Espressif HAL | New board has **16 MB flash** (D-04); MCU and partition plan in T-12 |

## 3. Test strategy and first backlog

Layers:

1. **Host unit tests** for `core/`, using ztest on `native_sim` or plain host ctest.
2. **Integration** on `native_sim`: FFF fakes and the GPIO emulator for the actuator, UART emulation for Modbus frames.
3. **Plant-model simulation**: a first-order-plus-dead-time thermal model fitted to field data, for closed-loop regression of the tuning results.
4. **Target unit tests** via twister on hardware.
5. **Device bench, powered controller, powered furnace**, with written plans (keep the existing plan template).

Field data available for plant fitting (field-reported, 15 kW unit):

- ~8 % steady duty at 80 °C;
- 4–5 °C coast after heater-off;
- ramp lag ~0.8 °C at ~3 °C/min;
- ~0.05 °C/s rise rate.

Collect raw logs for the 25 kW unit and for higher hold temperatures before relying on the model there.

First tests, in order (each names its requirement / finding):

| # | Test | REQ / F |
| --- | --- | --- |
| 1 | Sample acceptance against the `sensor_source` interface with fakes: transport error, non-finite, "no measurement" status, range edges (the real codec waits for T-07) | TEMP-02, D-10 |
| 2 | Aggregation: fresh/stale per sensor, quorum boundary, outlier exclusion and re-inclusion (thresholds provisional until T-05) | TEMP-03…05, D-07, F-004 |
| 3 | Ramp planner properties: monotonic, endpoints, rate ≤ programmed on every entry path, band = 0, band ≥ span | PROF-01, MAN-02, F-048, F-065 |
| 4 | Stage FSM table: explicit kinds, warm start, near-target ramp, hold below target, cooling, cooldown completion | PROG-02, PROF-01…05, F-080 |
| 5 | Guards: no guard ends a COOL stage, not-falling alarm (D-06, parameters T-04); absolute limit in all states (T-10); sustained-lag stall; hold deviation; replay of field traces without nuisance trips | GUARD-*, F-057, F-077 |
| 6 | PID and FF: P/I/D terms, anti-windup both directions, derivative-on-measurement on a setpoint step, both reset variants, non-finite handling | CTRL-01…03, F-053 |
| 7 | SSR window planner: duty to on/off ms at tick resolution; inhibit mid-window; demand-step latency | CTRL-04, T-14, F-078 |
| 8 | Actuator: inhibit reasons independent; atomic authorize+write under a forced interleaving; stale-epoch demand rejected; every inhibit drives SSR **and** contactor off with the publish path blocked | SAFE-02…04, F-003, F-062, F-063 |
| 9 | Run FSM: model-based random command/sensor-event sequences; invariants (no inhibit release outside RUNNING; profile time advances only while authorized; resume refused under a sensor inhibit) | RUN-*, F-060, F-061, F-072 |
| 10 | Operator events: abort cause reported, sensor loss reported, never COMPLETED for an abort | SAFE-09, F-058, F-059 |
| 11 | Manual mode holds until stop; live retarget at the requested rate | MAN-01/02, D-09, F-064, F-065 |
| 12 | Persistence: hours counter cadence, survives factory reset and schema change; `.prg` importer | PERS-*, PROG-05, T-13, F-073 |
| 13 | Crash record per forced reset cause | DIAG-02, F-068 |
| 14 | Closed-loop plant simulation: 80 °C hold arrival within field-accepted bounds (peak +1.5 to +2 °C, settle ±0.7 °C) with the field-tuned gains and FF | CTRL-*, 04 §7 |
| 15 | Config: build matrix per variant; production build rejects placeholders and enabled integration markers; pin-collision check (once the board is known, T-12) | R13, D-14, F-067, F-016 |

## 4. Migration and compatibility (all deferred: T-13)

- **Program library:** existing `.prg` text files on the panel SD (format in [03](03-LEGACY-SYSTEM-BEHAVIOUR.md) §6.3). Whether to import them is T-13.
- **Operating hours:** read the existing `op_time_s` from the ESP NVS `user_prefs` namespace if boards are reused, or record them manually at changeover.
- **Tuning:** the field values are a note only ([04](04-CONTROL-ALGORITHMS.md) §7, D-15); the new loop is re-tuned and validated. Storage form: T-03.
- **Sensors:** likely new custom sensors (D-10, T-07); record a per-unit baseline at commissioning.

