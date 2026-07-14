# Firmware findings register

Date: 2026-07-14. Scope: read-only static architecture, safety, concurrency, lifecycle, integration, control, persistence, and configuration analysis. No hardware was operated. Line numbers are navigation hints; symbols/current source are authoritative.

Categories: **confirmed defect** has a reachable source-level failure; **highly likely defect** has strong evidence with one runtime/config dependency; **possible defect** needs more evidence; **design weakness**, **maintainability issue**, **missing test**, and **unresolved question** are not claimed defects.

## Priority summary

| Rank | ID | Category | Risk | Summary |
| ---: | --- | --- | --- | --- |
| 1 | F-016 | source fix implemented; validation pending | critical safety/configuration | Run indicator is disabled when disabled, invalid, or colliding with the contactor GPIO |
| 2 | F-017 | source fix implemented; validation pending | critical conditional safety | SSR GPIO failure now latches local inhibit and requests contactor-off; electrical behavior is unverified |
| 3 | F-001 | Phase B source fix implemented; validation pending | critical control safety | Fresh-sample quorum, direct sensor inhibit, pause, and three-sample recovery are implemented |
| 4 | F-002 | source fix implemented; validation pending | critical safety/concurrency | Dispatcher can deadlock before queued heater-off commands execute |
| 5 | F-003 | source fix implemented; validation pending | critical safety/concurrency | Heater-side control inhibit rejects stale PID output after pause/stop |
| 6 | F-004 | policy-adjusted; validation pending | high control | Sensor disagreement is intentionally a warning; fresh-sample quorum now gates control input |
| 7 | F-018 | source fix implemented; validation pending | high reset safety | Restart/factory-reset paths now fail closed unless direct heater inhibit succeeds |
| 8 | F-019 | source fix implemented; validation pending | high startup/control | Profile start now creates the control task before queuing contactor START |
| 9 | F-048 | source fix implemented; validation pending | high control | Ramp-to-hold easing now decelerates without leading the configured ramp rate |
| 10 | F-053 | source fix implemented; regression pending | high conditional control safety | Non-finite PID values are reset/forced to zero before heater PWM conversion |
| 11 | F-021 | source fix implemented; validation pending | high concurrency/control | Coordinator temperature now uses a mutex-protected snapshot |
| 12 | F-009 | source fix implemented; validation pending | high lifecycle | Heater teardown now joins its worker; stop/restart validation remains open |
| 13 | F-022 | source fix implemented; validation pending | high persistence/control | Incomplete Nextion file reads are rejected before draft replacement |
| 14 | F-023 | source fix implemented; validation pending | high operational/safety access | Repeated NAKs now abort storage transfer instead of blocking the sole HMI worker |
| 15 | F-024 | source fix implemented; validation pending | high persistence/control | Program replacement now uses a completed temporary file; final rename power-loss behavior remains unverified |
| 16 | F-049–F-052 | mixed below | medium/high | Persistence, release configuration, and test backlog |

## Safety and control findings

### F-016 — Indicator/contact-or GPIO collision

- **Category/confidence:** production configuration disabled; source defense in depth retained / high.
- **Evidence:** `components/heater_controller_component/Kconfig` and current config select `CONFIG_HEATER_CONTACTOR_GPIO_PIN=22`; `components/run_indicator/Kconfig` and current config select `CONFIG_RUN_INDICATOR_GPIO=22`; `run_indicator.c:run_indicator_task` writes the pin every 200 ms; `heater_controller.c:start_heater/stop_heater` writes the same pin.
- **Trigger/impact:** any indicator ON/OFF/BLINK write also drives the contactor. After natural completion, F-015 leaves indicator ON, so it can reassert the contactor after coordinator STOP. The pulled `6741c72` fault-pause path emits `PROFILE_PAUSED`, which selects BLINK, so a stall or hold-deviation fault can now periodically drive the contactor pin. SSR state limits immediate heat in the nominal case, but independent contactor isolation is defeated.
- **Source correction:** the production `main` component no longer links or initializes the test run-indicator component. The source initialization guard still refuses invalid, disabled, or contactor-colliding pins if the component is later enabled in an explicit test build.
- **Residual risk:** the indicator is intentionally unavailable; any future re-enablement requires a synchronization fix, schematic-approved pin, active-polarity review, and powered validation.
- **Uncertainty:** deployed wiring, active polarity, and external interlocks.

### F-017 — SSR-off GPIO failure does not force independent isolation

- **Category/confidence:** source fix implemented; regression and powered-controller validation pending.
- **Evidence:** `heater_controller_task.c:heater_controller_handle_ssr_failure` now latches `output_inhibited`, clears demand, retries SSR-low, directly calls `stop_heater()`, then posts `FURNACE_ERROR_EVENT`. PWM and heater-on command paths reject reauthorization while the latch is set.
- **Residual risk:** physical persistence, contactor polarity/independence, GPIO-mutex latency, and F-002/F-003 remain hardware/architecture-dependent.
- **Validation:** [BUG-017-ssr-off-failure.md](BUG-017-ssr-off-failure.md), [CHANGE-VALIDATION-F17.md](CHANGE-VALIDATION-F17.md), and [F17-HARDWARE-VALIDATION.md](F17-HARDWARE-VALIDATION.md).

### F-001 — Control accepts nonexistent or stale temperature

- **Category/confidence:** source fix implemented; regression and hardware validation pending.
- **Evidence:** sensor devices record successful-read ticks; the processor compacts fresh samples and publishes a validity snapshot; coordinator invalid snapshots directly set heater sensor-data inhibit and pause the profile; three consecutive valid aggregates release only that recoverable inhibit and resume the profile.
- **Residual risk:** quorum is temporarily derived as sensor-count-minus-two; physical output latency, GPIO polarity, and full HMI fault presentation remain unverified/deferred. F-003 remains a separate in-flight PID race.
- **Validation:** [BUG-001-temperature-freshness.md](BUG-001-temperature-freshness.md), [CHANGE-VALIDATION-F001-PHASE-A.md](CHANGE-VALIDATION-F001-PHASE-A.md), and [CHANGE-VALIDATION-F001-PHASE-B.md](CHANGE-VALIDATION-F001-PHASE-B.md).

### F-002 — Dispatcher self-deadlock can precede heater-off

- **Category/confidence:** source fix implemented; regression and hardware validation pending.
- **Evidence:** `commands_manager_core.c:commands_dispatcher_dispatch_command` uses `portMAX_DELAY`; `commands_dispatcher_task.c:commands_dispatcher_task` is sole consumer; coordinator pause/stop handlers submit multiple heater commands; queue default is ten.
- **Trigger/impact:** handler runs with insufficient remaining queue slots, fills its own queue, and blocks forever. Later CLEAR/STOP cannot execute while prior output may remain active.
- **Source correction:** dispatcher-task re-entrant submissions now invoke the registered handler directly instead of self-enqueuing. Calls from other tasks retain the existing queue and blocking behavior, preserving normal command ordering while preventing the sole consumer from waiting on its own full queue.
- **Residual risk:** nested handler execution increases dispatcher stack depth and handler re-entrancy is not characterized; external queue callers and physical GPIO-off behavior still require validation. F-003 remains a separate in-flight PID race.
- **Validation:** [CHANGE-VALIDATION-F002.md](CHANGE-VALIDATION-F002.md).

### F-003 — In-flight PID publication can restore heat after pause

- **Category/confidence:** source fix implemented; regression and hardware validation pending / high.
- **Evidence:** `coordinator_component_heater_controller.c:heater_controller_task`, `pause_heating_profile`, and `kill_heater` do not serialize state transition with positive output publication.
- **Trigger/impact:** PID computes positive power, pause queues zero/clear, then preempted PID work resumes and queues stale positive power. Paused ticks do not necessarily reassert zero.
- **Source correction:** coordinator pause, fault pause, stop, completion, and emergency-stop paths assert a temporary heater control inhibit that directly de-energizes outputs; the heater rejects power/start commands while that inhibit is active. Profile start and resume explicitly release only this temporary gate.
- **Residual risk:** this is intentionally a narrow hardening gate, not a command-generation protocol; queue ordering, resume behavior, and physical output latency still require regression and powered-controller validation.

### F-004 — Failed sensor batch is still published

- **Category/confidence:** policy-adjusted; regression and hardware validation pending.
- **Evidence:** field requirements explicitly treat 3–5°C (or larger) disagreement as a warning because sensors are equally representative chamber probes. The Phase B processor now uses fresh-sample quorum for validity, while disagreement remains logged as a warning and the fresh aggregate is retained.
- **Residual risk:** no per-board outlier rejection or sensor mapping exists yet; validate whether hottest/coldest rejection is needed after chamber data collection.

### F-018 — Reset paths do not explicitly inhibit outputs

- **Category/confidence:** source fix implemented; regression and hardware validation pending / medium-high.
- **Evidence:** `nextion_settings_handlers.c` restart/factory-reset paths perform UI/storage delays then call `esp_restart()` without a synchronous heater inhibit.
- **Impact:** reset-time output depends on GPIO reset behavior, external pulls, contactor polarity, and SSR hardware rather than an application invariant.
- **Source correction:** both actual reset paths call the heater's direct temporary control inhibit before UI delays, storage changes, display reset, or `esp_restart()`. If inhibition fails, the reset is refused.
- **Residual risk:** GPIO polarity, pull-down behavior, contactor/SSR electrical independence, and measured de-energization latency still require hardware validation.

### F-019 — Profile-start partial failure can leave contactor start queued

- **Category/confidence:** source fix implemented; regression and hardware validation pending / high.
- **Evidence:** `coordinator_component_heater_controller.c:start_heating_profile` queues HEATER_CLEAR/HEATER_START before `xTaskCreate`; task-creation failure returns without queued STOP or complete profile unwind.
- **Impact:** profile start reports failure but output authorization may already execute.
- **Source correction:** coordinator task creation now completes before the temporary heater inhibit is released and before HEATER_CLEAR/HEATER_START are submitted. A task-creation failure therefore submits no new heater-start command.
- **Residual risk:** timer-create/start failure remains F-011; command submission failures and physical output behavior still require validation.
- **Direction:** establish all control resources first or synchronously unwind/inhibit on every later failure.

### F-020 — PID history persists across runs

- **Category/confidence:** source fix implemented; regression and hardware characterization still missing / high control.
- **Evidence:** `pid_component.c` remains a file-static singleton. The coordinator reset points include profile start, stop, heating-to-hold handover, and resume.
- **Source correction:** `stop_heating_profile()` now calls `pid_controller_reset()` after asserting the heater inhibit and killing the heater, so completion, emergency stop, and explicit stop clear controller history immediately rather than waiting for the next run.
- **Residual risk:** exact first-tick/hold/resume behavior and all reset paths are not regression-tested; no hardware validation is claimed.

### F-021 — Current temperature is a cross-task data race

- **Category/confidence:** source fix implemented; regression validation pending / high under the C memory model.
- **Evidence:** private event-loop callback writes coordinator temperature under `temperature_mutex` in `coordinator_component_events.c`; coordinator control task reads it through `coordinator_get_current_temperature` in `coordinator_component_heater_controller.c`. No remaining direct control-task read of the shared float was found.
- **Impact:** undefined cross-core visibility/torn/stale behavior can affect setpoint transitions and demand. The new stall and hold-deviation decisions consume this same value, widening the impact to fault detection.
- **Source correction:** the existing branch hardening adds one mutex-protected temperature snapshot and routes coordinator control reads through its getter.
- **Residual risk:** freshness/validity remains a separate sensor-data contract; executable race characterization and lifecycle validation remain open.

## Concurrency and lifecycle findings

### F-006 — Temperature processor frees live task context

- **Category/confidence:** source fix implemented; regression validation pending / high.
- **Evidence:** `temperature_processor_core.c:shutdown_temp_processor`; `temperature_processor_task.c:temp_process_task`. Stop clears/notifies, owner frees immediately, and handle clearing is placed after unreachable `vTaskDelete(NULL)`.
- **Source correction:** the worker signals a context-owned exit semaphore before self-deletion; the owner waits for that acknowledgement before clearing the handle, deleting the semaphore, and freeing the context.
- **Residual risk:** event-manager unsubscribe and device-manager producer shutdown remain broader lifecycle concerns tracked separately; executable stop/restart coverage is still missing.

### F-007 — Dispatcher deletes queue/context under live task

- **Category/confidence:** source fix implemented; regression validation pending / high.
- **Evidence:** `commands_dispatcher_task.c:stop_commands_dispatcher_task`; `commands_manager_core.c:commands_dispatcher_shutdown`. Worker can remain in two-second wait; one-second poll times out, cleanup still proceeds, and worker never reliably clears its handle.
- **Source correction:** dispatcher running state is atomic; the worker signals a context-owned exit semaphore before self-deletion; shutdown waits for that acknowledgement and refuses queue/handler/context cleanup if stopping fails.
- **Residual risk:** external producers blocked in `xQueueSend(..., portMAX_DELAY)` still require a producer-shutdown contract; executable stop/restart coverage is missing.

### F-008 — Coordinator teardown leaves task/callbacks on freed state

- **Category/confidence:** source fix implemented; regression validation pending / high.
- **Evidence:** `coordinator_core.c:shutdown_coordinator`; `coordinator_component_events.c`; `coordinator_component_heater_controller.c`; `temperature_profile_core.c`. Temperature subscription and awakened control/profile work can outlive freed context.
- **Source correction:** coordinator shutdown now handles inactive/completed contexts, waits for the control worker's exit acknowledgement before destroying the temperature mutex/context, and makes the worker self-exit path acknowledge before deletion.
- **Residual risk:** event-manager callback quiescence and profile/timer cleanup need executable stop/restart coverage; broader producer ownership remains open.

### F-009 — Heater teardown deletes mutex/context before worker exit

- **Category/confidence:** source fix implemented; regression and hardware validation pending / high.
- **Evidence:** `heater_controller_core.c:shutdown_heater_controller`; `heater_controller_task.c`. PWM task can still wake and use deleted state/mutex.
- **Source correction:** heater shutdown now signals the worker, waits for its context-owned exit acknowledgement, and only then clears the task handle, destroys the mutex/semaphore, and frees the context. The running flag uses atomic access.
- **Residual risk:** stop/restart regression coverage is still missing; GPIO off polarity, electrical isolation, and de-energization timing require hardware validation.

### F-012 — Device manager stop/reinit permits overlapping workers

- **Category/confidence:** source fix implemented; regression validation pending / high.
- **Evidence:** `device_manager_task.c:stop_device_manager_task/init_device_manager_task`. Stop does not join before `running` can become true again.
- **Source correction:** device-manager stop now atomically clears the running flag, wakes the worker, and waits for its context-owned exit acknowledgement before clearing the task handle. A subsequent init therefore cannot reuse the context while the previous worker is still alive.
- **Residual risk:** device-update callbacks and event consumers still lack a broader producer/lifecycle contract; stop/restart regression and Modbus/device validation remain open.

### F-013 — Manual-target mailbox mixes atomic/plain access

- **Category/confidence:** source fix implemented; regression validation pending / medium-high.
- **Evidence:** `coordinator_component_internal.h` plain fields; event handler writes `pending` plainly; control task uses `atomic_exchange` on the same object.
- **Source correction:** the manual-target mailbox now has a coordinator-owned mutex. The event handler copies target, rate, and pending state under the mutex; the profile task copies and clears the complete update under the same mutex before applying it.
- **Residual risk:** concurrent-update regression coverage and runtime HMI/profile validation remain open; the mailbox remains latest-value-wins by design.

### F-028 — Run-indicator mode is a cross-task data race

- **Category/confidence:** confirmed defect, dormant by production build / medium.
- **Evidence:** event-loop callback writes static `s_mode`; indicator task reads it in `run_indicator.c` without synchronization.
- **Status:** the production build excludes the component, so the task and callback are not part of the firmware image. A synchronization correction is required before re-enablement.

## Temperature, Modbus, and device findings

### F-005 — Successful sensor samples are not compacted

- **Category/confidence:** source fix implemented; regression and sensor validation pending / high.
- **Evidence:** `temperature_processor_task.c:read_temp_sensors` writes by physical sensor index, while `temperature_processor.c:process_temperature_samples` consumes `[0, samples_count)`.
- **Source correction:** accepted fresh samples are now appended at `temperatures_buffer[*number_of_samples]`, with the sample count incremented only after a successful read and freshness check.
- **Residual risk:** sparse-success regression coverage and sensor validation remain open.

### F-010 — Failed sensor initialization can create tight high-priority loop

- **Category/confidence:** source fix implemented; regression and sensor validation pending / high.
- **Evidence:** `temperature_processor_core.c:init_temp_processor` ignores `init_devices` result; zero-sample branch in `temperature_processor_task.c` continues before blocking/wait.
- **Source correction:** the current worker already blocks on its notification after a zero-sample cycle; initialization now also checks `init_devices`, destroys any partially created sensors, and refuses to start the worker when setup fails.
- **Residual risk:** initialization fault-injection and sensor/device lifecycle validation remain open.

### F-025 — MS9024 write verification compares only low byte

- **Category/confidence:** source fix implemented; regression and device validation pending / high.
- **Evidence:** `temp_sensor_device/src/ms9024.c:ms9024_write_and_verify` masks/compares low byte; repair writes 16-bit value 282 to register 129.
- **Source correction:** write verification and the adjacent auto-correct comparison now require the complete 16-bit readback to equal the desired value; diagnostics log both bytes.
- **Residual risk:** Modbus fault-injection and powered MS9024 validation remain open.

### F-029 — Failed sensor creation leaks static pool slot

- **Category/confidence:** confirmed defect / medium.
- **Evidence:** `temp_sensor_device_core.c:temp_sensor_create` sets `allocated=true` before `device_manager_create_device`; error macro returns without clearing the slot.

### F-030 — Modbus shutdown retains deleted handle

- **Category/confidence:** confirmed latent defect / medium.
- **Evidence:** `modbus_master_core.c:modbus_master_shutdown` deletes master but does not clear handle; request path lacks initialization-state validation.
- **Trigger:** shutdown/reinit or request after shutdown; current startup is boot-only.

## HMI, persistence, and configuration findings

### F-022 — Partial program read is accepted as complete

- **Category/confidence:** source fix implemented; regression and panel validation pending / high.
- **Evidence:** `nextion_file_reader.c:nextion_read_file` returns success when `total_received > 0` after later-chunk timeout; `nextion_storage.c:nextion_storage_parse_file_to_draft` replaces the draft and parses the partial prefix.
- **Source correction:** file reads now succeed only when the received count exactly matches the Nextion-reported size; storage parsing builds a temporary draft and atomically replaces the active draft only after the complete transfer is available. No Nextion-side change is required.
- **Residual risk:** UART fault-injection/read regression coverage and panel storage validation remain open; checksum/version protection is deferred because the existing file protocol is unchanged.

### F-023 — Persistent Nextion NAK blocks sole HMI worker

- **Category/confidence:** source fix implemented; regression and panel validation pending / high.
- **Evidence:** `nextion_storage.c` transfer loop `continue`s indefinitely on `0x04` NAK without retry/progress limit; save executes on sole HMI coordinator.
- **Source correction:** packet transfer now allows three retries for a NAK, then fails the save and exits through the existing cleanup path, releasing the UART mutex and clearing storage-active state. No Nextion-side change is required.
- **Residual risk:** UART fault-injection and independent HMI stop-path validation remain open; destructive replacement ordering remains tracked separately as F-024.

### F-024 — Program save is destructive before replacement succeeds

- **Category/confidence:** source fix implemented; panel and power-loss validation pending / high.
- **Evidence:** `nextion_storage.c:nextion_storage_save_program` deletes existing file before transfer; later timeout/NAK/power loss loses last good copy.
- **Source correction:** saves now transfer to a `.tmp` file, replace the destination with the panel-supported `refile` command only after the complete transfer, and read back the final file to verify exact payload contents. No Nextion-side change is required.
- **Residual risk:** the old file is still removed immediately before the final rename when overwriting, so power loss during that narrow replacement window remains a panel-storage limitation requiring validation; F-023 bounded NAK handling protects the transfer phase.

### F-026 — Factory reset does not enumerate stored programs

- **Category/confidence:** source fix implemented; panel-storage validation pending / medium.
- **Evidence:** `nextion_storage.c` delete-all walks a volatile session registry capped by coordinator max profiles (five) while storage advertises 200; registry is populated only by current-session saves/loads.
- **Impact:** after reboot, existing panel SD programs can survive a claimed factory reset.
- **Source correction:** the program registry now uses the configured `CONFIG_NEXTION_MAX_PROGRAMS` capacity, persists names in a dedicated NVS namespace, and restores them during HMI startup. Factory reset deletes the restored names before erasing NVS.
- **Residual risk:** the panel still has no serial directory enumeration in this implementation; files created or renamed outside the firmware registry remain outside the deletion set, and panel response/power-loss behavior requires validation.

### F-027 — Program deletion reports success without response validation

- **Category/confidence:** source fix implemented; panel-storage validation pending / medium.
- **Evidence:** `nextion_storage.c:nextion_storage_delete_program` sends `delfile`, delays, and returns true without checking panel result.
- **Source correction:** deletion now releases the transfer lock, verifies the file no longer exists through the panel read path, and only then refreshes the browser, removes the persistent registry entry, and returns success. Failed verification keeps the registry entry for retry.
- **Residual risk:** panel response timing and physical storage behavior remain unvalidated; a zero-length file is treated as absent by the existing file-existence helper.

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
- **Evidence:** `NEXTION_HMI_ENABLED` still has unconditional component/startup paths. The run-indicator path now honors its enable/valid-pin/collision state at `run_indicator_init`, although `main` retains the harmless unconditional call.
- **Next evidence:** clean builds with each feature disabled.

## Operational and maintainability findings

### F-011 — Coordinator timer failure can report/start inconsistent profile

- **Category/confidence:** source fix implemented; regression and hardware validation pending / high.
- **Evidence:** timer create/start errors in `start_heating_profile` do not fully unwind already loaded/profile/output state.
- **Source correction:** the coordinator now creates and starts the PID timer before releasing heater control inhibit or submitting `HEATER_CLEAR`/`HEATER_START`. Any timer create/start failure invokes the existing profile stop/unwind path and returns the timer error; heater authorization failure uses the same fail-closed cleanup.
- **Residual risk:** timer fault-injection and startup/stop regression coverage remain open; command submission and physical output behavior still require separate validation.

### F-014 — HMI bridge silently drops lifecycle/error telemetry

- **Category/confidence:** source fix implemented; regression validation pending / medium.
- **Evidence:** `hmi_coordinator.c` event bridges send to bounded queue with zero wait and ignore failure. In `6741c72`, a control fault posts separate `ERROR_OCCURRED` and `PROFILE_PAUSED` events; each bridge attempt is independently lossy. The transfer-time critical-event buffer is also bounded (eight entries).
- **Impact:** an operator can see a pause with no fault context, or an error with no corresponding paused state. A UI-side resume can therefore be based on incomplete information; the queued fault-off path must not rely on HMI delivery.
- **Source correction:** lifecycle and error commands now use front-of-queue delivery with a bounded 100 ms wait, ahead of telemetry, and every queue failure is logged. Temperature/status updates remain nonblocking best-effort telemetry.
- **Residual risk:** queue saturation can still lose a critical event after the bounded wait, and the transfer-time critical buffer remains bounded; queue-saturation and panel transfer validation remain open.

### F-015 — Natural completion leaves run indicator ON

- **Category/confidence:** resolved by production configuration; source behavior retained for the disabled test component / high when combined with F-016.
- **Evidence:** `run_indicator.c:run_indicator_event_handler` handles STOPPED but not COMPLETED.
- **Status:** the run-indicator component is excluded from the production build, so natural completion cannot drive a GPIO. Re-enabling the test component requires an explicit indication design and regression coverage.

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

- **Category/confidence:** source fix implemented; regression and powered validation pending / high control quality.
- **Evidence:** the former `temperature_profile_core.c:ramp_ease_position` curve used `q=f0+w*(u+u²-u³)` in the final ease band. Relative to linear interpolation, `q-linear = w*u²*(1-u)` was positive for all interior points; its rate multiplier `1+2u-3u²` peaked at 4/3. This matched the observed ramp-to-hold overshoot behavior.
- **Source correction:** the final band now uses a monotonic quadratic ease-out whose physical rate starts at the configured ramp rate and decreases to zero. The band takes twice its linear time, so the runtime planned duration is extended by one linear band duration instead of silently exceeding the configured rate.
- **Residual risk:** the controller/PID and chamber thermal response still require real-world validation; graph rendering remains linear and may not match the runtime trajectory.

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

- **Category/confidence:** source fix implemented; executable regression coverage pending.
- **Evidence:** PID now rejects non-finite input/state/output, resets, and returns zero; the heater setter clears a non-finite command and the PWM reader treats a non-finite stored target as zero. `ms9024_read_float` already rejects decoded NaN/Inf and out-of-range values, so the remaining normal-path risk is lower than originally stated.
- **Residual risk:** finite-but-stale values and cross-task temperature ownership remain F-001/F-021; zero demand still follows the existing output command path.
- **Validation:** [BUG-053-nonfinite-control-input.md](BUG-053-nonfinite-control-input.md) and [CHANGE-VALIDATION-F53.md](CHANGE-VALIDATION-F53.md).

## Missing tests

- **F-039 — missing test:** no project host test suite or ESP-IDF unit-test component was found for PID/profile, parsers, validation, or failure paths.
- **F-040 — missing test:** no automated alternate-Kconfig/effective-configuration build matrix validates optional features, pin collisions, queue depths, timing bounds, source defaults versus upgrade configuration, or incompatible feedforward/adaptive settings.
- **F-041 — missing test:** no recorded device-bench or powered-controller validation covers boot/reset GPIO states, sensor loss, Modbus timeout, queue saturation, pause/stop races, watchdog reset, and independent output removal.
- **F-052 — missing test:** no deterministic coverage exists for eased-ramp shape/graph parity, handover tolerance, stall lag/rate, sustained hold deviation, PID reset/feedforward/anti-windup, non-finite control inputs, or coordinator error-payload delivery.

Deferred test, build-profile, production-logging, and per-board configuration work is tracked in [FUTURE_WORK_ITEMS.md](FUTURE_WORK_ITEMS.md); it is not part of the current hardening scope.

## Unresolved questions

- **F-042:** What are the deployed GPIO active polarities, external pull states, schematic-approved indicator pin, and reset behavior?
- **F-043:** Is there an independent hardwired overtemperature cutoff, airflow proof, or safety contactor circuit?
- **F-044:** What exact MS9024 revision/register widths/float word order and Nextion file-transfer guarantees apply?
- **F-045:** What are resolved production task priorities/stacks, ESP-IDF watchdog/brownout settings, and measured high-water marks?
- **F-046:** Does the deployed Nextion project enforce stronger command framing than the parser, and is the `.HMI` source available?
- **F-047:** What physical output state and time-to-de-energize occur during WDT, software restart, brownout, and power cycling?

## Recommended first hardening batch (do not fix in this preparation pass)

These five are independent, source-confirmed, testable with low architectural risk. They reduce incorrect control/data behavior while the larger actuator-inhibit and fresh-sample architecture is decided:

1. **F-048:** characterize the corrected soft-landing trajectory and graph parity.
2. **F-005:** validate compact successful temperature samples with sparse-success batch tests.
3. **F-022:** validate complete-read rejection and atomic draft replacement.
4. **F-025:** verify full register width for MS9024 writes.
5. **F-050:** bound service operational-time override and propagate NVS write failures.

Before any powered release, investigate/resolve F-016, then make an ADR for the direct actuator-inhibit/fresh-temperature gate needed by F-017/F-001/F-002/F-003/F-004. Those are higher risk but cross multiple current boundaries and should not be patched piecemeal.

## Existing build evidence

ESP-IDF 5.5.4 build completed on pulled commit `16467e0` on 2026-07-13 using `IDF_PATH=/home/vesko/.espressif/v5.5.4/esp-idf cmake --build build -j2`: application `0x614f0` bytes, 62% of the smallest 1 MiB app partition free. The build used the retained local `sdkconfig`, not the new Kconfig defaults (F-049). Warnings included discarded `const` in logger CLI code, logger-storage declaration issues, unused legacy MAX31865 parser, and an impossible unsigned `< 0` check in health monitoring. Build evidence does not validate hardware behavior.
