# Firmware findings register

Date: 2026-07-13. Scope: read-only static architecture, safety, concurrency, lifecycle, integration, control, persistence, and configuration analysis. No hardware was operated. Line numbers are navigation hints; symbols/current source are authoritative.

Categories: **confirmed defect** has a reachable source-level failure; **highly likely defect** has strong evidence with one runtime/config dependency; **possible defect** needs more evidence; **design weakness**, **maintainability issue**, **missing test**, and **unresolved question** are not claimed defects.

## Priority summary

| Rank | ID | Category | Risk | Summary |
| ---: | --- | --- | --- | --- |
| 1 | F-016 | confirmed defect | critical safety/configuration | Run indicator and contactor both drive GPIO22 in current/default config |
| 2 | F-017 | confirmed defect | critical conditional safety | SSR-off failure reports an unconsumed event but does not drop contactor |
| 3 | F-001 | confirmed defect | critical control safety | Boot-zero or indefinitely stale temperature is accepted for control |
| 4 | F-002 | confirmed defect | critical safety/concurrency | Dispatcher can deadlock before queued heater-off commands execute |
| 5 | F-003 | confirmed defect | critical safety/concurrency | In-flight PID output can restore power after pause |
| 6 | F-004 | confirmed defect | critical control safety | An anomalous sensor batch is still published as valid control input |
| 7 | F-018 | highly likely defect | high reset safety | Restart/factory-reset path does not first synchronously inhibit outputs |
| 8 | F-019 | confirmed defect | high startup/control | Profile start queues contactor START before control task creation is proven |
| 9 | F-048 | confirmed defect | high control | Cubic soft-landing accelerates setpoint before decelerating |
| 10 | F-053 | confirmed defect | high conditional control safety | Non-finite PID input can propagate to heater demand |
| 11 | F-021 | confirmed defect | high concurrency/control | Coordinator temperature is a plain cross-task data race |
| 12 | F-006–F-009, F-012 | confirmed defects | high lifecycle | Multiple shutdown paths destroy state without joining workers/callbacks |
| 13 | F-022 | confirmed defect | high persistence/control | Partial Nextion file read is returned as complete and can truncate a profile |
| 14 | F-023 | confirmed defect | high operational/safety access | Persistent NAK blocks the sole HMI worker indefinitely |
| 15 | F-024, F-049–F-052 | mixed below | medium/high | Persistence, release configuration, and test backlog |

## Safety and control findings

### F-016 — Indicator/contact-or GPIO collision

- **Category/confidence:** confirmed defect / high.
- **Evidence:** `components/heater_controller_component/Kconfig` and current config select `CONFIG_HEATER_CONTACTOR_GPIO_PIN=22`; `components/run_indicator/Kconfig` and current config select `CONFIG_RUN_INDICATOR_GPIO=22`; `run_indicator.c:run_indicator_task` writes the pin every 200 ms; `heater_controller.c:start_heater/stop_heater` writes the same pin.
- **Trigger/impact:** any indicator ON/OFF/BLINK write also drives the contactor. After natural completion, F-015 leaves indicator ON, so it can reassert the contactor after coordinator STOP. The pulled `6741c72` fault-pause path emits `PROFILE_PAUSED`, which selects BLINK, so a stall or hold-deviation fault can now periodically drive the contactor pin. SSR state limits immediate heat in the nominal case, but independent contactor isolation is defeated.
- **Direction:** determine the schematic-approved indicator pin, separate pin ownership, and add compile/startup validation for collisions among actuator, indicator, fan, and UART pins.
- **Uncertainty:** deployed wiring, active polarity, and external interlocks.

### F-017 — SSR-off GPIO failure does not force independent isolation

- **Category/confidence:** confirmed defect / high; physical persistence is hardware-dependent.
- **Evidence:** `heater_controller_task.c:heater_controller_task` checks `toggle_heater(HEATER_OFF)` only by calling `check_error_and_post_event`; that helper posts `FURNACE_ERROR_EVENT`. No production subscriber to that event was found, and the PWM loop continues.
- **Trigger/impact:** an SSR-low GPIO operation returns failure while output remains asserted. The contactor is not synchronously dropped, so heat can remain continuously demanded.
- **Direction:** latch a direct actuator inhibit and synchronously attempt independent contactor-off on any output-write failure; require explicit recovery.

### F-001 — Control accepts nonexistent or stale temperature

- **Category/confidence:** confirmed defect / high.
- **Evidence:** `temp_sensor_device_core.c:temp_sensor_create/temp_sensor_update/temp_sensor_read`; `temperature_processor_task.c:read_temp_sensors`; `coordinator_component_events.c:temperature_processor_event_handler`; `coordinator_component_heater_controller.c:heater_controller_task`.
- **Trigger/impact:** profile starts before first physical sample or Modbus fails after a valid sample. Initial `0.0f` or cached value has no timestamp/validity and continues into profile/PID logic.
- **Direction:** publish one synchronized sample snapshot with validity, age, contributors, and quorum; require freshness before and during heat; independently inhibit on timeout.

### F-002 — Dispatcher self-deadlock can precede heater-off

- **Category/confidence:** confirmed defect / high.
- **Evidence:** `commands_manager_core.c:commands_dispatcher_dispatch_command` uses `portMAX_DELAY`; `commands_dispatcher_task.c:commands_dispatcher_task` is sole consumer; coordinator pause/stop handlers submit multiple heater commands; queue default is ten.
- **Trigger/impact:** handler runs with insufficient remaining queue slots, fills its own queue, and blocks forever. Later CLEAR/STOP cannot execute while prior output may remain active.
- **Direction:** never synchronously submit to the consumer's own bounded queue; add direct idempotent inhibit and bounded submissions.

### F-003 — In-flight PID publication can restore heat after pause

- **Category/confidence:** confirmed defect / high.
- **Evidence:** `coordinator_component_heater_controller.c:heater_controller_task`, `pause_heating_profile`, and `kill_heater` do not serialize state transition with positive output publication.
- **Trigger/impact:** PID computes positive power, pause queues zero/clear, then preempted PID work resumes and queues stale positive power. Paused ticks do not necessarily reassert zero.
- **Direction:** synchronized run generation/inhibit checked again at actuator boundary.

### F-004 — Failed sensor batch is still published

- **Category/confidence:** confirmed defect / high.
- **Evidence:** `temperature_processor.c:process_temperature_samples` returns anomaly failure; `temperature_processor_task.c:temp_process_task` logs/posts error then still posts the average.
- **Trigger/impact:** sensors disagree beyond threshold; faulty low data can dilute overshoot detection and increase demand.
- **Direction:** define quorum/outlier validity and never publish failed batches as control-valid.

### F-018 — Reset paths do not explicitly inhibit outputs

- **Category/confidence:** highly likely defect / medium-high.
- **Evidence:** `nextion_settings_handlers.c` restart/factory-reset paths perform UI/storage delays then call `esp_restart()` without a synchronous heater inhibit.
- **Impact:** reset-time output depends on GPIO reset behavior, external pulls, contactor polarity, and SSR hardware rather than an application invariant.
- **Direction:** validate hardware and add a bounded direct inhibit before any restart path.

### F-019 — Profile-start partial failure can leave contactor start queued

- **Category/confidence:** confirmed defect / high.
- **Evidence:** `coordinator_component_heater_controller.c:start_heating_profile` queues HEATER_CLEAR/HEATER_START before `xTaskCreate`; task-creation failure returns without queued STOP or complete profile unwind.
- **Impact:** profile start reports failure but output authorization may already execute.
- **Direction:** establish all control resources first or synchronously unwind/inhibit on every later failure.

### F-020 — PID history persists across runs

- **Category/confidence:** source-level mitigation added; regression and hardware characterization still missing.
- **Evidence:** `6741c72` calls `pid_controller_reset()` before profile load in `start_heating_profile`; it also adds `pid_controller_reset_for_setpoint()` at heating-to-hold and resume. `pid_component.c` remains a file-static singleton.
- **Current status:** the previously confirmed fresh-run carryover path is addressed in source, but the exact first-tick/hold/resume behavior and all reset paths are not regression-tested. Retain this ID until the behavior is characterized rather than silently treating it as release-verified.

### F-021 — Current temperature is a cross-task data race

- **Category/confidence:** confirmed defect / high under the C memory model.
- **Evidence:** private event-loop callback writes coordinator temperature in `coordinator_component_events.c`; coordinator control task reads it in profile/PID/stall logic in `coordinator_component_heater_controller.c`; no mutex, queue, or atomic snapshot exists.
- **Impact:** undefined cross-core visibility/torn/stale behavior can affect setpoint transitions and demand. The new stall and hold-deviation decisions consume this same value, widening the impact to fault detection.
- **Direction:** transfer an immutable validity/freshness sample to the control task or guard a complete snapshot with one synchronization protocol.

## Concurrency and lifecycle findings

### F-006 — Temperature processor frees live task context

- **Category/confidence:** confirmed defect / high.
- **Evidence:** `temperature_processor_core.c:shutdown_temp_processor`; `temperature_processor_task.c:temp_process_task`. Stop clears/notifies, owner frees immediately, and handle clearing is placed after unreachable `vTaskDelete(NULL)`.
- **Direction:** worker acknowledges before self-delete; owner joins, unsubscribes, then frees.

### F-007 — Dispatcher deletes queue/context under live task

- **Category/confidence:** confirmed defect / high.
- **Evidence:** `commands_dispatcher_task.c:stop_commands_dispatcher_task`; `commands_manager_core.c:commands_dispatcher_shutdown`. Worker can remain in two-second wait; one-second poll times out, cleanup still proceeds, and worker never reliably clears its handle.

### F-008 — Coordinator teardown leaves task/callbacks on freed state

- **Category/confidence:** confirmed defect / high.
- **Evidence:** `coordinator_core.c:shutdown_coordinator`; `coordinator_component_events.c`; `coordinator_component_heater_controller.c`; `temperature_profile_core.c`. Temperature subscription and awakened control/profile work can outlive freed context.

### F-009 — Heater teardown deletes mutex/context before worker exit

- **Category/confidence:** confirmed defect / high.
- **Evidence:** `heater_controller_core.c:shutdown_heater_controller`; `heater_controller_task.c`. PWM task can still wake and use deleted state/mutex.

### F-012 — Device manager stop/reinit permits overlapping workers

- **Category/confidence:** confirmed defect / high.
- **Evidence:** `device_manager_task.c:stop_device_manager_task/init_device_manager_task`. Stop does not join before `running` can become true again.

### F-013 — Manual-target mailbox mixes atomic/plain access

- **Category/confidence:** confirmed defect / medium-high.
- **Evidence:** `coordinator_component_internal.h` plain fields; event handler writes `pending` plainly; control task uses `atomic_exchange` on the same object.
- **Direction:** queue/mutex ownership or a fully declared atomic release/acquire protocol.

### F-028 — Run-indicator mode is a cross-task data race

- **Category/confidence:** confirmed defect / medium.
- **Evidence:** event-loop callback writes static `s_mode`; indicator task reads it in `run_indicator.c` without synchronization.

## Temperature, Modbus, and device findings

### F-005 — Successful sensor samples are not compacted

- **Category/confidence:** confirmed defect / high.
- **Evidence:** `temperature_processor_task.c:read_temp_sensors` writes by physical sensor index, while `temperature_processor.c:process_temperature_samples` consumes `[0, samples_count)`.
- **Trigger/impact:** earlier read fails and later succeeds; stale failed slot is included and fresh later value excluded.
- **Direction:** append each successful sample at `buffer[count]`.

### F-010 — Failed sensor initialization can create tight high-priority loop

- **Category/confidence:** confirmed defect / high.
- **Evidence:** `temperature_processor_core.c:init_temp_processor` ignores `init_devices` result; zero-sample branch in `temperature_processor_task.c` continues before blocking/wait.

### F-025 — MS9024 write verification compares only low byte

- **Category/confidence:** confirmed defect / high.
- **Evidence:** `temp_sensor_device/src/ms9024.c:ms9024_write_and_verify` masks/compares low byte; repair writes 16-bit value 282 to register 129.
- **Impact:** mismatched high byte can be accepted as successful configuration repair.

### F-029 — Failed sensor creation leaks static pool slot

- **Category/confidence:** confirmed defect / medium.
- **Evidence:** `temp_sensor_device_core.c:temp_sensor_create` sets `allocated=true` before `device_manager_create_device`; error macro returns without clearing the slot.

### F-030 — Modbus shutdown retains deleted handle

- **Category/confidence:** confirmed latent defect / medium.
- **Evidence:** `modbus_master_core.c:modbus_master_shutdown` deletes master but does not clear handle; request path lacks initialization-state validation.
- **Trigger:** shutdown/reinit or request after shutdown; current startup is boot-only.

## HMI, persistence, and configuration findings

### F-022 — Partial program read is accepted as complete

- **Category/confidence:** confirmed defect / high.
- **Evidence:** `nextion_file_reader.c:nextion_read_file` returns success when `total_received > 0` after later-chunk timeout; `nextion_storage.c:nextion_storage_parse_file_to_draft` replaces the draft and parses the partial prefix.
- **Impact:** a multi-stage schedule can silently become a shorter valid schedule.
- **Direction:** require exact file length/completion and parse into a temporary model before atomic replacement; add version/checksum if protocol permits.

### F-023 — Persistent Nextion NAK blocks sole HMI worker

- **Category/confidence:** confirmed defect / high.
- **Evidence:** `nextion_storage.c` transfer loop `continue`s indefinitely on `0x04` NAK without retry/progress limit; save executes on sole HMI coordinator.
- **Impact:** HMI pause/stop input cannot be processed while heating continues.
- **Direction:** bound retries/time, release UART/storage ownership, and preserve an independent stop path.

### F-024 — Program save is destructive before replacement succeeds

- **Category/confidence:** confirmed defect / high.
- **Evidence:** `nextion_storage.c:nextion_storage_save_program` deletes existing file before transfer; later timeout/NAK/power loss loses last good copy.
- **Direction:** write/verify temporary or versioned file, then replace using panel-supported semantics.

### F-026 — Factory reset does not enumerate stored programs

- **Category/confidence:** confirmed defect / medium.
- **Evidence:** `nextion_storage.c` delete-all walks a volatile session registry capped by coordinator max profiles (five) while storage advertises 200; registry is populated only by current-session saves/loads.
- **Impact:** after reboot, existing panel SD programs can survive a claimed factory reset.

### F-027 — Program deletion reports success without response validation

- **Category/confidence:** confirmed defect / medium.
- **Evidence:** `nextion_storage.c:nextion_storage_delete_program` sends `delfile`, delays, and returns true without checking panel result.

### F-031 — Coordinator query commands publish uninitialized/wrong payload

- **Category/confidence:** confirmed latent defect / medium.
- **Evidence:** `coordinator_component_events.c` GET_STATUS/GET_CURRENT_PROFILE cases declare uninitialized stack objects and post them; status payload conflicts with the declared event type. No current in-tree producer was found.

### F-032 — Destructive command routing uses substring matching

- **Category/confidence:** possible defect / medium.
- **Evidence:** `nextion_events.c` routes restart/factory-reset by `strstr`; malformed frames containing tokens could invoke destructive handlers. Reachability depends on panel protocol framing/caller guards.

### F-033 — HMI numeric parsing lacks range/overflow checks

- **Category/confidence:** highly likely defect / medium.
- **Evidence:** `nextion_parse_utils.c` uses `strtol` without `errno`/range validation; decimal accumulation can overflow signed integers.

### F-034 — Enable Kconfig booleans are not honored by build/startup

- **Category/confidence:** highly likely defect / medium.
- **Evidence:** `RUN_INDICATOR_ENABLED` and `NEXTION_HMI_ENABLED` guard Kconfig symbols, but component sources and `main` calls are unconditional.
- **Next evidence:** clean builds with each feature disabled.

## Operational and maintainability findings

### F-011 — Coordinator timer failure can report/start inconsistent profile

- **Category/confidence:** confirmed defect / high.
- **Evidence:** timer create/start errors in `start_heating_profile` do not fully unwind already loaded/profile/output state.

### F-014 — HMI bridge silently drops lifecycle/error telemetry

- **Category/confidence:** confirmed defect / medium.
- **Evidence:** `hmi_coordinator.c` event bridges send to bounded queue with zero wait and ignore failure. In `6741c72`, a control fault posts separate `ERROR_OCCURRED` and `PROFILE_PAUSED` events; each bridge attempt is independently lossy. The transfer-time critical-event buffer is also bounded (eight entries).
- **Impact:** an operator can see a pause with no fault context, or an error with no corresponding paused state. A UI-side resume can therefore be based on incomplete information; the queued fault-off path must not rely on HMI delivery.

### F-015 — Natural completion leaves run indicator ON

- **Category/confidence:** confirmed defect / high when combined with F-016.
- **Evidence:** `run_indicator.c:run_indicator_event_handler` handles STOPPED but not COMPLETED.

### F-035 — Furnace errors have no mitigation subscriber

- **Category/confidence:** design weakness / high.
- **Evidence:** multiple publishers of `FURNACE_ERROR_EVENT`; no production subscription found. `error_manager` has no descriptor registration found.

### F-036 — Health/watchdog supervision is disabled and incomplete

- **Category/confidence:** design weakness / high.
- **Evidence:** `init_health_monitor()` is commented in `main.c`; if enabled after existing producers start, prior registrations may be lost. Health failure stops watchdog reset but does not directly inhibit outputs.

### F-037 — Duplicate active/legacy temperature architectures

- **Category/confidence:** maintainability issue / medium.
- **Evidence:** current Modbus/device/processor path and compiled but inactive SPI/MAX31865 monitor coexist, with separate lifecycle/synchronization/error models.

### F-038 — Device-manager count is not maintained

- **Category/confidence:** confirmed low defect / low.
- **Evidence:** `device_manager_context_t.count` is checked but no increment/decrement was found; slot scan still enforces the physical array limit, making this currently misleading rather than overflowing.

### F-048 — Cubic soft-landing accelerates before it decelerates

- **Category/confidence:** confirmed defect / medium-high control quality.
- **Evidence:** `temperature_profile_core.c:ramp_ease_position` uses `q=f0+w*(u+u²-u³)` in the final ease band. Relative to linear interpolation, `q-linear = w*u²*(1-u)`, which is positive for all interior points; its rate multiplier is `1+2u-3u²`, peaking at 4/3.
- **Trigger/impact:** every heating ramp using a nonzero ease band commands up to 33% faster setpoint movement before tapering, leading the former linear profile by up to `4*ease_band/27` (0.89 C at the 6 C default). This contradicts the stated soft-landing/taper intent and can increase pre-handover control demand. The physical overshoot consequence requires bench validation.
- **Direction:** select and document the intended trajectory, then test seam continuity, monotonicity, rate bounds, endpoint, short-span behavior, and HMI graph parity.

### F-049 — Control defaults, resolved build, and field-tuning documentation disagree

- **Category/confidence:** highly likely defect / high operational uncertainty.
- **Evidence:** changed Kconfig defaults are overshoot 20 C, stall check 300 s, PID Kd 0.030, and fan 43/40 C. The reviewed resolved `sdkconfig` is 30 C, 180 s, Kd 0, and 35/32 C; it disables feedforward and service-time override. No tracked `sdkconfig.defaults` was found. `components/pid_component/pid_values.md` mixes different gains/feedforward calibration and labels unproven values as field information.
- **Impact:** fresh configurations, upgraded local configurations, and claimed field tuning can run materially different control and guard behavior. The successful build validates only the retained local configuration, not the changed defaults.
- **Direction:** track an explicit per-furnace/release configuration artifact, record its commit/hash with bench evidence, define upgrade behavior, and add a build-time effective-symbol check.

### F-050 — Operational-time service override can wrap and misreport persistence

- **Category/confidence:** confirmed defect / medium.
- **Evidence:** `components/nextion_hmi/src/program/Kconfig` gives `NEXTION_OP_TIME_OVERRIDE_HOURS` no range. `heating_program_models.c` casts it to `uint32_t` and multiplies by 3600 before writing NVS; negative values or values above 1,193,046 hours wrap. The enabled override executes at every boot and NVS set/commit failures are not surfaced before reporting the value.
- **Impact:** service hours can be corrupted or falsely reported as restored, weakening maintenance records.
- **Direction:** impose a valid range, check persistence errors, and make the operation a consumed one-shot or rename/document it as a persistent boot override.

### F-051 — New coordinator error payload has an unversioned compatibility contract

- **Category/confidence:** design weakness / medium.
- **Evidence:** `coordinator_error_data_t` in `event_registry.h` grew from the prior code's small payload to temperature/setpoint/stage/elapsed fields, and gained `COORDINATOR_ERROR_HOLD_DEVIATION`. In-tree producers/consumers build, but independently compiled subscribers and fixed-size bridges have no version/size contract.
- **Direction:** state a payload compatibility policy or version the message, and add a producer/consumer copy-size regression test.

### F-053 — Non-finite PID input can become non-finite actuator demand

- **Category/confidence:** confirmed defect / high conditional control safety.
- **Evidence:** `pid_controller_compute` rejects only nonpositive `dt`; it does not reject NaN/Inf measurement, setpoint, or state. Comparisons used by its output clamp are false for NaN, allowing NaN to leave the function. `heater_controller_task.c:set_heater_target_power_level` likewise checks only `< 0` and `> 1`, so NaN bypasses its range rejection.
- **Trigger/impact:** a non-finite value from any upstream calculation can propagate to SSR demand and history state. The current Modbus parser's reachability of non-finite values needs a focused trace, but the PID/actuator boundary is source-confirmed unsafe for them.
- **Direction:** explicitly reject non-finite inputs/state/output at each control boundary, force zero through an independent inhibit path, and add NaN/Inf regression cases.

## Missing tests

- **F-039 — missing test:** no project host test suite or ESP-IDF unit-test component was found for PID/profile, parsers, validation, or failure paths.
- **F-040 — missing test:** no automated alternate-Kconfig/effective-configuration build matrix validates optional features, pin collisions, queue depths, timing bounds, source defaults versus upgrade configuration, or incompatible feedforward/adaptive settings.
- **F-041 — missing test:** no recorded device-bench or powered-controller validation covers boot/reset GPIO states, sensor loss, Modbus timeout, queue saturation, pause/stop races, watchdog reset, and independent output removal.
- **F-052 — missing test:** no deterministic coverage exists for eased-ramp shape/graph parity, handover tolerance, stall lag/rate, sustained hold deviation, PID reset/feedforward/anti-windup, non-finite control inputs, or coordinator error-payload delivery.

## Unresolved questions

- **F-042:** What are the deployed GPIO active polarities, external pull states, schematic-approved indicator pin, and reset behavior?
- **F-043:** Is there an independent hardwired overtemperature cutoff, airflow proof, or safety contactor circuit?
- **F-044:** What exact MS9024 revision/register widths/float word order and Nextion file-transfer guarantees apply?
- **F-045:** What are resolved production task priorities/stacks, ESP-IDF watchdog/brownout settings, and measured high-water marks?
- **F-046:** Does the deployed Nextion project enforce stronger command framing than the parser, and is the `.HMI` source available?
- **F-047:** What physical output state and time-to-de-energize occur during WDT, software restart, brownout, and power cycling?

## Recommended first hardening batch (do not fix in this preparation pass)

These five are independent, source-confirmed, testable with low architectural risk. They reduce incorrect control/data behavior while the larger actuator-inhibit and fresh-sample architecture is decided:

1. **F-048:** replace or explicitly characterize the soft-landing trajectory; add trajectory and graph-parity coverage.
2. **F-005:** compact successful temperature samples; add sparse-success batch tests.
3. **F-022:** reject partial file reads and parse atomically into a temporary draft.
4. **F-025:** verify full register width for MS9024 writes.
5. **F-050:** bound service operational-time override and propagate NVS write failures.

Before any powered release, investigate/resolve F-016, then make an ADR for the direct actuator-inhibit/fresh-temperature gate needed by F-017/F-001/F-002/F-003/F-004. Those are higher risk but cross multiple current boundaries and should not be patched piecemeal.

## Existing build evidence

ESP-IDF 5.5.4 build completed on pulled commit `16467e0` on 2026-07-13 using `IDF_PATH=/home/vesko/.espressif/v5.5.4/esp-idf cmake --build build -j2`: application `0x614f0` bytes, 62% of the smallest 1 MiB app partition free. The build used the retained local `sdkconfig`, not the new Kconfig defaults (F-049). Warnings included discarded `const` in logger CLI code, logger-storage declaration issues, unused legacy MAX31865 parser, and an impossible unsigned `< 0` check in health monitoring. Build evidence does not validate hardware behavior.
