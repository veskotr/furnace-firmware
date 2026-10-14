# 02 — Functional requirements (behavior contract)

Each requirement is written to be turned into one or more tests in the new project. **Current** describes what the code at `d1b0fda` does (evidence: source unless stated). **Status** is one of:

- `kept`: current behavior is correct and should be preserved.
- `fix`: current behavior diverges from the requirement; the defect is listed in [06](06-KNOWN-DEFECTS-AND-MISTAKES.md).
- `decide`: a product decision is needed before the requirement is final.
- `new`: not implemented today.

Numbers quoted are the field-tuned values unless marked otherwise; see [05](05-CONFIGURATION-CATALOG.md).

Status references to D-xx (decided) and T-xx (deferred: ask the user before designing) point to [10-DECISIONS-AND-DEFERRED-TOPICS.md](10-DECISIONS-AND-DEFERRED-TOPICS.md). Updated 2026-10-13.

## SAFE — physical output safety

| ID | Requirement | Current | Status |
| --- | --- | --- | --- |
| SAFE-01 | At boot, after reset, and after partial initialization, SSR and contactor are de-energized and heating is impossible until valid temperature data and an explicit program authorization exist. | Heater init drives both low; coordinator init asserts the sensor-data inhibit (`coordinator_core.c:init_coordinator`). The window between reset and GPIO configuration depends on hardware pulls (F-042/F-047). | kept + hardware verify |
| SAFE-02 | Three independent inhibit reasons gate the actuator: permanent hardware fault, recoverable sensor-data, temporary control (pause/stop/complete). Releasing one never releases another. | `heater_controller_context_t.output_inhibited / sensor_data_inhibited / control_inhibited`; ADR-0002. | kept |
| SAFE-03 | Asserting any inhibit zeroes demand and drives SSR and contactor off **synchronously, without any queue, event or logger wait** on the path. | Done by direct call, but `toggle_heater()` logs **before** the GPIO write (logger can wait up to 100 ms) and posts an event with **unbounded wait after it**, so the contactor-off can be skipped (F-062, F-074). | fix — D-08 |
| SAFE-04 | The actuator re-checks inhibit state atomically with every SSR-on write; a concurrently asserted inhibit can never be followed by SSR-on. | Check and write are separate (`heater_controller_task.c:46-48`), so SSR can be re-asserted for up to one window after an inhibit (F-063). | fix — D-08 |
| SAFE-05 | An SSR GPIO failure latches a permanent inhibit, retries SSR-off, opens the contactor, and is reported; only a restart clears it. | `heater_controller_handle_ssr_failure`. The report goes to an event with no subscriber (F-035). | kept + report fix |
| SAFE-06 | Restart and factory reset inhibit the heater first and refuse if inhibition fails. | `nextion_settings_handlers.c:inhibit_heater_before_reset` (F-018). | kept |
| SAFE-07 | Overtemperature supervision runs whenever the controller is powered, against an absolute limit as well as the setpoint-relative limit. | Only while a profile is running and not paused, and only relative to the moving setpoint (`temperature_profile_core.c:494-503`). No absolute firmware limit; external cutoff unknown (F-043, F-077). | new — T-10 |
| SAFE-08 | A stuck application task is detected and leads to de-energized outputs, not merely a log. | Health monitor disabled; the ESP-IDF task watchdog watches only idle tasks (F-036). | new |
| SAFE-09 | Every fault that stops heating is reported to the operator with a cause; "completed" is never shown for an abort. | Overshoot abort reports `PROFILE_COMPLETED` (F-058); sensor-loss pause reports nothing (F-059). | fix — D-13 / T-01 |

## TEMP — temperature acquisition and validity

| ID | Requirement | Current | Status |
| --- | --- | --- | --- |
| TEMP-01 | Poll N sensors (N=5) on one RS-485 bus at about 1 Hz. Each sensor has its own Modbus address (1…N). | `device_manager_task` 1 s cycle; addresses `TEMP_SENSOR_MODBUS_START_ADDRESS + pool index`. | kept |
| TEMP-02 | A reading is accepted only if it is finite, not the MS9024 "no reading" pattern `0x80000000`, and within the plausible range. | `ms9024_read_float`: rejects NaN/Inf, `-0.0`, and outside −200…1500 °C. The furnace range is 20–170 °C, so the plausibility range is very wide. | kept / decide range |
| TEMP-03 | Each sensor records the tick of its last successful read; a cached value counts toward the aggregate only while younger than the freshness budget (5 s). | `temperature_processor_task.c:read_temp_sensors`. The cache pair (value, tick) is written and read across tasks without synchronization (F-071). | kept + fix sync |
| TEMP-04 | The aggregate is the mean of fresh samples. It is valid only if the fresh count ≥ quorum. Quorum is currently max(1, N−2). | Hard-coded `number_of_temp_sensors - 2`. | changed — D-07 / T-05 |
| TEMP-05 | Sensor disagreement beyond a threshold is a **warning**, not invalidity (explicit field requirement: probes are equally representative). | `process_temperature_samples`, F-004. | **changed — D-07**: drastic outlier is excluded and reported (T-05) |
| TEMP-06 | An accepted aggregate expires after the freshness budget even if no further message arrives; expiry de-energizes the heater directly. | Coordinator one-shot `sensor_data_expiry_timer` (F-054). | kept |
| TEMP-07 | After invalidity or expiry, K consecutive valid aggregates (K=3) are required before the sensor-data inhibit can be released. | `coordinator_component_events.c:194-237`. | kept |
| TEMP-08 | Whether a sensor-caused pause resumes automatically after recovery must be decided and implemented consistently. | Docs and ADR-0002 say it auto-resumes; the code never does, because pause also sets the control inhibit (F-060). | decide — T-05 |
| TEMP-09 | Every consumer that shows or uses temperature gets the validity with the value, and a stale value is never presented as current. | The HMI and fan only receive the float when valid; on loss they keep the last value with no indication (F-059). | fix — T-01 |
| TEMP-10 | A sensor whose reading differs drastically from the others is detected, reported to the operator, and excluded from the aggregate until it is trustworthy again. | Not implemented: disagreement is only a warning (F-004). | new — D-07; algorithm T-05 |

## PROG — program model and validation

| ID | Requirement | Current | Status |
| --- | --- | --- | --- |
| PROG-01 | A program has a name (≤31 chars, letters/digits/spaces, no commas) and up to 15 stages (3 editor pages × 5). | `program_draft_t` in `common/include/core_types.h`; size from **Nextion** Kconfig (F-076). | kept, decouple — D-01 |
| PROG-02 | A stage has a target temperature (integer °C), duration `t_min` (minutes), rate `delta_t_per_min_x10` (0.1 °C/min units, signed), and set-flags. | `program_stage_t`. `t_delta_min` is stored but not used at runtime. | decide precision |
| PROG-03 | Save-time validation: name rules; every set stage complete; 20 ≤ target ≤ 170 °C; heating stages need 0 < t ≤ 10080 min and −3.0 ≤ rate ≤ 3.0 °C/min; stage math consistent within ±2 °C and ±1 min from the previous target (first stage from the configured ambient); total time 1…10080 min. Cooling stages (target below previous) skip time/rate/math checks. | `program_validate_draft_with_temp`. | kept |
| PROG-04 | Run-time validation checks device limits and completeness only (no math), because execution starts from the measured temperature. | `program_validate_draft_for_run`. | kept |
| PROG-05 | Autofill computes the missing value (time from rate rounded up; rate from time only if exact, else suggest). | `autofill_calc_time/delta`. Editor feature; HMI-side in the rewrite. | kept (HMI) |

## PROF — profile execution

| ID | Requirement | Current | Status |
| --- | --- | --- | --- |
| PROF-01 | Stage phase is detected from the **measured** temperature at stage start: HEATING if target > T+2, COOLING if target < T−2, else HOLDING. | `detect_stage_phase`, tolerance `NEXTION_TEMP_TOLERANCE_C=2`. A ramp stage that starts within 2 °C of its target becomes a HOLD lasting the ramp's `t_min` (F-080). | kept / decide |
| PROF-02 | A HEATING stage's planned time is recomputed from the actual start: \|target−start\| / rate, plus one ease band at the same rate. | `advance_stage`. | kept |
| PROF-03 | The HEATING setpoint follows a linear ramp with a quadratic ease-out over the last 6 °C; the commanded rate never exceeds the programmed rate. | `ramp_ease_position` (F-048 fix). The manual live-update path breaks this (F-065). | kept + fix |
| PROF-04 | HEATING ends when T ≥ target − 0.5 °C (independent of time; it never times out, the stall guard covers lack of progress). | `handover_tol`. | kept |
| PROF-05 | HOLDING holds setpoint = target for `t_min` from the moment HOLDING started; paused time is not counted. | `profile_tick` HOLDING branch. | kept |
| PROF-06 | COOLING: heater off, setpoint = target, ends when T ≤ target + 2 °C. Programmed cooling rate is ignored (no active cooling). | COOLING branch plus `kill_heater` on entry. | kept / decide |
| PROF-07 | After the last stage: COOLDOWN (heater off, fan on); complete when T < 40 °C. | `PROFILE_COMPLETE_TEMP_C`. | kept — T-11 |
| PROF-08 | Duration estimate: rate-based per stage from the current temperature; on each stage change the estimate is corrected by the planned-versus-actual difference. Displayed remaining time excludes cooldown. | `calculate_program_duration_ms`, `update_estimate_on_stage_change`. | kept |
| PROF-09 | Status published every tick: temperature, setpoint, power 0–1, elapsed, total, active-stage ordinal / count, phase, stage remaining time (HOLD only). | `post_status_update`. | kept |

## CTRL — closed-loop control and actuation

| ID | Requirement | Current | Status |
| --- | --- | --- | --- |
| CTRL-01 | PID runs every 1 s on (setpoint, measured, dt). Output 0…1 clamped to [min, max]. | `pid_controller_compute`. | kept |
| CTRL-02 | Derivative on measurement; conditional-integration anti-windup; non-finite input, state or output resets and yields 0. | Same. | kept |
| CTRL-03 | Optional additive feedforward: linear in setpoint between two calibration points, clamped flat outside. | `pid_feedforward_model`. Field: on, (80 °C → 8 %), (150 °C → 12 %); the high point is uncalibrated. | kept — D-05 / T-03 |
| CTRL-04 | PID reset at profile start and stop. Bumpless reset (derivative cleared, integrator seeded or zeroed by FF state) at the ramp→hold handover and on resume. | Coordinator. | kept |
| CTRL-05 | Time-proportioned SSR: on for power × W, off for the remainder of each window W = 5 s. | Heater PWM task. Demand changes take effect only at the next window start (up to 5 s latency; 4 of every 5 PID outputs are discarded; F-078). | decide — T-14 |
| CTRL-06 | Contactor closes at program start (after the control task and timer exist) and opens on every inhibit, stop and completion. | `start_heating_profile`, inhibit functions. | kept |
| CTRL-07 | During COOLING and COOLDOWN no PID output is sent and the heater is held off. | `coordinator_component_heater_controller.c:513-533`. | kept |

## GUARD — run-time fault guards

| ID | Requirement | Current | Status |
| --- | --- | --- | --- |
| GUARD-01 | Overshoot: if T > setpoint + threshold during an active heating or holding stage, emergency stop. | Threshold 20 °C field / 30 °C tracked. **Also evaluated in COOLING**, where setpoint = the lower target, so any cooling step larger than the threshold aborts the program (F-057). | **fix — D-06**: never end the program in COOLING; alarm if T does not fall (T-04) |
| GUARD-02 | Stall: in HEATING, over each window (300 s), trip if lag > 5 °C **and** expected rise > 1 °C **and** actual rise < 50 % of expected. Trip = fault pause. | Same. A planned redesign to a sustained-lag guard was never done (see [03](03-CONTROL-ALGORITHMS.md)). | kept / redesign |
| GUARD-03 | Hold deviation: in HOLDING, trip when \|T − target\| > 4 °C continuously for 300 s. Trip = fault pause. | Same. | kept |
| GUARD-04 | A fault pause de-energizes outputs, resets the PID, and reports code, temperature, setpoint, stage and duration. | `enter_fault_pause`. | kept |
| GUARD-05 | During COOLING, the guard never ends the program. If the temperature rises or stays flat instead of falling for a long time, raise an operator alarm. | Not implemented; the current overshoot guard aborts instead (F-057). | new — D-06; parameters T-04 |

## RUN — run lifecycle commands

| ID | Requirement | Current | Status |
| --- | --- | --- | --- |
| RUN-01 | Start is refused unless temperature is valid, fresh, and recovered. | `start_heating_profile`. | kept |
| RUN-02 | Start while running is ignored (returns OK). | `start_heating_profile` first check. | decide (should report) |
| RUN-03 | Pause and resume toggle; resume re-enables output only if no other inhibit is active. | `pause_heating_profile` / `resume_heating_profile`. Resume while the sensor inhibit is still held lets profile time run with no heat (F-061). | fix |
| RUN-04 | Stop ends the run irrecoverably (the HMI asks for confirmation) and is queue-independent for output-off. | `stop_heating_profile` (F-055). | kept |
| RUN-05 | Operating hours count once per second while a program is active, including pauses; persisted at most once per minute and on stop/complete. | HMI `nextion_run_tick`, `program_add_operational_time_sec`. Lost if no HMI (F-076/07). | kept, rehome — D-01 |

## MAN — manual mode

| ID | Requirement | Current | Status |
| --- | --- | --- | --- |
| MAN-01 | The operator sets a target (20–170 °C) and rate (0.1–3.0 °C/min); the controller ramps there and **holds until the operator stops it**. | A one-stage program with `t_min = 10080`. On reaching target the profile advances past the only stage into COOLDOWN and **turns the heater off** (F-064). | **fix — D-09** (hold until stop; redesign T-06) |
| MAN-02 | Changes while running retarget from the current temperature without stopping, at the requested rate. | Mailbox plus `profile_update_stage_target`. The ease band is not added to the planned time, so the effective rate is too high (F-065). | fix — D-09 / T-06 |

## FAN — fan control

| ID | Requirement | Current | Status |
| --- | --- | --- | --- |
| FAN-01 | The fan is on whenever the run is in HEATING, HOLDING or COOLING. | Today it is on from program start until stop or completion, including pause (`fan_controller.c`). | **changed — D-12** |
| FAN-02 | Outside those phases (idle, cooldown, after stop/complete), the fan follows hysteresis: on at or above the upper threshold, off below the lower one, about 3 °C apart near 40 °C, using valid temperature. | Today 43/41 °C (field build) on the float event only; the state freezes on sensor loss. | kept — D-12; values and paused/fault behavior T-09 |
| FAN-03 | "Max/Silent" fan mode chosen on the HMI is applied to the fan. | Stored and displayed only; never applied (F-070). | decide — T-09 |
| FAN-04 | Airflow proof / fan failure detection. | None (F-043). | decide — T-09 |

## PERS — persistence

| ID | Requirement | Current | Status |
| --- | --- | --- | --- |
| PERS-01 | Persist: ambient-reference temperature, cooldown rate, fan-mode preference, operating hours, program library. | NVS `user_prefs` keys `ambient_c`, `cool_rate`, `fan_mode`, `op_time_s`; programs on panel SD; names in NVS `prog_registry`. | kept, rehome — D-01/D-03 (programs in controller memory) |
| PERS-02 | Operating hours survive factory reset and storage-format upgrades; they are never silently lost. | Factory reset preserves them. The boot-time NVS-erase fallback wipes them (F-073). | fix |
| PERS-03 | Program replacement is atomic: the old copy survives a failed save. | Panel `.tmp` then `refile`; a small window remains (F-024). | rehome — D-03 |
| PERS-04 | Service override of operating hours, bounded, applied only when enabled. | `NEXTION_OP_TIME_OVERRIDE_*` (F-050). | decide |

## DIAG — diagnostics and logging

| ID | Requirement | Current | Status |
| --- | --- | --- | --- |
| DIAG-01 | Logging never blocks a safety or control path. | The logger waits up to 100 ms when its queue is full; log volume is high (PID, every MS9024 read, every device tick at INFO; default level DEBUG) (F-074). | fix |
| DIAG-02 | On a critical fault, panic, watchdog or brownout, the last N log lines and the cause are stored persistently with firmware/config identity and time. | `logger_store_full_log` has no caller; RTC snapshot only on `esp_restart`; cause and timestamps always 0 (F-068). | fix |
| DIAG-03 | Service console lists and dumps stored crash logs. | `log-ls`, `log-file`. | kept |
| DIAG-04 | A sensor bus scan/dump tool exists for commissioning and diagnostics (read-only). | `transmitter_diagnostics` (linked, never started) and `ms9024_diagnostic_scan` (no caller). | kept (tool) |
| DIAG-05 | Every image identifies its firmware version, configuration and board. | None (WI-003/WI-005). | new |

## API — controller authority and remote display

| ID | Requirement | Current | Status |
| --- | --- | --- | --- |
| API-01 | The controller is the sole authority: it owns program storage and validation, run state, settings, operating hours and fault handling. The display is a remote client that can be disconnected without affecting safe operation. | The Nextion component owns most of these (F-076). | new — D-01, D-03 |
| API-02 | The controller and display communicate over a versioned custom protocol (commands, state, telemetry, faults, program transfer). | Nextion ASCII line protocol. | new — D-02; design T-02 |
| API-03 | Per-furnace PID/feedforward values are preserved and stored on the controller. | Compile-time Kconfig per local build (F-067). | new — D-05; form T-03 |

