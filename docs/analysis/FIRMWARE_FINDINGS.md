# Firmware findings register

Date: 2026-07-15. Scope: read-only static architecture, safety, concurrency, lifecycle, integration, control, persistence, and configuration analysis. No hardware was operated. Line numbers are navigation hints; symbols/current source are authoritative.

Categories: **confirmed defect** has a reachable source-level failure; **highly likely defect** has strong evidence with one runtime/config dependency; **possible defect** needs more evidence; **design weakness**, **maintainability issue**, **missing test**, and **unresolved question** are not claimed defects.

Source reconciliation on 2026-09-15 added F-069–F-078 without changing
firmware. D/F broad reviews were interrupted after bounded source evidence;
F-072/F-073 were independently checked at their exact trigger boundaries.
The user confirmed the operating oven has no independent hardware
safety protections; the ESP32 checks are not certified safety functions. All
GPIO/off claims here are source requests, not physical validation.

## Priority summary

| Rank | ID | Category | Risk | Summary |
| ---: | --- | --- | --- | --- |
| 1 | F-016 | source fix implemented; validation pending | critical safety/configuration | Run indicator is disabled when disabled, invalid, or colliding with the contactor GPIO |
| 2 | F-017 | source fix implemented; validation pending | critical conditional safety | SSR GPIO failure now latches local inhibit and requests contactor-off; electrical behavior is unverified |
| 3 | F-001/F-054 | source fix implemented; validation pending | critical control safety | A one-shot aggregate expiry timer directly inhibits output when the update stream stops |
| 4 | F-070 | confirmed source-level defect; correction not authorized | critical conditional output safety | Manual live cooling can retain positive PWM demand; ordinary cooling off is queued |
| 5 | F-002 | source fix implemented; validation pending | critical safety/concurrency | Dispatcher can deadlock before queued heater-off commands execute |
| 6 | F-003 | source fix implemented; validation pending | critical safety/concurrency | Heater-side control inhibit rejects stale PID output after pause/stop |
| 7 | F-055 | source fix implemented; regression pending | high lifecycle | Coordinator stop is queue-independent; queue-full stop/restart validation remains pending |
| 8 | F-004 | policy-adjusted; validation pending | high control | Sensor disagreement is intentionally a warning; fresh-sample quorum now gates control input |
| 9 | F-018 | source fix implemented; validation pending | high reset safety | Restart/factory-reset paths now fail closed unless direct heater inhibit succeeds |
| 10 | F-019 | source fix implemented; validation pending | high startup/control | Profile start now creates the control task before queuing contactor START |
| 11 | F-048 | source fix implemented; validation pending | high control | Ramp-to-hold easing now decelerates without leading the configured ramp rate |
| 12 | F-053 | source fix implemented; regression pending | high conditional control safety | Non-finite PID values are reset/forced to zero before heater PWM conversion |
| 13 | F-021 | source fix implemented; validation pending | high concurrency/control | Coordinator temperature now uses a mutex-protected snapshot |
| 14 | F-009 | source fix implemented; validation pending | high lifecycle | Heater teardown now joins its worker; stop/restart validation remains open |
| 15 | F-056 | source fix implemented; regression pending | medium lifecycle | Temperature-processor shutdown now destroys owned sensor devices after worker exit; device-manager mutation synchronization remains open |
| 16 | F-057 | confirmed defect; correction not authorized | high conditional startup safety/lifecycle | Heater partial-initialization cleanup is skipped, retaining resources and possibly a dispatcher handler |
| 17 | F-058 | confirmed defect; correction not authorized | high lifecycle/control-input availability | Temperature processor allows a duplicate initialization to orphan the first sensor set |
| 18 | F-059 | confirmed defect; correction not authorized | high lifecycle/conditional command safety | Coordinator partial initialization can retain a dispatcher handler after context teardown |
| 19 | F-060 | confirmed defect; correction not authorized | high control-input concurrency | Temperature cache fields race between device-manager update and processor aggregation |
| 20 | F-061 | confirmed defect; correction not authorized | high lifecycle | A STOP wake can let the control worker submit one final command before exiting |
| 21 | F-062 | confirmed defect; correction not authorized | high lifecycle | Dispatcher shutdown frees its queue/context without quiescing admitted external producers |
| 22 | F-063 | confirmed defect; correction not authorized | high sensor lifecycle/control-input integrity | Temperature-processor teardown can destroy/reuse a sensor while device manager updates it |
| 23 | F-064 | confirmed defect; correction not authorized | medium dormant lifecycle | Debug-console shutdown deinitializes state before its task exits |
| 24 | F-065 | confirmed defect; correction not authorized | high disabled-feature lifecycle | Health-monitor shutdown can free its context while its worker still uses it |
| 25 | F-066 | confirmed defect; correction not authorized | high disabled-feature lifecycle | Health-monitor partial initialization retains an event callback/context on task-init failure |
| 26 | F-067 | confirmed defect; excluded legacy code | medium legacy lifecycle | The excluded SPI temperature monitor has double-free and no worker-join paths |
| 27 | F-068 | confirmed configuration gap; correction not authorized | medium operational persistence | Resolved partition table lacks the logger's required `storage` LittleFS partition |
| 28 | F-022 | source fix implemented; validation pending | high persistence/control | Incomplete Nextion file reads are rejected before draft replacement |
| 29 | F-023 | source fix implemented; validation pending | high operational/safety access | Repeated NAKs now abort storage transfer instead of blocking the sole HMI worker |
| 30 | F-024 | source fix implemented; validation pending | high persistence/control | Program replacement now uses a completed temporary file; final rename power-loss behavior remains unverified |
| 31 | F-049–F-052 | mixed below | medium/high | Persistence, release configuration, and test backlog |
| 32 | F-069 | confirmed source-level defect; correction not authorized | high cure-cycle availability/control | Sensor-paused run cannot auto-resume after sensor recovery because its control inhibit remains |
| 33 | F-071 | confirmed source-level defect; correction not authorized | high conditional control-input safety | Failed scans can renew aggregate lease and recovery credit from one cached acquisition |
| 34 | F-072 | confirmed source-level defect; correction not authorized | high conditional crash/STOP lifecycle | STOP frees profile singleton before control worker acknowledges exit |
| 35 | F-073 | confirmed source/config defect; correction not authorized | high conditional active-HMI crash | Factory-reset delete-all frame exceeds configured HMI coordinator task stack |
| 36 | F-074 | confirmed source-level race; correction not authorized | critical conditional output safety | An admitted SSR/contactor high write can follow a completed direct inhibit |
| 37 | F-075 | confirmed source-level backpressure; correction not authorized | critical conditional off latency | Blocking heater telemetry can stall PWM or contactor-off after an SSR write |
| 38 | F-076 | confirmed source-level defect; correction not authorized | high cure-cycle integrity | A valid downward cooling step can trip the generic positive overshoot guard |
| 39 | F-077 | confirmed conditional off-error path; correction not authorized | critical fault response | Direct inhibit GPIO-off failure is not latched and pause/STOP can report success |
| 40 | F-078 | confirmed source-level status defect; correction not authorized | high command/cure integrity | RESUME can publish success while permanent heater output inhibit remains active |

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
- **Residual risk:** physical persistence, contactor polarity/independence and GPIO-mutex latency remain hardware/architecture-dependent. F-077 covers direct inhibit/contactor-low errors not latched by this PWM/CLEAR SSR handler; F-074 covers an admitted high write after inhibit and F-075 covers off-path telemetry blocking. The source correction does not close those separate boundaries.
- **Validation:** [BUG-017-ssr-off-failure.md](BUG-017-ssr-off-failure.md), [CHANGE-VALIDATION-F17.md](CHANGE-VALIDATION-F17.md), and [F17-HARDWARE-VALIDATION.md](F17-HARDWARE-VALIDATION.md).

### F-001 — Control accepts nonexistent or stale temperature

- **Category/confidence:** source fix implemented; deterministic regression and hardware validation pending / critical control safety.
- **Evidence:** sensor devices record successful-read ticks; the processor compacts accepted caches and publishes a validity snapshot; coordinator invalid snapshots directly set heater sensor-data inhibit and pause the profile; a coordinator-owned one-shot timer expires each accepted *aggregate* at its stamped-age deadline and directly asserts the sensor inhibit before waking the control task. Three accepted validity events release only that recoverable sensor inhibit; they do **not** automatically resume an ordinary sensor-paused run (F-069).
- **Residual risk:** F-071 shows aggregate processing time can renew the apparent age of an older acquisition and failed scans can count toward recovery. Quorum is temporarily sensor-count-minus-two; expiry timing, GPIO polarity, electrical isolation, and physical output latency remain unverified. F-003's high-write ordering residual needs concurrency review.
- **Validation:** [BUG-001-temperature-freshness.md](BUG-001-temperature-freshness.md), [CHANGE-VALIDATION-F001-PHASE-A.md](CHANGE-VALIDATION-F001-PHASE-A.md), [CHANGE-VALIDATION-F001-PHASE-B.md](CHANGE-VALIDATION-F001-PHASE-B.md), [BUG-054-temperature-update-silence.md](BUG-054-temperature-update-silence.md), and [CHANGE-VALIDATION-F054.md](CHANGE-VALIDATION-F054.md).

### F-069 — Sensor recovery cannot resume a sensor-paused run

- **Category/confidence:** confirmed source-level defect; correction not authorized / high cure-cycle availability and control.
- **Reachable trigger:** an invalid aggregate or timer expiry pauses a running profile and sets both sensor-data and control heater inhibits. After the configured three validity events, the coordinator releases only the sensor inhibit, then tests the *combined* inhibit before calling the routine that would clear control inhibit. That test remains false and the run stays paused.
- **Invariant/uncertainty:** confirmed user policy `POL-SENSOR-RECOVERY-001` requires automatic resume after trustworthy sensor recovery, without overriding operator pause or permanent faults. F-071 leaves recovery qualification itself unresolved; field image and timing are unverified.
- **Report/test:** [BUG-069-sensor-recovery-pause-gate.md](BUG-069-sensor-recovery-pause-gate.md), `SCN-W1-002` in [current behavior](CURRENT-CONTROLLER-BEHAVIOR.md). No patch or test ran.

### F-070 — Cooling can retain positive heater demand

- **Category/confidence:** confirmed source-level defect; correction not authorized / critical conditional output-safety gap.
- **Reachable trigger:** in a positive-demand manual run, a live target decrease >2 °C below current temperature changes profile phase to COOLING without `stage_changed`. Coordinator's cooling `kill_heater` is conditioned on that flag; COOLING then skips PID/SET_POWER while the old positive target remains in PWM and contactor stays requested high. Ordinary stage cooling queues off commands and can be delayed by a full dispatcher queue.
- **Invariant/uncertainty:** a no-heat phase must revoke positive output authorization; current source requests are not measured heater power. The operating oven has no independent hardware protection. Heater high-write/inhibit ordering (F-074) is a separate boundary before final-off can be claimed.
- **Report/test:** [BUG-070-cooling-output-authorization.md](BUG-070-cooling-output-authorization.md), `SCN-W1-001` in [current behavior](CURRENT-CONTROLLER-BEHAVIOR.md). No patch, test, or hardware validation ran.

### F-071 — Failed scans renew aggregate age and recovery credit

- **Category/confidence:** confirmed source-level defect; correction not authorized / high conditional control-input safety.
- **Reachable trigger:** a successful read is followed by failed scans while its cache age is still ≤5 s. Scan-complete events still process that cache and stamp new aggregate ticks; coordinator grants another ≤5 s lease and increments recovery per validity event rather than per new acquisition. One acquisition can approach 10 s of accepted physical age and satisfy three recovery events.
- **Invariant/uncertainty:** acquisition time/identity must be explicit for control freshness and recovery. Product quorum, scan-skew, and distinct-read qualification still need decisions; total update silence is separately mitigated by F-054.
- **Report/test:** [BUG-071-aggregate-acquisition-freshness.md](BUG-071-aggregate-acquisition-freshness.md), `SCN-W1-004` in [current behavior](CURRENT-CONTROLLER-BEHAVIOR.md). No patch or test ran.

### F-072 — STOP destroys profile state before worker exit

- **Category/confidence:** confirmed source-level interleaving; correction not authorized / high conditional crash and incomplete STOP.
- **Reachable trigger:** preempt the control worker inside `profile_tick` after its singleton non-null check but before the separate singleton program dereference. Dispatcher STOP then requests direct inhibit, frees the singleton/resets tick state, and only afterward waits for worker exit. The worker can resume into null/freed state.
- **Boundary/uncertainty:** F-061 covers a STOP-woken final command and queue-full join; F-072 is distinct destroy-before-join failure even for a tick entered before STOP. The operating oven has no independent hardware protection; direct GPIO-off request is not physical evidence.
- **Report/test:** [BUG-072-profile-stop-destroy-before-join.md](BUG-072-profile-stop-destroy-before-join.md), `SCN-D-001` in [current behavior](CURRENT-CONTROLLER-BEHAVIOR.md). No patch or test ran.

### F-073 — Factory-reset frame exceeds HMI worker stack

- **Category/confidence:** confirmed source/configuration trigger with historical compiled-frame corroboration; correction not authorized / high conditional active-feature crash.
- **Reachable trigger:** enabled Nextion `factory_reset_confirm` reaches delete-all on its 8,192-byte coordinator task stack. The function's automatic 200×64-byte names array is 12,800 bytes, and the stored object frame is 12,896 bytes. Empty registry still enters that frame; stack canaries detect, not prevent, overflow.
- **Boundary/uncertainty:** heater direct inhibit is requested before delete-all but physical off/reset state is unverified. F-018 covers reset output response and F-026 covers registry enumeration, not this stack budget.
- **Report/test:** [BUG-073-nextion-factory-reset-stack.md](BUG-073-nextion-factory-reset-stack.md), `SCN-F-001` in [current behavior](CURRENT-CONTROLLER-BEHAVIOR.md). No new build, patch, test, or hardware action ran.

### F-074 — Admitted high output write can follow direct inhibit

- **Category/confidence:** confirmed source-level interleaving; correction not authorized / critical conditional output-safety race.
- **Reachable trigger:** PWM, HEATER_START, and positive TOGGLE check the combined heater inhibit under `power_mutex`, release it, then write SSR/contactor high. An inhibit can set flags and finish direct SSR-low/contactor-low between checks and those high writes; GPIO's mutex serializes individual writes, not admission plus write.
- **Boundary/uncertainty:** F-003's historical source fix rejects new checks after pause, but not a high writer admitted before it. F-075's blocking telemetry must be separated before any check/write lock plan is safe. No electrical outcome is measured; the operating oven has no independent hardware cutoff.
- **Report/test:** [BUG-074-heater-high-write-after-inhibit.md](BUG-074-heater-high-write-after-inhibit.md), `SCN-W1-003` in [current behavior](CURRENT-CONTROLLER-BEHAVIOR.md). No patch/test/hardware action ran.

### F-075 — Blocking telemetry delays off response

- **Category/confidence:** confirmed source-level full-queue trigger; correction not authorized / critical conditional off-latency gap.
- **Reachable trigger:** `toggle_heater` writes SSR then posts `HEATER_TOGGLED` using `portMAX_DELAY` to the private 32-entry event queue. A non-event-loop PWM task can block with SSR-high; a direct inhibit can block after SSR-low but before contactor-low when that queue is full and its consumer stalled. Same-loop ESP-IDF posts use zero wait, so no self-post deadlock is claimed.
- **Invariant/uncertainty:** output-off requests must not wait on diagnostics capacity. Source has no bounded contactor-off deadline here; GPIO polarity and physical latency remain unknown.
- **Report/test:** [BUG-075-heater-telemetry-blocks-off.md](BUG-075-heater-telemetry-blocks-off.md), `SCN-W1-005` in [current behavior](CURRENT-CONTROLLER-BEHAVIOR.md). No patch/test/hardware action ran.

### F-076 — Cooling target triggers false overshoot completion

- **Category/confidence:** confirmed source-level valid-program trigger; correction not authorized / high cure-cycle integrity.
- **Reachable trigger:** a validated 150→100 °C cooling stage uses 100 °C as setpoint while chamber is still 150 °C. Generic `actual > setpoint + 30 °C` guard runs in cooling, so the next tick threshold-exceeds/emergency-completes an ordinary downward step.
- **Invariant/uncertainty:** legitimate cooling is not runaway solely because actual exceeds a lower target; a genuine direction-aware/absolute high-temperature response remains to be specified. Current hardware has no independent protection; false trip is output-off/cure interruption, not observed heating.
- **Report/test:** [BUG-076-cooling-overshoot-guard.md](BUG-076-cooling-overshoot-guard.md), `SCN-W1-006` in [current behavior](CURRENT-CONTROLLER-BEHAVIOR.md). No patch/test/hardware action ran.

### F-077 — Direct off-write failure is not a persistent output fault

- **Category/confidence:** confirmed conditional GPIO-error path; correction not authorized / critical fault-response gap.
- **Reachable trigger:** inject persistent contactor-low errors through invalid-input sensor inhibit and subsequent pause control inhibit while SSR-low succeeds. Both setters return error without fault latch; coordinator `void` wrapper only logs, pause reports success. Qualified recovery and explicit RESUME can release normal reasons, admitting positive PWM despite no confirmed contactor-low request. Queued CLEAR retries SSR only.
- **Invariant/uncertainty:** required off-write failure must not be treated as normal recoverable pause/STOP; error must persist as a blocking fault with truthful outcome. GPIO return error is fault-injection evidence, not proof of a physically stuck contactor; no independent hardware cutoff exists today.
- **Report/test:** [BUG-077-direct-inhibit-off-failure.md](BUG-077-direct-inhibit-off-failure.md), `SCN-W3-001` in [current behavior](CURRENT-CONTROLLER-BEHAVIOR.md). No patch/test/hardware action ran.

### F-078 — RESUME reports success with permanent output fault active

- **Category/confidence:** confirmed source-level status/command defect; correction not authorized / high cure-cycle integrity.
- **Reachable trigger:** PWM/CLEAR SSR GPIO error latches heater `output_inhibited`. Operator PAUSE then RESUME clears only temporary control reason; its setter returns success even though permanent reason remains. Coordinator marks unpaused and publishes RESUMED while heater rejects heat.
- **Invariant/uncertainty:** command result/process status must reflect authoritative output authorization. This does not claim re-energization; fault-state/external-HMI semantics and field image remain open.
- **Report/test:** [BUG-078-resume-under-permanent-output-fault.md](BUG-078-resume-under-permanent-output-fault.md), `SCN-W3-002` in [current behavior](CURRENT-CONTROLLER-BEHAVIOR.md). No patch/test/hardware action ran.

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
- **Residual risk:** this is intentionally a narrow hardening gate, not a command-generation protocol. F-074 records a high write admitted before inhibit that can follow its completed off writes; F-075 records blocking telemetry inside the off path. Cooling without a direct gate is F-070. Queue ordering, resume behavior, and physical output latency still require regression and powered-controller validation; the historical narrow fix status does not close those residuals.

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
- **Residual risk:** normal shutdown does not call the existing device-destruction helper, leaving registered sensor devices in the device manager and static pool after the processor context is freed (F-056). Event-manager unsubscribe and device-manager producer shutdown remain broader lifecycle concerns; executable stop/restart coverage is still missing.

### F-055 — Coordinator shutdown can deadlock on a full dispatcher queue

- **Category/confidence:** source fix implemented; regression validation pending / high lifecycle; physical-off path remains independently asserted.
- **Evidence:** `coordinator_component_heater_controller.c:stop_heating_profile` waits indefinitely for the control-worker exit acknowledgement. The control worker always calls the same function while exiting, which sends `HEATER_CLEAR` and `HEATER_STOP`. From that worker, `commands_dispatcher_dispatch_command` uses `xQueueSend(..., portMAX_DELAY)` because only the dispatcher task takes the inline re-entrant path.
- **Reachable trigger:** fill the bounded dispatcher queue, then let the dispatcher execute an external STOP command. The dispatcher waits in `stop_heating_profile` for the control worker; the worker's self-exit attempts to enqueue its two heater commands and blocks because the dispatcher, the sole consumer, is waiting for that acknowledgement.
- **Impact:** the control-worker acknowledgement never arrives, so stop/restart/shutdown hangs. The current path asserts the direct heater control inhibit before waiting, so this interleaving does not by itself re-energize the SSR/contactor; it still defeats the claimed complete lifecycle teardown.
- **Source correction:** `stop_heating_profile` now submits no dispatcher-backed heater commands for either external STOP handling or worker self-exit. The direct control inhibit clears the target and de-energizes the SSR and contactor before cleanup, and the next profile start reinitializes heater command state.
- **Residual risk:** executable queue-full stop/restart coverage is still missing, and other external producers can remain blocked in unbounded dispatcher sends during global shutdown.
- **Validation:** a mocked FreeRTOS queue-full stop test must prove bounded completion and verify SSR/contactor-off requests occur before the wait.

### F-061 — STOP wake can produce one final control tick

- **Category/confidence:** confirmed defect; correction not authorized / high lifecycle. This is a recurrence edge of F-055, not a claim of physical re-energization.
- **Evidence/trigger:** `stop_heating_profile` sets `running=false` and wakes the control worker before applying the direct control inhibit (`coordinator_component_heater_controller.c:850-880`). The worker tests `running` only before `ulTaskNotifyTake` and does not retest immediately after wake (`:325-327`), so it can run `profile_tick` and submit `SET_POWER` before reaching its self-exit acknowledgement (`:358-547`). If an external producer refills the bounded dispatcher queue after STOP is dequeued but before that submission, the worker blocks and the dispatcher waits for its acknowledgement.
- **Impact/direction:** stop/restart can again hang. The direct inhibit is asserted before the dispatcher begins its join, so the source still requests output-off. Assert inhibit before the wake and have a wake after `running=false` exit without control/dispatcher work; add a deterministic queue-refill STOP test. See [AUDIT-005](AUDIT-005-pid-profile-control.md).

### F-062 — Dispatcher shutdown does not quiesce external producers

- **Category/confidence:** confirmed defect; correction not authorized / high lifecycle, with conditional command-safety relevance.
- **Evidence/trigger:** an external caller can pass the context/running check in `commands_dispatcher_dispatch_command` and later dereference the global queue for an unbounded send (`commands_manager_core.c:77-105`). Shutdown joins only the consumer, then deletes the queue/semaphore and frees that context (`:112-149`). Pausing a producer after admission, completing shutdown, then resuming it reaches freed state.
- **Impact/direction:** use-after-free or unbounded producer shutdown. Define a single lifecycle/admission protocol that rejects new producers, waits for admitted ones to exit, then destroys resources. See [AUDIT-006](AUDIT-006-dispatcher-device-lifecycle.md).

### F-063 — Sensor destruction races device-manager update

- **Category/confidence:** confirmed defect; correction not authorized / high sensor-lifecycle and control-input integrity.
- **Evidence/trigger:** temperature-processor shutdown joins only its own worker, then destroys its sensors while device manager may already be in `temp_sensor_update` (`temperature_processor_core.c:80-108`; `device_manager_task.c:36-62`). Destruction clears/reuses the static entry without a shared protocol, while an update can return from a blocking Modbus read and write it (`temp_sensor_device_core.c:67-79, 178-198`).
- **Impact/direction:** an old transaction can write into a destroyed or replacement logical sensor. Use manager-owned removal or quiesce the manager before reuse, with a deterministic blocked-Modbus interleaving test. See [AUDIT-006](AUDIT-006-dispatcher-device-lifecycle.md).

### F-064–F-067 — Dormant and legacy lifecycle defects

- **F-064:** debug-console shutdown immediately deinitializes console state without waking/joining the linenoise task; shutdown/reinit can leave the old task active.
- **F-065:** if enabled, health-monitor shutdown frees context after merely notifying its worker; the worker still dereferences it during exit.
- **F-066:** if enabled, health-monitor task/watchdog initialization failure after event subscription retains the callback/context and retry overwrites the global pointer.
- **F-067:** the excluded legacy SPI temperature monitor has double-free partial cleanup and frees task-owned state without joining its worker.
- **Boundary:** none is a current production heater-output claim. F-064 is reachable through its public shutdown/reinit API; F-065/F-066 need the currently disabled monitor enabled; F-067 needs explicit legacy-component re-enablement. Each needs a one-owner lifecycle and acquisition-boundary fault-injection before activation. See [AUDIT-007](AUDIT-007-operations-diagnostics.md).

### F-068 — Logger storage partition is not selected

- **Category/confidence:** confirmed configuration gap; correction not authorized / medium operational persistence.
- **Evidence/trigger:** the tracked resolved configuration selects ESP-IDF's standard single-app partition table, which has no `storage` label; `logger_init` always attempts to mount LittleFS by that literal label and continues after failure. The repository `partitions.csv` does define `storage`, but is unselected and assumes 8 MiB despite the tracked 2 MiB flash setting.
- **Impact/direction:** persistent logger/crash-record storage is unavailable under the tracked configuration. Establish one deployable flash/partition contract and clean-configure/boot-validate it before relying on storage. See [AUDIT-008](AUDIT-008-configuration-build.md).

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

### F-057 — Heater partial initialization bypasses cleanup

- **Category/confidence:** confirmed defect; correction not authorized / high conditional startup safety and lifecycle.
- **Evidence:** `heater_controller_core.c:init_heater_controller_component` creates context resources and registers the heater command handler before assigning `initialized=true`; all pre-completion failures call `shutdown_heater_controller_component`, whose guard returns whenever `initialized` is false. Therefore the allocation, created mutex/semaphore, and any registered handler remain live on every such failure.
- **Reachable trigger:** fault-inject a failure after any successful acquisition and before the final flag assignment (for example task creation), then retry component initialization. The retry reuses the retained context and overwrites existing resource fields; a handler registered before the failure remains associated with the partial context.
- **Impact:** partial initialization violates the required reverse-order cleanup protocol. Source does not establish an explicit complete off/unwind path for every partial GPIO state. Physical actuator impact is conditional on unresolved pin polarity/reset/external-isolation facts and active post-failure command producers; it is not claimed as hardware-observed.
- **Direction:** use explicit partial ownership state or teardown safe for any allocated context; unregister the command handler before freeing its context and explicitly request off for configured outputs. Do not deinitialize the shared GPIO driver without an ownership decision.
- **Validation:** [BUG-057-heater-partial-init-cleanup.md](BUG-057-heater-partial-init-cleanup.md). Add deterministic acquisition-boundary fault injection, then an authorized powered-controller partial-init validation after a correction.

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

### F-056 — Temperature-processor shutdown leaves registered sensor devices active

- **Category/confidence:** source fix implemented; regression validation pending / medium lifecycle.
- **Evidence:** `temperature_processor_core.c:shutdown_temp_processor` unsubscribes, stops and joins the worker, then frees `g_temp_processor_ctx` without calling `destroy_devices`. That helper is used only on initialization/task-start failures and is the only path that calls `temp_sensor_destroy` for the processor's registered sensors.
- **Reachable trigger:** initialize the temperature processor successfully, then call its normal shutdown and initialize it again. The previous `device_manager` entries and static `temp_sensor_device` pool slots remain allocated/running even though the processor context that referenced them has been freed.
- **Impact:** restart can exhaust device-manager or sensor-pool capacity, and the device manager can continue updating orphaned sensors. This is a lifecycle/resource correctness defect; it does not alone command heater-on.
- **Direction:** after the worker acknowledges exit and before freeing the processor context, destroy each owned sensor device and clear its pointer. Establish the intended order with device-manager shutdown/restart so no worker accesses a device during destruction.
- **Source correction:** normal shutdown now calls `destroy_devices` after the processor worker acknowledgement and before freeing the context.
- **Residual risk:** device-manager table mutation has no documented lock; executable stop/reinit coverage and a device-manager lifecycle contract remain pending.
- **Validation:** a stop/reinit regression must verify device-manager count and sensor-pool allocation return to their pre-init values, then prove the same number of sensors can be initialized again.

### F-058 — Temperature processor allows duplicate initialization

- **Category/confidence:** confirmed defect; correction not authorized / high lifecycle and control-input availability.
- **Evidence:** `temperature_processor_core.c:init_temp_processor` guards initialization with file-static `processor_running`, but that variable is never written. The active worker uses the distinct context atomic `processor_running`. A second call therefore re-runs `init_devices`, overwrites the live context’s sensor pointers with newly allocated static-pool entries, and leaves the existing worker running because `start_temp_processor_task` sees its non-null handle.
- **Reachable trigger:** initialize successfully, then call `init_temp_processor` again without shutdown. The first sensor set remains registered in device manager but is no longer reachable through the context array; later shutdown destroys only the second set.
- **Impact:** repeated initialization can exhaust the sensor pool/device table and mutates a live sensor array while the processor worker may read it. The direct sensor-data inhibit remains fail-closed when valid aggregates disappear, so no immediate heater-on behavior is claimed.
- **Direction:** make one context-owned atomic/handle state the sole initialization guard; reject duplicate active init before any mutation and characterize device-manager mutation synchronization.
- **Validation:** [BUG-058-temperature-processor-reinit-guard.md](BUG-058-temperature-processor-reinit-guard.md). Add duplicate-init and stop/reinit count/pool tests.

### F-060 — Temperature sensor cache is read and written concurrently

- **Category/confidence:** confirmed defect; correction not authorized / high control-input concurrency.
- **Evidence:** device-manager task invokes `temp_sensor_update`, which writes ordinary `last_temperature`, `last_update_tick`, and `has_successful_sample` fields. Temperature-processor task reads those same fields via its cache accessors/`temp_sensor_read`. The only cross-task mechanism is a notification after a manager update cycle; no mutex, atomics, or coherent snapshot protocol exists.
- **Reachable trigger:** delay the event-loop callback or processor after a device-manager update notification until a later manager cycle begins. The manager then writes a cache while the processor aggregates it; equal task priorities and no affinity/exclusion guarantee permit the interleaving.
- **Impact:** C data-race behavior can produce a mixed temperature/tick/validity tuple used in aggregate quorum and control input. F-001/F-054 expiry does not prove an accepted racy tuple coherent.
- **Direction:** transfer a whole immutable sample or protect the complete cache snapshot under one ownership/synchronization protocol; do not use `volatile` as a fix.
- **Validation:** [BUG-060-temperature-cache-cross-task-race.md](BUG-060-temperature-cache-cross-task-race.md). Add a mocked interleaving test that rejects mixed tuples and retains invalid/stale inhibit behavior.

### F-059 — Coordinator partial initialization can retain a dispatcher handler

- **Category/confidence:** confirmed defect; correction not authorized / high lifecycle and conditional command safety.
- **Evidence:** `init_coordinator_events` registers `COMMAND_TARGET_COORDINATOR` before subscribing to temperature events, but sets `events_initialized` only after both calls succeed. If subscription fails, `init_coordinator` returns without calling `stop_coordinator`; later `shutdown_coordinator_events` skips unregistration because the flag is false, while `stop_coordinator` frees the context.
- **Reachable trigger:** fault-inject the temperature-event subscription after handler registration, then call coordinator stop or retry and later stop. The dispatcher registration can retain the freed context argument.
- **Impact:** timer/context/resource teardown is incomplete and a dispatcher command can target stale state. Normal startup returns before HMI startup on this init failure, so no normal-boot post-failure HMI command is claimed; physical impact is conditional and unverified.
- **Direction:** track handler registration, event subscription, and timer ownership independently; make teardown safe for partial context and retain the startup direct sensor-data inhibit.
- **Validation:** [BUG-059-coordinator-partial-init-cleanup.md](BUG-059-coordinator-partial-init-cleanup.md). Add acquisition-boundary fault injection and a post-teardown dispatcher-command safety oracle.

### F-054 — Valid temperature never expires when updates stop

- **Category/confidence:** source fix implemented; deterministic regression and hardware validation pending / critical control safety.
- **Evidence:** `temperature_processor_task.c` stamps and posts a validity sample only when notified by `DEVICE_MANAGER_UPDATED_EVENT`, then blocks indefinitely for the next notification. `coordinator_component_events.c` now records accepted aggregate ticks, arms a coordinator-owned one-shot `ESP_TIMER_TASK` timer for the remaining stale-age budget, and directly invokes the heater sensor-data inhibit when it expires.
- **Source correction:** expiry directly zeros demand and requests SSR/contactor-off through `heater_controller_set_sensor_data_inhibit(true)` before the coordinator task is woken to pause. Profile start also rejects an expired aggregate. A later fresh sample must still satisfy the existing recovery-count contract.
- **Violated invariant:** temperature is usable for control only while both validity and freshness are explicit. A plausible cached float is not fresh merely because no later failure event arrived.
- **Residual risk:** no executable valid-then-silent regression exists yet; `esp_timer` callback latency, GPIO polarity/electrical isolation, and physical de-energization timing require powered-controller validation.
- **Validation:** [BUG-054-temperature-update-silence.md](BUG-054-temperature-update-silence.md), [CHANGE-VALIDATION-F054.md](CHANGE-VALIDATION-F054.md), and [F001-F054-HARDWARE-VALIDATION.md](F001-F054-HARDWARE-VALIDATION.md).

### F-025 — MS9024 write verification compares only low byte

- **Category/confidence:** source fix implemented; regression and device validation pending / high.
- **Evidence:** `temp_sensor_device/src/ms9024.c:ms9024_write_and_verify` masks/compares low byte; repair writes 16-bit value 282 to register 129.
- **Source correction:** write verification and the adjacent auto-correct comparison now require the complete 16-bit readback to equal the desired value; diagnostics log both bytes.
- **Residual risk:** Modbus fault-injection and powered MS9024 validation remain open.

### F-029 — Failed sensor creation leaks static pool slot

- **Category/confidence:** source fix implemented; regression and sensor lifecycle validation pending / medium.
- **Evidence:** `temp_sensor_device_core.c:temp_sensor_create` sets `allocated=true` before `device_manager_create_device`; error macro returns without clearing the slot.
- **Source correction:** sensor creation now clears `allocated`, `valid`, and `device_handle` when device-manager creation fails before returning the original error.
- **Residual risk:** fault-injection and repeated create/destroy validation remain open.

### F-030 — Modbus shutdown retains deleted handle

- **Category/confidence:** source fix implemented; regression and Modbus validation pending / medium.
- **Evidence:** `modbus_master_core.c:modbus_master_shutdown` deletes master but does not clear handle; request path lacks initialization-state validation.
- **Trigger:** shutdown/reinit or request after shutdown; current startup is boot-only.
- **Source correction:** successful shutdown now clears `master_handle`; initialization rejects a second active instance, and the raw request path returns `ESP_ERR_INVALID_STATE` when no master exists.
- **Residual risk:** shutdown/reinit and request-after-shutdown validation remain open; physical RS-485 behavior is not claimed.

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

- **Category/confidence:** source fix implemented; regression validation pending / medium.
- **Evidence:** `coordinator_component_events.c` GET_STATUS/GET_CURRENT_PROFILE cases declare uninitialized stack objects and post them; status payload conflicts with the declared event type. No current in-tree producer was found.
- **Source correction:** status queries now construct and post an initialized `coordinator_status_data_t`; current-profile queries return index `0` when a program is loaded and `INVALID_PROFILE_INDEX` otherwise.
- **Residual risk:** query-event regression coverage and HMI consumer validation remain open; status queries intentionally report zero power/stage detail because the query command has no tick result context.

### F-032 — Destructive command routing uses substring matching

- **Category/confidence:** source fix implemented; regression and panel validation pending / medium.
- **Evidence:** `nextion_events.c` routes restart/factory-reset by `strstr`; malformed frames containing tokens could invoke destructive handlers. Reachability depends on panel protocol framing/caller guards.
- **Source correction:** restart, factory-reset request, and factory-reset confirmation now require an exact cleaned command string before invoking their destructive handlers.
- **Residual risk:** malformed-frame injection and the deployed panel's command framing still require validation; other non-destructive routes retain their existing substring-compatible protocol.

### F-033 — HMI numeric parsing lacks range/overflow checks

- **Category/confidence:** source fix implemented; regression validation pending / medium.
- **Evidence:** `nextion_parse_utils.c` uses `strtol` without `errno`/range validation; decimal accumulation can overflow signed integers.
- **Source correction:** `parse_int` now rejects conversion errors and values outside `int`; `parse_decimal_x10` bounds its accumulation and handles the signed `INT_MIN` endpoint without narrowing overflow.
- **Residual risk:** no parser test harness exists in the repository; accepted values still require downstream heating/profile range validation.

### F-034 — Enable Kconfig booleans are not honored by build/startup

- **Category/confidence:** source fix implemented; alternate-Kconfig validation pending / medium.
- **Evidence:** `NEXTION_HMI_ENABLED` had an unconditional `nextion_hmi_init()` call in `main.c` and an unconditional HMI source list in `components/nextion_hmi/CMakeLists.txt`; the run-indicator path is already guarded at `run_indicator_init` and is excluded from production.
- **Source correction:** `main.c` now starts Nextion only when enabled; disabled HMI builds compile only the public no-op init source, while enabled builds retain the complete HMI source set.
- **Residual risk:** a disabled-Kconfig build still needs to be generated and compiled; no hardware validation is relevant to the compile/startup gate.

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

- **Category/confidence:** deferred architecture work / high.
- **Evidence:** multiple publishers of `FURNACE_ERROR_EVENT`; no production subscription found. `error_manager` has no descriptor registration found.
- **Decision:** leave outside the current hardening pass. Revisit when the planned fault manager is introduced; that architecture must define error severity, authoritative heater inhibition, latching/recovery, duplicate/late-event handling, and ownership of physical mitigation.
- **Status:** intentionally deferred; no partial subscriber or replacement error-routing path is being added now.

### F-036 — Health/watchdog supervision is disabled and incomplete

- **Category/confidence:** deferred architecture work / high.
- **Evidence:** `init_health_monitor()` is commented in `main.c`; if enabled after existing producers start, prior registrations may be lost. Health failure stops watchdog reset but does not directly inhibit outputs.
- **Decision:** leave outside the current hardening pass. Revisit with the planned fault-manager/watchdog redesign after ownership, startup ordering, failure response, and physical-output mitigation decisions are made.
- **Status:** intentionally deferred; no partial watchdog or health-monitor activation is being added now.

### F-037 — Duplicate active/legacy temperature architectures

- **Category/confidence:** source fix implemented; legacy path retained but excluded from production / medium.
- **Evidence:** current Modbus/device/processor path is production; the SPI/MAX31865 monitor was only pulled into the build through `main/CMakeLists.txt` and was never started by `app_main`.
- **Source correction:** the project excludes `temperature_monitor_component` from ESP-IDF discovery and `main/CMakeLists.txt` no longer depends on it; its sources remain in the repository and are marked legacy for historical reference. The two configuration symbols still consumed by the active processor were moved to `temperature_processor_component/Kconfig` without changing their names or defaults.
- **Residual risk:** the legacy path is not covered by the production build; reviving it requires an explicit architecture/PCB decision and separate validation.

### F-038 — Device-manager count is not maintained

- **Category/confidence:** source fix implemented; lifecycle regression validation pending / low.
- **Evidence:** `device_manager_context_t.count` was checked but no increment/decrement was found; slot scan still enforced the physical array limit, making this misleading rather than overflowing.
- **Source correction:** successful slot reservation increments `count`; `device_manager_destroy` decrements it, including the cleanup path after failed device initialization, with an underflow guard.
- **Residual risk:** concurrent device registration/destruction remains outside this narrow bookkeeping fix and needs lifecycle coverage if those APIs become multi-task.

### F-048 — Cubic soft-landing accelerates before it decelerates

- **Category/confidence:** source fix implemented; regression and powered validation pending / high control quality.
- **Evidence:** the former `temperature_profile_core.c:ramp_ease_position` curve used `q=f0+w*(u+u²-u³)` in the final ease band. Relative to linear interpolation, `q-linear = w*u²*(1-u)` was positive for all interior points; its rate multiplier `1+2u-3u²` peaked at 4/3. This matched the observed ramp-to-hold overshoot behavior.
- **Source correction:** the final band now uses a monotonic quadratic ease-out whose physical rate starts at the configured ramp rate and decreases to zero. The band takes twice its linear time, so the runtime planned duration is extended by one linear band duration instead of silently exceeding the configured rate.
- **Residual risk:** the controller/PID and chamber thermal response still require real-world validation; graph rendering remains linear and may not match the runtime trajectory.

### F-049 — Control defaults, resolved build, and field-tuning documentation disagree

- **Category/confidence:** configuration baseline recorded; release/effective-configuration validation pending / high operational uncertainty.
- **Evidence:** the repository now tracks `sdkconfig.defaults`, copied from the reviewed resolved `sdkconfig`, including overshoot 30 C, stall check 180 s, PID Kd 0, fan 35/32 C, feedforward disabled, and service-time override disabled. Component Kconfig defaults and tuning notes still describe alternate values, so the tracked release baseline must remain the authoritative deployment configuration.
- **Impact:** fresh configurations, upgraded local configurations, and claimed field tuning can run materially different control and guard behavior. The successful build validates only the retained local configuration, not the changed defaults.
- **Source/configuration correction:** added the complete resolved configuration as `sdkconfig.defaults`; new configurations now have a version-controlled baseline for all ESP-IDF and project options.
- **Residual risk:** Kconfig help/default text and tuning documentation still need reconciliation with the chosen production baseline; effective-symbol/build verification and field/bench evidence remain pending.

### F-050 — Operational-time service override can wrap and misreport persistence

- **Category/confidence:** source fix implemented; regression validation pending / medium.
- **Evidence:** before correction, `components/nextion_hmi/src/program/Kconfig` gave `NEXTION_OP_TIME_OVERRIDE_HOURS` no range and `heating_program_models.c` cast it to `uint32_t` before multiplying by 3600. The enabled override still executes at every boot by documented service-tool design, but the source now bounds the value and checks NVS errors before replacing the loaded counter.
- **Impact:** service hours can be corrupted or falsely reported as restored, weakening maintenance records.
- **Source correction:** Kconfig now bounds the override to the uint32-safe hour range; runtime validation rejects invalid values; NVS open/set/commit errors are surfaced and leave the loaded counter unchanged. The documented enable-once/disable-after-correction workflow remains unchanged.
- **Residual risk:** no executable persistence regression exists; the override remains intentionally applied on every boot while enabled.
- **Validation:** [CHANGE-VALIDATION-F050.md](CHANGE-VALIDATION-F050.md).

### F-051 — New coordinator error payload has an unversioned compatibility contract

- **Category/confidence:** deferred design weakness / medium.
- **Evidence:** `coordinator_error_data_t` in `event_registry.h` grew from the prior code's small payload to temperature/setpoint/stage/elapsed fields, and gained `COORDINATOR_ERROR_HOLD_DEVIATION`. In-tree producers/consumers build, but independently compiled subscribers and fixed-size bridges have no version/size contract.
- **Direction:** state a payload compatibility policy or version the message, and add a producer/consumer copy-size regression test.
- **Disposition:** defer to a later interface version; no compatibility-breaking payload redesign is included in the current hardening baseline.

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

The current closeout work is validation rather than another required source correction. The remaining items below are independent, source-confirmed candidates after validation or explicit disposition:

1. **F-001/F-054/F-055/F-056/F-050:** add the identified deterministic regression/fault-injection coverage.
2. **F-048:** characterize the corrected soft-landing trajectory and graph parity.

Before any powered release, complete F-001/F-054 regression and powered-controller validation, complete F-055/F-056/F-050 regression coverage, retain the direct actuator-inhibit/fresh-temperature contract in ADR-0002, investigate/resolve F-016, and complete the required hardware validation. No compilation or mock result establishes physical output behavior.

## Existing build evidence

ESP-IDF 5.5.4 build completed on pulled commit `16467e0` on 2026-07-13 using `IDF_PATH=/home/vesko/.espressif/v5.5.4/esp-idf cmake --build build -j2`: application `0x614f0` bytes, 62% of the smallest 1 MiB app partition free. The build used the retained local `sdkconfig`, not the new Kconfig defaults (F-049). Warnings included discarded `const` in logger CLI code, logger-storage declaration issues, unused legacy MAX31865 parser, and an impossible unsigned `< 0` check in health monitoring. Build evidence does not validate hardware behavior.
