# 09 — Rewrite guidance: Zephyr, test-driven, no hosted HMI

This is guidance, not a design decision. Items marked **decide** need an owner. Decisions taken on 2026-10-13 and topics deliberately postponed are in [10-DECISIONS-AND-DEFERRED-TOPICS.md](10-DECISIONS-AND-DEFERRED-TOPICS.md). **When work reaches a deferred topic, remind the user and ask before designing it.** Zephyr facility names should be checked against the Zephyr release the new project pins.

## 1. Scope of the new controller

Same responsibilities as today ([01](01-SYSTEM-OVERVIEW.md), [02](02-FUNCTIONAL-REQUIREMENTS.md)):

- multi-sensor RS-485 temperature acquisition with validity and freshness;
- program execution (ramp / hold / cool / cooldown);
- PID with feedforward;
- time-proportioned SSR plus contactor;
- fan;
- fault guards;
- persistence (settings, operating hours, programs);
- diagnostics and a service console.

Not hosted: the Nextion panel. Instead the controller exposes a versioned API that any HMI uses ([07](07-HMI-CONTRACT-NEXTION.md) §5). Precision changes are not yet specified (§6).

## 2. Architecture rules learned from the current firmware

| # | Rule | Learned from |
| --- | --- | --- |
| R1 | The control/domain core is pure C with no RTOS calls (program, validation, profile, ramp planner, PID, FF, guards, sensor aggregation, MS9024 codec). Instances, not singletons. Tested on host first. | F-039, F-052, F-020, F-076 |
| R2 | One owner thread for run state (an explicit FSM). Everything else sends it messages. No shared plain flags. | F-013, F-021, F-072 |
| R3 | The actuator owns SSR and contactor. Authorization and GPIO write are one critical section. Every inhibit change wakes the actuator. Demand is accepted only with the current run-epoch token. | F-003, F-063, F-078 |
| R4 | Safety paths (inhibit, supervisor, watchdog response) do GPIO and atomics only. No logging before the action, no queue waits, no event publishing inside them. | F-062, F-074 |
| R5 | Telemetry is non-blocking and droppable. Commands and state transitions use a reliable channel with defined overflow behavior. Never publish into a channel from its own consumer. Do not publish what nobody consumes. | F-002, F-014, F-055, F-062 |
| R6 | Temperature is a record (value, monotonic timestamp, status, contributing sensors), published atomically. Validity and freshness travel with the value to every consumer, including the UI. | F-001, F-054, F-059, F-069, F-071 |
| R7 | An always-on supervisor enforces an absolute over-temperature limit and sensor-freshness loss in every state (idle, paused, running) and opens the contactor directly. | F-057, F-077 |
| R8 | Terminal outcomes are distinct (COMPLETED, STOPPED, ABORTED(cause)); every fault reaches the operator with a cause. A fault manager owns latching and acknowledgement. | F-035, F-058, ADR-0001 |
| R9 | Stage kind is explicit in the program (RAMP / HOLD / COOL). "Already at target" is handled deliberately. Manual mode is its own mode. | F-064, F-080 |
| R10 | One ramp planner for every entry path; its rate bound is a tested property. | F-048, F-065 |
| R11 | Every thread is registered with the task watchdog from day one. The watchdog's consequence is outputs off, then reset. | F-036, F-047 |
| R12 | Static threads that never stop (no runtime teardown) unless a lifecycle is required and tested. | F-006…F-012, F-055, F-056 |
| R13 | One tracked configuration per board/furnace variant; config identity printed at boot and stored in crash records. Devicetree encodes pins **and polarity**; a build-time check rejects pin collisions. | F-016, F-042, F-049, F-067 |
| R14 | Persistence has versioned schemas and explicit migrations. Service hours are never erased implicitly. | F-050, F-073 |
| R15 | Crash records: reset cause, uptime, wall time if available, firmware/config ID, last N log lines; tested by forcing each reset cause. | F-068 |
| R16 | Commissioning tools are read-only by default. Device writes are explicit, per-unit and verified against that unit's baseline. | F-066 |
| R17 | Warnings as errors, format checking, static analysis, TSan/ASan on host tests, all in CI. | F-079 |
| R18 | Docs that describe behavior are generated or checked against code where possible (channel/thread inventory, config table). | F-075 |
| R19 | A sensor reading that differs drastically from the others is excluded from the aggregate and reported; the exclusion and re-inclusion rules are explicit and tested. | D-07, T-05, F-004 |
| R20 | During COOLING no guard ends the program; a temperature that is not falling raises an alarm. | D-06, T-04, F-057 |
| R21 | Keep a decisions/deferred-topics register with a remind-and-ask rule in the new project's agent instructions, enforced by its verification script. | 10, 08 §5 |

## 3. Suggested module decomposition

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
platform/       Zephyr adapters
  actuator/     SSR + contactor driver, inhibit reasons, epoch token
  sensor_bus/   Modbus RTU client owner thread
  storage/      settings (hours, prefs), program store
  supervisor/   absolute over-temperature, freshness watchdog, task_wdt integration
  diag/         logging policy, crash record, shell commands, bus scan/dump (read-only)
tests/          unit (host), integration (native_sim + emulators), plant-model simulation
```

## 4. Current mechanism → Zephyr facility

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

## 5. Test strategy and first backlog

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
| 1 | MS9024 codec golden vectors: CDAB order, `0x80000000`, NaN, Inf, range edges | TEMP-02 |
| 2 | Aggregation: fresh/stale per sensor, quorum boundary, compaction, warning-only disagreement | TEMP-03…05, F-005 |
| 3 | Ramp planner properties: monotonic, endpoints, rate ≤ programmed on every entry path, band = 0, band ≥ span | PROF-02/03, F-048, F-065 |
| 4 | Stage FSM table: explicit kinds, warm start, near-target ramp, hold below target, cooling, cooldown completion | PROF-01…07, F-080 |
| 5 | Guards: overshoot not tripping in COOLING; absolute limit in all states; sustained-lag stall; hold deviation; replay of field traces without nuisance trips | GUARD-*, F-057, F-077 |
| 6 | PID and FF: P/I/D terms, anti-windup both directions, derivative-on-measurement on a setpoint step, both reset variants, non-finite handling | CTRL-01…04, F-053 |
| 7 | SSR window planner: duty to on/off ms at tick resolution; inhibit mid-window; demand-step latency | CTRL-05, F-078 |
| 8 | Actuator: inhibit reasons independent; atomic authorize+write under a forced interleaving; stale-epoch demand rejected; every inhibit drives SSR **and** contactor off with the publish path blocked | SAFE-02…04, F-003, F-062, F-063 |
| 9 | Run FSM: model-based random command/sensor-event sequences; invariants (no inhibit release outside RUNNING; profile time advances only while authorized; resume refused under a sensor inhibit) | RUN-*, F-060, F-061, F-072 |
| 10 | Operator events: abort cause reported, sensor loss reported, never COMPLETED for an abort | SAFE-09, F-058, F-059 |
| 11 | Manual mode holds until stop; live retarget at the requested rate | MAN-01/02, F-064, F-065 |
| 12 | Persistence: hours counter cadence, survives factory reset and schema change; `.prg` importer | PERS-*, F-073 |
| 13 | Crash record per forced reset cause | DIAG-02, F-068 |
| 14 | Closed-loop plant simulation: 80 °C hold arrival within field-accepted bounds (peak +1.5 to +2 °C, settle ±0.7 °C) with the field-tuned gains and FF | CTRL, 03 §7 |
| 15 | Config: build matrix per variant; partition table includes storage; pin-collision check | F-067, F-016 |

## 6. Precision decisions (Q-07)

| Quantity | Today | Questions |
| --- | --- | --- |
| Program target | 1 °C | 0.1 °C? |
| Ramp rate | 0.1 °C/min | finer? |
| Stage time | 1 min | seconds? |
| Phase/cooling tolerance | 2 °C | tighter, or replace by explicit stage kinds (R9) |
| Handover tolerance | 0.5 °C (0.1 °C steps) | |
| Guard thresholds | 1 °C steps | |
| Sensor | float32; instrument resolution/accuracy unknown | Spec the MS9024/Pt100 accuracy chain; filter (reg 26 FIN = 50) |
| Aggregate | arithmetic mean | median/trimmed mean? weighted by position? |
| PID period | 1 s | |
| Actuator | 5 s window, 10 ms resolution (0.2 % duty), sampled once per window | shorter window? mid-window update? zero-cross SSR minimum on-time? |
| Time base | 10 ms tick, `uint32_t` ms (49.7-day wrap) | 64-bit uptime (`k_uptime_get`) |
| Gains | 0.001 steps | float config? |

## 7. Migration and compatibility

- **Program library:** existing `.prg` text files on the panel SD (format in [07](07-HMI-CONTRACT-NEXTION.md) §3). Decide whether to import them.
- **Operating hours:** read the existing `op_time_s` from the ESP NVS `user_prefs` namespace if boards are reused, or record them manually at changeover.
- **Tuning:** carry the per-furnace values from [03](03-CONTROL-ALGORITHMS.md) §7 into per-variant config. Re-validate, because the actuator scheme may change (F-078).
- **Sensors:** carry MS9024 addresses 1–5 and the healthy-unit baseline; record a baseline per installed unit at commissioning.

## 8. Open decisions

The original questions Q-01…Q-09 were answered or deferred on 2026-10-13. The live list is the T-entries in [10-DECISIONS-AND-DEFERRED-TOPICS.md](10-DECISIONS-AND-DEFERRED-TOPICS.md): T-01 status/notifications, T-02 protocol, T-03 PID storage, T-04 cooling guard, T-05 sensor faults/loss, T-06 manual mode, T-07 sensor hardware, T-08 precision, T-09 fan RPM/failure, T-10 absolute limit, T-11 completion, T-12 MCU and 16 MB layout, T-13 data migration, T-14 actuation scheme, T-15 output polarity/reset. None of them has to be answered up front; each is asked when work reaches it.
