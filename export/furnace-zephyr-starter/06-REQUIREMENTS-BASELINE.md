# 06 — Requirements baseline for the new controller

These are **target** requirements, the starting point for the new project's living requirements document. IDs are stable so tests can trace to them.

- **Basis** lists the decisions (D), deferred topics (T), rules (R) and legacy lessons (F) behind each requirement.
- **Legacy** notes how the old firmware differed. It is not something to copy.
- Values marked T-xx are deliberately open: implement them as configurable, mark them `*_PLACEHOLDER` and ask the product owner when the work reaches them ([01](01-RULES.md) §2–§3). Legacy values are given only as reference.

## SAFE — physical output safety

| ID | Requirement | Basis | Legacy |
| --- | --- | --- | --- |
| SAFE-01 | At boot, after reset, after partial initialization and on any failure, SSR and contactor are de-energized. Heating is impossible until fresh, valid temperature and an explicit run authorization exist. | R13, T-15 | Same intent; the reset-to-configuration window depended on unknown pulls |
| SAFE-02 | Independent inhibit reasons gate the actuator: permanent hardware fault, sensor data, run control, supervisor/over-temperature. Releasing one never releases another. | R3 | Three reasons existed |
| SAFE-03 | Asserting any inhibit zeroes demand and drives SSR **and** contactor off synchronously, with no queue, bus, logger or display wait on the path. | D-08, R4 | Logged first and could block before opening the contactor (F-062, F-074) |
| SAFE-04 | After an inhibit returns, no path can drive the SSR or contactor on until that inhibit is released by its owner. Authorization and the GPIO write are atomic; stale demand (wrong run epoch) is rejected. | D-08, R3 | Check-then-act race (F-063); stale PID output (F-003) |
| SAFE-05 | An output-driver failure latches a permanent inhibit, opens the contactor and is reported; only a restart or a defined service action clears it. | R8 | Latched, but the report reached nobody |
| SAFE-06 | Restart, factory reset and firmware update inhibit the heater first and refuse if inhibition fails. | — | Same |
| SAFE-07 | An always-on supervisor enforces an absolute temperature limit in every state (idle, paused, running, manual) and opens the contactor directly. Limit and response time: T-10. | R7, T-10 | Only relative to the moving setpoint, only while running (F-077) |
| SAFE-08 | Every thread is supervised by the task watchdog; a stuck thread leads to outputs off, then reset. | R11, T-15 | Only idle tasks watched (F-036) |
| SAFE-09 | Every condition that stops or prevents heating produces a distinct, reportable state with a cause. "Completed" is never reported for an abort. Presentation: T-01. | R8, D-13, T-01 | Over-temperature reported as completed (F-058); sensor-loss pause silent (F-059) |

## TEMP — temperature acquisition and validity

| ID | Requirement | Basis | Legacy |
| --- | --- | --- | --- |
| TEMP-01 | Acquire N chamber sensors periodically (count, bus and rate: T-07). | T-07, D-14 | 5 sensors, ~1 Hz, Modbus |
| TEMP-02 | Accept a reading only if the transport succeeded, the value is finite and the sensor reports a valid measurement, and the value lies in a plausible range (T-07/T-08). | T-07 | NaN/Inf/−0.0 rejected; −200…1500 °C |
| TEMP-03 | Each sample is one immutable record (value, monotonic timestamp, status, sensor ID) published atomically. | R6 | Unsynchronized value/tick pair (F-071) |
| TEMP-04 | The aggregate uses only fresh, non-excluded samples. It is valid only with a quorum (T-05). | D-07, T-05 | Mean of fresh samples, quorum N−2 |
| TEMP-05 | A sensor whose reading differs drastically from the others is excluded from the aggregate and reported. Exclusion and re-inclusion follow explicit, tested rules (T-05). | D-07, R19, T-05 | Disagreement only logged (F-004) |
| TEMP-06 | An accepted aggregate expires after a freshness budget even if no further message arrives; expiry de-energizes the heater directly. | R4, R6 | 5 s one-shot timer (kept as a good pattern) |
| TEMP-07 | After invalidity or expiry, a recovery criterion (for example K consecutive valid aggregates) must hold before the sensor inhibit can be released. Whether the run then resumes automatically or needs the operator: T-05. | T-05 | K = 3; auto-resume documented but unreachable (F-060) |
| TEMP-08 | Every consumer (control, fan, display, logs) receives validity with the value; a stale or invalid value is never presented as current. | R6, T-01 | Display froze on the last value (F-059) |

## PROG — programs

| ID | Requirement | Basis | Legacy |
| --- | --- | --- | --- |
| PROG-01 | The controller owns the program model, validation and library; the display only edits and requests (API-01). | D-01, D-03 | Lived in the display component (F-076) |
| PROG-02 | A program has a name and an ordered list of stages. Each stage has an **explicit kind** (RAMP / HOLD / COOL), target temperature, duration and/or rate. Field resolution: T-08. | R9, T-08 | Kind inferred from measured temperature (F-080); integer °C, 0.1 °C/min, minutes |
| PROG-03 | Save-time validation enforces device limits and consistency (limits: T-10/T-08). | — | See [03](03-LEGACY-SYSTEM-BEHAVIOUR.md) §6.2 |
| PROG-04 | Start-time validation runs **on the controller** before any output authorization. | D-01 | Only in the display |
| PROG-05 | Program storage is atomic (the old copy survives a failed save) and versioned. Format and import of legacy programs: T-13. | R14, T-13 | Panel SD, temp file + rename |

## PROF — profile execution

| ID | Requirement | Basis | Legacy |
| --- | --- | --- | --- |
| PROF-01 | RAMP stages ramp from the **measured** start temperature at the programmed rate, never exceeding it, with a soft landing near the target. A ramp whose start is already at target is handled explicitly (decide when designing; ask under T-08 if precision-related). | R9, R10 | Ease-out over the last 6 °C; near-target ramps became holds (F-080) |
| PROF-02 | A RAMP stage ends when the chamber reaches the target within a handover tolerance. Lack of progress is handled by the stall guard. | — | 0.5 °C |
| PROF-03 | A HOLD stage holds the target for its duration, counting only time during which heating is authorized. | — | Counted only while not paused; could count while the heater was inhibited (F-061) |
| PROF-04 | A COOL stage keeps the heater off and ends when the chamber reaches the target within tolerance. | D-06 | Same |
| PROF-05 | After the last stage, a cooldown phase keeps the heater off; completion criterion: T-11. | T-11 | Below 40 °C |
| PROF-06 | Duration estimate from the measured temperature; corrected at stage changes. | — | Same, duplicated in the display |
| PROF-07 | Status is published periodically: temperature with validity, setpoint, demand, elapsed/total/remaining, stage index/count, phase, inhibit reasons. Content: T-01. | T-01 | Status without validity or inhibit reasons |

## CTRL — closed-loop control and actuation

| ID | Requirement | Basis | Legacy |
| --- | --- | --- | --- |
| CTRL-01 | PID instance (not singleton): derivative on measurement, conditional-integration anti-windup, output clamp; non-finite input/state/output resets and yields zero. | R1 | Same math; singleton |
| CTRL-02 | Additive feedforward from a per-furnace model of steady-state duty versus setpoint. | D-05, T-03 | Two-point linear model; field-validated as essential |
| CTRL-03 | PID reset at start/stop; bumpless reset (derivative cleared, integrator seeded or zeroed by FF state) at ramp→hold handover and on resume. | — | Same |
| CTRL-04 | Time-proportioned SSR plus contactor. Window length, mid-window updates and SSR constraints: T-14. | T-14 | 5 s window, demand sampled once per window (F-078) |
| CTRL-05 | The contactor closes only after all run resources exist and the run is authorized; it opens on every inhibit, stop, abort and completion. | — | Same order (after fixes) |
| CTRL-06 | No PID demand is produced in COOL stages or in cooldown. | D-06 | Same |
| CTRL-07 | PID/FF values are stored per furnace on the controller (form: T-03, T-16). | D-05 | Compile-time per local build |

## GUARD — fault guards

| ID | Requirement | Basis | Legacy |
| --- | --- | --- | --- |
| GUARD-01 | Overshoot: during RAMP and HOLD, a temperature above the setpoint by more than a threshold is a fault (response policy within R8; threshold: T-10). | R8 | 20 °C; reported as completed (F-058) |
| GUARD-02 | During COOL and cooldown, **no guard ends the program**. A temperature that rises or stays flat instead of falling for too long raises an alarm (parameters: T-04). | D-06, R20, T-04 | Overshoot guard aborted cooling steps (F-057) |
| GUARD-03 | Stall: during RAMP, sustained failure to follow the setpoint raises a fault pause (design: sustained under-temperature lag; parameters configurable). | — | Lag AND rate-fraction test over 300 s, judged weak |
| GUARD-04 | Hold deviation: during HOLD, \|T − target\| beyond a band for a sustain time raises a fault pause. | — | 4 °C for 300 s |
| GUARD-05 | Every fault pause de-energizes outputs, resets the PID and reports the code, temperature, setpoint, stage and duration. | R8 | Same content; delivery unreliable |

## RUN — run lifecycle

| ID | Requirement | Basis | Legacy |
| --- | --- | --- | --- |
| RUN-01 | One run-state machine (IDLE, RUNNING{phase}, PAUSED{reasons}, MANUAL, COMPLETED, STOPPED, ABORTED{cause}) owned by one thread; all others send messages. | R2 | Plain flags written from four contexts (F-072) |
| RUN-02 | Start is refused without fresh valid temperature, sensor recovery, valid program and clear inhibits; refusal is reported (never silently ignored). | — | A start while running returned OK silently |
| RUN-03 | Resume is refused while any non-control inhibit is active. | — | Allowed (F-061) |
| RUN-04 | Stop ends the run irrecoverably and is output-independent of queues. | R4 | Same (after fixes) |
| RUN-05 | Service hours count on the controller while a run is active (pause included unless decided otherwise), persisted with bounded wear, never lost implicitly. | D-01, R14 | Counted by the display component |

## MAN — manual mode

| ID | Requirement | Basis | Legacy |
| --- | --- | --- | --- |
| MAN-01 | Manual mode ramps to the operator's target at the requested rate and **holds until the operator stops it**. | D-09 | Turned the heater off on arrival (F-064) |
| MAN-02 | Live target/rate changes re-plan from the measured temperature through the same ramp planner (rate bound holds). | R10 | Ramped up to 2× too fast (F-065) |
| MAN-03 | Full manual-mode process, limits and fault behavior: T-06. | T-06 | — |

## FAN

| ID | Requirement | Basis | Legacy |
| --- | --- | --- | --- |
| FAN-01 | The fan is on during HEATING (RAMP), HOLD and COOL phases. | D-12 | On from start to stop/complete, including pause |
| FAN-02 | Outside those phases, the fan follows two thresholds about 3 °C apart near 40 °C (on above the upper, off below the lower), using valid temperature only. Exact values, paused/fault behavior: T-09. | D-12, T-09 | 43 / 41 °C; state froze on sensor loss |
| FAN-03 | Speed control and fan-failure detection: T-09. | T-09 | None; the "Max/Silent" toggle was never applied (F-070) |

## PERS — persistence

| ID | Requirement | Basis | Legacy |
| --- | --- | --- | --- |
| PERS-01 | The controller persists settings, per-furnace tuning, service hours and the program library, with versioned schemas and explicit migrations. Layout on 16 MB flash: T-12. | D-01, D-03, D-04, R14 | Split between controller NVS and panel SD |
| PERS-02 | Service hours survive factory reset, firmware update and schema change. | R14 | Lost on a storage-format error (F-073) |
| PERS-03 | Migration of legacy data (programs, hours, tuning): T-13. | T-13 | — |

## DIAG — diagnostics

| ID | Requirement | Basis | Legacy |
| --- | --- | --- | --- |
| DIAG-01 | Logging is deferred, rate-limited and lossy; it never blocks a control or safety path. | R4, R5 | Waited up to 100 ms; very high volume (F-074) |
| DIAG-02 | Crash records hold reset cause, uptime, wall time if available, firmware/config identity and the last N lines, tested per reset cause. Storage and format: T-12 / D-14 marker. | R15 | Feature never wired (F-068) |
| DIAG-03 | Service console: status, inhibit reasons, sensor statistics, crash-log list/dump. | — | Crash-log list/dump only |
| DIAG-04 | Every image identifies firmware version, configuration and board. | R13 | None |
| DIAG-05 | Control trace records (P, I, D, FF, output, setpoint, temperature) at a fixed rate for tuning. | 04 §7 | Text log per tick |

## API — controller authority and remote display

| ID | Requirement | Basis | Legacy |
| --- | --- | --- | --- |
| API-01 | The controller is the sole authority; the display is a remote client. Disconnecting the display never affects safe operation or a running program. | D-01, R21 | The display component held domain logic |
| API-02 | Controller and display use a versioned custom protocol: commands, state, telemetry, faults, program transfer. Design: T-02; content: T-01. | D-02, T-01, T-02 | Panel ASCII line protocol |
| API-03 | Every command is validated by the controller and acknowledged with a result; destructive commands require exact, confirmed requests. | — | Substring routing except destructive commands |
| API-04 | Wall-clock time source for records (display-supplied, RTC or other): T-02 / T-12. | — | Only the panel had a clock |
