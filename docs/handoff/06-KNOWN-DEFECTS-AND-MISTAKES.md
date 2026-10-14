# 06 — Known defects and mistakes left in place

This is the honest ledger for the rewrite. It has four parts:

- **A.** Findings added by the 2026-10-13 audit (F-057 to F-080). They are also recorded in [../analysis/FIRMWARE_FINDINGS.md](../analysis/FIRMWARE_FINDINGS.md).
- **B.** Earlier findings that are still open, only source-fixed, or deferred.
- **C.** Documentation and process mistakes.
- **D.** Unresolved questions.

Categories follow the findings register:

- **confirmed defect**: reachable trigger plus exact source evidence.
- **highly likely defect**: one runtime or config dependency not demonstrated.
- **possible defect**: needs more evidence.
- **design weakness**: not a claimed defect.

No item here was reproduced on hardware. "Confirmed" means confirmed by source trace.

Every item ends with **Lesson** (the rule for the new design) and **Test** (the first test the TDD project should write for it).

## A. Findings added by this audit

### F-057 — Overshoot guard aborts every programmed cooling step larger than the threshold

- **Category:** confirmed defect (source trace; not field-observed). **Risk:** high (process abort, misreported).
- **Evidence:** The validator explicitly accepts cooling stages (`heating_program_validation.c:281`, `:387`). For such a stage `detect_stage_phase` returns COOLING, and the COOLING branch sets `setpoint = target` (`temperature_profile_core.c:464-470`). The overshoot test `current_temp > setpoint + overshoot_threshold` excludes only COOLDOWN and COMPLETE (`:494-503`).
- **Trigger:** A program such as "ramp to 150 °C, hold, cool to 100 °C". On the first COOLING tick, 150 > 100 + 20 (field) or 100 + 30 (tracked) returns `THRESHOLD_EXCEEDED`, so the coordinator emergency-stops.
- **Impact:** The run is aborted (heater off, which is the safe direction) and reported as "Completed" (F-058). The operator believes the cure finished.
- **Lesson:** Guards must be phase-aware and expressed against the right reference. Overshoot during cooling (heater off) is meaningless relative to the lower target. Keep an absolute over-temperature limit separate (F-077).
- **Test:** A profile with a 50 °C cooling step under a simulated thermal model must complete all stages without a guard trip. A heater-stuck-on trace during HOLD must trip.
- **Decision (2026-10-13, D-06):** the user rates this a minor bug. New-project rule: in COOLING never end the program; alarm if the temperature rises or stays flat for a long time. Parameters are deferred (T-04).

### F-058 — Over-temperature emergency stop is reported as normal completion

- **Category:** confirmed defect. **Risk:** high (operator and quality-record misinformation on the most safety-relevant fault).
- **Evidence:** `coordinator_component_heater_controller.c:370-391` posts `FURNACE_ERROR_EVENT` (no production subscriber, F-035), then `COORDINATOR_EVENT_PROFILE_COMPLETED`. The HMI `nextion_event_handle_profile_completed` (`nextion_run_handlers.c:464-469`) shows "Completed" and zeroes the remaining time. The fan controller returns to auto.
- **Lesson:** Terminal states need distinct outcomes: completed, stopped by operator, aborted with a fault code. Fault reporting must not depend on an unsubscribed channel.
- **Test:** Inject an overshoot. Assert the outcome is ABORTED(OVERTEMP), the operator-facing event carries the cause, and no COMPLETED is emitted.
- **Decision:** operator-side presentation is deferred to the new display design (D-13, T-01).

### F-059 — A sensor-loss pause is invisible to the operator; the last valid temperature stays on screen

- **Category:** confirmed defect. **Risk:** high (operational).
- **Evidence:** The invalid or expired paths call `pause_heating_profile` without posting `PROFILE_PAUSED` or an error (`coordinator_component_events.c:165-176`, `:186-192`; `coordinator_component_heater_controller.c:328-339`). The HMI subscribes only to the float event (`hmi_coordinator.c:410-414`), which the processor posts only when valid (`temperature_processor_task.c:86-90`). While paused, the control loop sends no status updates.
- **Impact:** The panel keeps showing "Running" with a frozen temperature and frozen elapsed time, while heating has stopped. Operating hours keep counting.
- **Lesson:** Sensor health is a first-class operator state. Publish value and validity together; every inhibit transition emits a state event.
- **Test:** Stop sensor updates for longer than the freshness budget. Assert the output-off happens before the operator event, and that the event names the cause and the fresh/total sensor count.
- **Decision:** sensor problems are top priority; field errors have come from sensor loss (D-07). Policy is deferred (T-05); presentation is deferred (T-01).

### F-060 — Automatic resume after sensor recovery can never happen (code and docs disagree)

- **Category:** confirmed behavior/documentation divergence. **Risk:** medium (fail-safe, but a silent indefinite stall when combined with F-059).
- **Evidence:** `pause_heating_profile` always asserts the control inhibit (`coordinator_component_heater_controller.c:793`). The recovery branch resumes only when `!heater_controller_output_is_inhibited()` (`coordinator_component_events.c:221`), and that function includes `control_inhibited` (`heater_controller_task.c:heater_output_is_inhibited`). So the recovery always takes the "another heater inhibit remains active" branch.
- **Docs that say otherwise:** ADR-0002 Consequences; F-001 evidence text; F001-F054 hardware plan step 7; `COORDINATOR_SENSOR_AUTO_RECOVERY` help.
- **Lesson:** Decide the policy (auto-resume, or operator acknowledgement) and encode it in one state machine with a test. Do not infer it from two interacting flags.
- **Test:** Drive invalid, then K valid aggregates. Assert the documented policy and the event sequence.
- **Decision:** policy deferred (T-05).

### F-061 — Operator resume while the sensor-data inhibit is still held lets profile time run without heat

- **Category:** possible defect (narrow window by default; persistent if `COORDINATOR_SENSOR_AUTO_RECOVERY=n`). **Risk:** medium.
- **Evidence:** `resume_heating_profile` releases only the control inhibit and clears `paused`. If `sensor_data_expired` is already false (the first fresh sample cleared it), the control loop runs `profile_tick`, so HOLD time elapses, while `set_heater_target_power_level` rejects every command. With auto-recovery disabled, nothing ever releases the sensor inhibit until reboot.
- **Lesson:** "Resume" must be refused, or must report, while any non-control inhibit is active. Profile time must advance only while heating is actually authorized.
- **Test:** Resume during recovery count < K. Assert the resume is refused and stage time does not advance.

### F-062 — Unbounded event posting on safety and real-time paths; the event loop can block on its own queue

- **Category:** confirmed violation of the stated rule "do not block a sole consumer by submitting to its own bounded queue". The resulting deadlock is a highly likely latent defect: it needs event-queue saturation, not demonstrated at runtime. **Risk:** high.
- **Evidence:**
  - `toggle_heater` (`heater_controller.c:35-40`) posts `HEATER_CONTROLLER_HEATER_TOGGLED` with `portMAX_DELAY` after every SSR write. **No subscriber exists** for `HEATER_CONTROLLER_EVENT`.
  - Health heartbeats are posted with `portMAX_DELAY` (`event_manager.c:event_manager_post_health`) by the dispatcher, heater PWM, coordinator, device manager, temperature processor and HMI tasks. There is **no subscriber** while the health monitor is disabled.
  - Coordinator, processor and device-manager events and `FURNACE_ERROR_EVENT` all use blocking posts.
  - The coordinator temperature callback runs **on the event-loop task** and calls `pause_heating_profile` / `coordinator_inhibit_for_sensor_data_expiry`. Those reach `toggle_heater`, which posts to that same loop, and `kill_heater`, which submits to the dispatcher queue with `portMAX_DELAY`.
- **Interleaving:** The event queue (32) is full while the loop is inside the temperature callback.
  1. The loop writes SSR low, then blocks forever in the post, so `stop_heater()` (contactor off) never runs.
  2. The heater PWM task blocks in its next `toggle_heater`. If that was an SSR-on write, the SSR stays on until the expiry timer fires.
  3. The expiry callback on the shared `esp_timer` task writes SSR low and then blocks in the same post, wedging every `esp_timer` callback in the system, including the PID tick.
  4. Final state: SSR off, **contactor on**, three system tasks wedged. The task watchdog does not watch them (F-036).
- **Lesson:** Safety paths perform I/O only. Telemetry is non-blocking and droppable. Never publish from a callback into the queue that feeds it. Do not publish events nobody consumes. Watch every task with the watchdog.
- **Test:** With a fake bus that rejects every publish (queue full), assert every inhibit path still drives SSR **and** contactor off and returns within a bound.

### F-063 — Actuator check-then-act race can re-assert the SSR after an inhibit

- **Category:** confirmed defect (reachable on the dual-core ESP32 without preemption). **Risk:** high (violates the actuator-boundary invariant; mitigated if the contactor is electrically in series).
- **Evidence:** `heater_controller_task.c:43-48` reads the target, then checks `heater_output_is_inhibited` (each under its own mutex acquisition), then calls `toggle_heater(HEATER_ON)` with no lock held. The inhibit functions do not notify the PWM task.
- **Interleaving:** The PWM task passes the check. On the other core, the expiry timer asserts the sensor inhibit (target 0, SSR low, contactor low). The PWM task then writes SSR high and sleeps for up to 5 s.
- **Lesson:** Make the authorization decision and the GPIO write one critical section owned by the actuator. Wake the actuator on every inhibit change.
- **Test:** A deterministic scheduler test that asserts inhibit between check and write must never observe SSR = 1 after the inhibit returns.
- **Decision (D-08):** unacceptable; must be prevented by design and tested (applies to F-062 as well).

### F-064 — Manual mode heats to the target and then turns the heater off instead of holding

- **Category:** confirmed by source trace; not field-observed. **Risk:** medium (functional; fail-safe direction).
- **Evidence:** `start_manual_program` (`nextion_manual_handlers.c:99-150`) builds one stage with `t_min = 10080` ("runs until the user explicitly stops it"). When T ≥ target − 0.5, HEATING advances (`temperature_profile_core.c:420-429`). With no further stage, `advance_stage` enters COOLDOWN (`:255-263`), so the heater turns off, the fan turns on, and the run completes below 40 °C. Only a start within ±2 °C of target is detected as a 7-day HOLD.
- **Lesson:** Manual mode is a different state machine ("track target until stopped"). Do not emulate it with a program.
- **Test:** Manual target 80 °C from 25 °C. After arrival, the setpoint stays 80 °C and the heater stays authorized until Stop.
- **Decision (D-09):** manual mode will hold until stop; full redesign deferred (T-06).

### F-065 — Manual live retarget ramps faster than the requested rate

- **Category:** confirmed defect (math). **Risk:** low-medium.
- **Evidence:** `apply_pending_target_update` computes `ramp_ms = ceil(|Δ| / rate)` in whole minutes from `(int)cur_temp` (`coordinator_component_heater_controller.c:271-284`). `ramp_ease_position` assumes planned time covers `span + band`. The effective linear rate is therefore (S + b)/S × requested: 1.6× for a 10 °C step with b = 6; up to 2× initial rate when S ≤ 6.
- **Lesson:** One function plans a ramp; every entry path uses it.
- **Test:** Property test: for every entry path, max(dSP/dt) ≤ requested rate.

### F-066 — MS9024 repair table still writes a wrong register value

- **Category:** confirmed (dormant: no in-tree caller). **Risk:** medium if ever used.
- **Evidence:** `ms9024.c:382` writes reg 129 = 282. The healthy-unit capture (`ms9024_healthy_registers.txt`) shows 265 and calls 282 wrong. Reg 26 is missing from the table. Regs 107–129 appear to be per-unit calibration.
- **Lesson:** Never ship hard-coded calibration repair. Commissioning writes are explicit, per-unit, verified against a stored baseline of *that* unit.
- **Test:** Diagnostics are read-only by default (no write function reachable without an explicit service command).
- **Decision (D-10):** not being fixed; the new project likely uses custom thermal sensors (T-07).

### F-067 — The tracked configuration is not the field configuration; partition and flash settings are wrong

- **Category:** confirmed by file comparison. **Risk:** high (operational).
- **Evidence:** See [05-CONFIGURATION-CATALOG.md](05-CONFIGURATION-CATALOG.md) §1. The tracked file has 2 MB flash and a single-app table with no `storage` partition. The field-validated PID/FF/guard values are absent, and earlier docs present the tracked file as authoritative.
- **Impact:** A clean build loses crash-log storage and every tuning result, and can trigger the NVS erase path (F-073).
- **Lesson:** One tracked configuration per variant. CI builds exactly what ships. Boot prints the config identity.
- **Test:** CI asserts the partition table contains the storage partition and the PID/guard values equal the variant file.

### F-068 — "Store last logs on critical error" is not wired

- **Category:** confirmed defect. **Risk:** medium (diagnostics).
- **Evidence:** `logger_store_full_log()` (`logger_storage.c:107`) has no caller anywhere. `panic_capture` is registered with `esp_register_shutdown_handler` (`:90`), which runs only on `esp_restart()`, not on panic, watchdog or brownout. `error_code` is never assigned (`:318`, commented). Header timestamps are hard-coded 0, and there is no wall-clock source (the RTC lives on the Nextion).
- **Lesson:** Use the platform's crash facilities (Zephyr coredump/retention, reset-cause register) and test them by forcing each reset cause.
- **Test:** Trigger an assert/fault in test mode. After reboot, a record exists with cause, uptime, firmware/config ID and the last N lines.

### F-069 — Invalid-temperature sentinel `-0.0f` also matches `+0.0f`

- **Category:** confirmed (minor). **Evidence:** `coordinator_component_events.c:255` compares `temperature == -0.0f`, which IEEE-754 makes true for any zero. **Lesson:** Never encode validity in a float value. **Test:** 0.0 °C with valid=true is accepted.

### F-070 — The fan "Max/Silent" mode is stored and displayed but never applied

- **Category:** confirmed feature gap. **Evidence:** `handle_fan_mode_toggle` saves NVS `fan_mode` and updates the panel; there is a TODO and no consumer (`nextion_manual_handlers.c:284-294`). The fan output is plain on/off. **Lesson:** Do not expose operator controls without an implementation. **Test:** n/a until the fan requirement is decided (FAN-03).

### F-071 — Sensor cache and device table are shared across tasks without synchronization

- **Category:** possible defect / design weakness (C data race; small practical effect on ESP32's atomic aligned 32-bit accesses).
- **Evidence:** `temp_sensor_update` writes `last_temperature`, `last_update_tick` and `has_successful_sample` on the device-manager task. `read_temp_sensors` reads them on the processor task. `device_t.state` and table slots are mutated from other tasks without locks (F-056 residual).
- **Effect:** A value/tick pair can mismatch, accepting a reading up to one cycle old as fresh.
- **Lesson:** Publish a sample as one immutable record (value, timestamp, status) through a channel or a seqlock.
- **Test:** A concurrent stress test on host (TSan) on the sample-publication primitive.

### F-072 — Coordinator state is driven from four execution contexts through plain fields

- **Category:** possible defect / design weakness. **Risk:** low as observed, but violates the single-owner rule.
- **Evidence:** `running`, `paused`, `heating_task_state.*` and `task_handle` are plain fields (`coordinator_component_internal.h`). They are touched by the dispatcher task (start/pause/resume/stop), the control task, the event-loop task (sensor pause and `resume_heating_profile`, `coordinator_component_events.c:221-224`), and the `esp_timer` task.
- **Interleaving:** The event-loop recovery passes `ctx->running` and calls resume. The dispatcher runs stop and asserts the control inhibit. Resume then releases the control inhibit after stop. Nothing produces commands afterwards, so the contactor stays open.
- **Lesson:** One owner thread for run state; all others send messages.
- **Test:** Model-based state-machine test of start/pause/resume/stop/sensor events in any order. Invariant: no inhibit released outside RUNNING.

### F-073 — The boot-time NVS erase fallback silently destroys service hours

- **Category:** design weakness. **Risk:** medium.
- **Evidence:** `main.c:31-36` erases the whole NVS partition on `ESP_ERR_NVS_NO_FREE_PAGES` or `ESP_ERR_NVS_NEW_VERSION_FOUND`. That wipes `op_time_s`, preferences and the program registry. Triggers include a partition-size change (F-067) and NVS format upgrades.
- **Lesson:** Service-hour data needs its own migration path and must never be erased implicitly. Log and surface storage loss.
- **Test:** A storage-format mismatch preserves the hours record or reports loss explicitly.

### F-074 — Logging sits on real-time and safety paths and is very high volume

- **Category:** design weakness. **Risk:** medium-high (feeds F-062).
- **Evidence:**
  - `logger_send` waits up to 100 ms on a full queue (`logger_core.c`).
  - `toggle_heater` logs *before* the GPIO write (`heater_controller.c:35`), so de-energization can be delayed by up to 100 ms per call.
  - INFO-level lines are written every PID tick, every MS9024 decode, every device update, and every processor event. Project `LOG_LEVEL=4` (DEBUG).
  - The logger task priority (4) is below the producers (5).
- **Lesson:** Deferred, lossy, rate-limited logging. Never log before a safety action. Structured control-trace records at a fixed rate, separate from text logs.
- **Test:** With the log backend blocked, inhibit latency stays within budget.

### F-075 — Documentation and rule drift

- **Category:** maintainability. Itemised in section C.

### F-076 — The program model, limits, operating hours and program library are owned by the Nextion component

- **Category:** design weakness (key for the rewrite).
- **Evidence:**
  - `common/include/core_types.h:21-23` sizes programs from `CONFIG_NEXTION_PROGRAMS_PAGE_*`.
  - Validation and profile execution use `CONFIG_NEXTION_*` limits and tolerances.
  - Operating hours are counted by the HMI task (`nextion_run_tick`).
  - Ambient reference, cooldown rate and fan mode are in the HMI model.
  - Program files live on the panel SD card.
- **Lesson:** Domain model and limits belong to the controller core; the HMI is a client. See [07](07-HMI-CONTRACT-NEXTION.md).

### F-077 — No absolute or always-on over-temperature supervision in firmware

- **Category:** design weakness. **Evidence:** The only over-temperature check is relative to the moving setpoint, inside a running, non-paused profile (`temperature_profile_core.c:494-503`). `NEXTION_MAX_TEMPERATURE_C` limits program entry only. Idle, paused and stopped states have none; an independent hardware cutoff is unknown (F-043). **Lesson:** An always-running supervisor with an absolute limit that opens the contactor, independent of profile state. **Test:** T > absolute limit in IDLE, PAUSED or RUNNING opens the contactor within the bound.

### F-078 — The actuator samples demand once per 5 s window

- **Category:** design weakness / decision. **Evidence:** The PWM task reads the target at window start only (`heater_controller_task.c:43`); PID publishes every 1 s; `HEATER_CLEAR` does not wake the task. **Effect:** 80 % of PID outputs are ignored and response latency is up to 5 s. Tuning was done under this behavior, so changing it changes loop dynamics. **Lesson:** Choose the actuation scheme deliberately (window length, mid-window updates, zero-cross SSR limits) and re-tune. **Test:** Duty accuracy and latency for a demand step.

### F-079 — Minor latent defects

- **Category:** confirmed, low risk.
- **Items:**
  - Format-string UB at `temperature_processor_task.c:73` (`%d` with a string) and `modbus_master_core.c:44` (`%d` with no argument).
  - The `init_temp_processor` idempotency guard tests a global `processor_running` that is never set (`temperature_processor_core.c:9,22`).
  - No bound check of the sensor count against `TEMP_SENSORS_MAX_SENSORS` (9) and the sample buffer (10).
  - `device_manager_task.c` compares ticks with `now < next_wake_time` (not wrap-safe).
  - `average_float_array` computes an unused min/max seeded with `FLT_MIN`.
  - `printf` in `device_manager_create_device`.
  - `HEATER_GET_STATUS` and `get_heating_task_state` are TODO stubs.
  - The `pid_tick_timer_cb` comment claims ISR context (it runs on the `esp_timer` task).
  - The coordinator exit log says "Temperature monitor task exiting".
- **Lesson:** Compiler warnings as errors plus format checking plus static analysis in CI.

### F-080 — Stage type is inferred from the measured temperature, not from program intent

- **Category:** confirmed behavior (design decision needed). **Risk:** medium-low.
- **Evidence:** `advance_stage` detects the phase from T with ±2 °C and uses `t_min` for any non-HEATING phase (`temperature_profile_core.c:275-305`).
- **Effects:**
  - A ramp stage that starts within 2 °C of its target (warm restart, small steps) becomes a HOLD lasting the ramp's `t_min`.
  - A hold stage (rate 0) that starts more than 2 °C below target becomes a slow ramp over the hold time, and is skipped on arrival, so the hold time is lost.
- **Lesson:** Store the stage kind explicitly (RAMP / HOLD / COOL) and handle "already there" deliberately.
- **Test:** A warm-start program table of expected phase and duration per stage.

## B. Earlier findings still open, source-fixed only, or deferred

Detail and evidence: [../analysis/FIRMWARE_FINDINGS.md](../analysis/FIRMWARE_FINDINGS.md) and [../analysis/CURRENT-FIX-STATUS.md](../analysis/CURRENT-FIX-STATUS.md). **No finding has executed regression coverage or hardware validation.** Every "source fix" is unproven by test.

| ID | Topic | State at `d1b0fda` | Carry into the rewrite as |
| --- | --- | --- | --- |
| F-001 / F-054 | Stale or absent temperature used for control | Source fix: fresh ticks, quorum, direct inhibit, expiry timer | Requirements TEMP-03…07 with tests; hardware latency plan |
| F-002 | Dispatcher self-deadlock | Source fix: inline re-entrant execution (nested stack uncharacterised) | No re-entrant command execution; bounded queues with defined overflow |
| F-003 | Stale PID output after pause | Narrow gate: temporary control inhibit; no command generation/epoch | Actuator accepts demand only with a run-epoch token |
| F-004 | Disagreeing sensors | Policy: warning only; quorum N−2 temporary | **Changed by D-07:** drastic outliers are excluded and reported; algorithm in T-05 |
| F-005, F-010 | Sample compaction; init failure | Source fix | Unit tests |
| F-006…F-009, F-012 | Lifecycle joins (processor, dispatcher, coordinator, heater, device manager) | Worker-ack source fixes; no stop/restart test | Static threads that never stop, or a tested lifecycle protocol |
| F-011, F-019 | Start-order and unwind | Source fix | Start transaction test (all resources before authorization) |
| F-013, F-021 | Mailbox and temperature races | Mutex source fix | Single owner (F-072 lesson) |
| F-014 | HMI drops critical events | Bounded priority delivery; still lossy | Reliable state channel separate from telemetry |
| F-015, F-016, F-028 | Run indicator collided with contactor GPIO | Component excluded from production | Devicetree pin ownership; build-time collision check |
| F-017 | SSR GPIO failure | Permanent local inhibit plus contactor off; report unsubscribed | Keep the behavior; report through the fault manager |
| F-018 | Reset without inhibit | Source fix | Keep |
| F-020 | PID state across runs | Reset at start/stop/handover/resume | Unit tests |
| F-022…F-024, F-026, F-027, F-032, F-033 | Nextion file/protocol robustness | Source fixes | Moot for the controller; lessons apply to any program-transfer protocol (complete-read, bounded retries, atomic replace, exact command framing, range-checked parsing) |
| F-025, F-029, F-030 | MS9024 verify; pool rollback; Modbus handle | Source fixes | Unit tests in the new Modbus layer |
| F-031 | Query payloads | Source fix | n/a |
| F-034 | Kconfig enable flags | Source fix; disabled build never compiled | Build matrix in CI |
| F-035 | `FURNACE_ERROR_EVENT` has no subscriber | **Deferred** (ADR-0001) | Fault manager owns severity, latching, operator acknowledgement |
| F-036 | Watchdog/health disabled | **Deferred** | Task watchdog for every thread from day one |
| F-037 | Legacy SPI/MAX31865 path | Excluded from build | Do not port |
| F-038 | Device count | Source fix | n/a |
| F-048 | Easing overshot the ramp rate | Source fix (quadratic) | Property test (03 §9) |
| F-049 | Config baseline | **Superseded/extended by F-067** | Same lesson |
| F-050 | Op-hours override | Source fix | Decide PERS-04 |
| F-051 | Error payload unversioned | **Deferred** | Versioned message schemas |
| F-053 | Non-finite control values | Source fix | Unit tests |
| F-055 | Stop deadlock on full queue | Source fix | Covered by the F-062 lesson |
| F-056 | Processor teardown left devices | Source fix; table sync open (F-071) | Same |
| F-039…F-041, F-052 | Missing tests | **Open; nothing executed** | The TDD backlog in [09](09-REWRITE-GUIDANCE-ZEPHYR-TDD.md) |

## C. Documentation and process mistakes (F-075 detail)

1. **The tracked `sdkconfig.defaults` was documented as the reviewed, authoritative configuration**, but it is not the field-tuned build (F-067). REPOSITORY_MAP, F-049 and CHANGE-VALIDATION-F049 repeat the tracked values.
2. **ADR-0002, F-001 and the F-001/F-054 hardware plan describe auto-resume** that the code cannot perform (F-060).
3. `docs/codex/repository-map.yaml` had drifted before this audit: `run_indicator_init` in startup, "soft-landing is cubic", "no tracked sdkconfig defaults", "F-056 still leaves devices active", "F-003 unresolved". Corrected 2026-10-13.
4. REPOSITORY_MAP said a task notification interrupts the heater PWM wait. Only shutdown notifies it (corrected 2026-10-13).
5. `components/AGENTS.md` forbids broad source globs, but every component `CMakeLists.txt` uses `file(GLOB SRC_FILES "src/*.c")`. The rule was written but never applied.
6. Kconfig help contradicts the used values:
   - `PID_KI` "typical 5..30" versus 1 used.
   - "keep ease band ≤ hold band" versus 6 > 4.
   - Overshoot help "20 C suits…" versus tracked 30.
   - `HEATER_WINDOW_SIZE_MS` "1000 ms is comfortable" versus 5000.
   - `NEXTION_COOLDOWN_RATE_X10` "ramps to 0 C" versus completion at 40 °C.
   - Both heater pins have the prompt "GPIO Pin".
7. `components/pid_component/pid_values.md` is internally inconsistent: FF low 7 versus 8; output max 100 % versus field build 95 %; FF high 10 versus 12; window "1000/5000 ms?".
8. Components listed as utilities that do nothing in production:
   - `time_component` has a Kconfig but no sources.
   - `transmitter_diagnostics` is linked but never started.
   - `health_monitor` is linked but disabled.
   - `error_manager` has no registered descriptors.
9. Misleading code comments are itemised in F-079. `ms9024_repair_from_good_unit` refers to a nonexistent `CONFIG_MODBUS_TEMP_REPAIR_BAD_UNIT`.
10. **A prior analysis error in team notes.** A June 2026 note recorded "overshoot guard fires in HEATING/HOLDING/COOLING (verified)" as useful coverage during long holds. It missed that in COOLING the reference is the lower target, which is F-057. Coverage claims need a counter-example search, not just a reachability check.
11. Build evidence in the docs uses a developer-specific path (`/home/vesko/.espressif/...`) and a local `sdkconfig`, so it is not reproducible from the repository.
12. Hardware validation plans (F-017, F-001/F-054) were written but never authorized or executed. The hardening pass closed with **every** safety fix in "source fix implemented; validation pending".
13. A commit titled "Store last logs on critical error" (#17) shipped an uncalled function (F-068). Commit titles and feature claims were not checked against call graphs.
14. Two "Field tests" commits (#20, #21) squash 55 listed changes of feature, tuning, hardening and documentation work; #21 has an identical tree. Per-change history survives only in the squash message.

## D. Unresolved questions (owner: product/hardware)

Status as of 2026-10-13: the answers and the remaining deferred topics are tracked in [10-DECISIONS-AND-DEFERRED-TOPICS.md](10-DECISIONS-AND-DEFERRED-TOPICS.md).

| Question | Status |
| --- | --- |
| Q-01 Fan purpose | Answered: D-12 (cooler and heat distributor, always on); RPM and failure detection deferred to T-09 |
| Q-02 Manual mode | Answered: D-09 (hold until stop); redesign deferred to T-06 |
| Q-03 Recovery after sensor loss | Deferred: T-05 (sensor problems are top priority, D-07) |
| Q-04 Quorum and outliers | Partly answered: D-07 (exclude and report drastic outliers); algorithm in T-05 |
| Q-05 Absolute limit | Deferred: T-10 |
| Q-06 Completion threshold | Deferred: T-11 |
| Q-07 Precision | Inventory accepted (D-11); values in T-08 |
| Q-08 Board | 16 MB flash decided (D-04); MCU and layout in T-12 |
| Q-09 HMI location and programs | Answered: D-01, D-03 (the display is a remote GUI; programs in controller memory); protocol in T-02 |
| F-042, F-047 | Deferred: T-15 |
| F-043 | Deferred: T-10 |
| F-044 | Mostly moot (D-10, custom sensors likely); sensor interface in T-07 |

Original question list:

| ID | Question |
| --- | --- |
| F-042 | GPIO active polarities, external pulls, reset-time pin states, schematic-approved pins |
| F-043 | Is there an independent hard-wired over-temperature cutoff, airflow proof, safety contactor chain? |
| F-044 | MS9024 revision, register widths, float word order (configurable?), meaning of regs 27/30/129, EEPROM endurance |
| F-045 | Task priorities, stacks and measured high-water marks (moot for the rewrite; measure the new one) |
| F-046 | Nextion framing guarantees (moot if the Nextion is not hosted) |
| F-047 | Output state and time-to-de-energize during WDT reset, software restart, brownout, power cycle |
| Q-01 | Fan purpose (chamber circulation or electronics cooling?) and required behavior during pause, fault and idle |
| Q-02 | Manual-mode semantics (hold until stop? time-limited?) |
| Q-03 | After sensor loss: auto-resume or operator acknowledgement? Should stage time be extended by outage duration? |
| Q-04 | Sensor quorum and placement policy; per-furnace sensor count; outlier rejection |
| Q-05 | Absolute maximum temperature and required response time |
| Q-06 | Is completion at < 40 °C fixed, configurable, or "operator confirms"? What if ambient keeps the chamber above 40 °C? |
| Q-07 | Exactly which precision changes are planned (see [05](05-CONFIGURATION-CATALOG.md) §2) |
| Q-08 | Target MCU/board for Zephyr (same ESP32? 8 MB flash?) |
| Q-09 | Where the operator interface and program library live without a hosted Nextion |
