# AUDIT-005 — PID, profile, and cooling-control map

**Scope:** source-only review of the active coordinator, profile state machine,
PID, heater-authorisation boundary, and fan policy.  No gains, profile values,
or production code were changed.  The retained `sdkconfig.defaults` is the
configuration baseline cited below; no powered hardware claim is made.

## End-to-end control path

`COMMAND_TYPE_COORDINATOR_START_PROFILE` is dispatched to
`start_heating_profile()` (`coordinator_component_events.c:273-405` and
`coordinator_component_heater_controller.c:651-778`).  Start rejects a missing
valid aggregate, an outstanding sensor recovery, or an expired aggregate before
loading the copied program.  It resets PID state, loads the singleton profile
controller, creates the worker and its periodic `ESP_TIMER_TASK` timer, then
releases the direct control inhibit and submits heater `CLEAR`/`START` commands.

Each timer expiration notifies the coordinator task; it measures elapsed
FreeRTOS tick time, calls `profile_tick()`, runs profile guards, calculates PID
duty if heating/holding, and submits `SET_POWER`.  The heater component accepts
that demand only when permanent, sensor-data, and control inhibits are all
clear; either recoverable inhibit directly clears demand and requests SSR and
contactor off (`heater_controller_task.c:124-217`).

## Units and timing

| Item | Source and conversion | Resolved baseline |
| --- | --- | --- |
| PID wake cadence | `CONFIG_COORDINATOR_PID_TICK_INTERVAL_MS`; periodic ESP timer notification | 1000 ms |
| PID `dt` | measured `xTaskGetTickCount() * portTICK_PERIOD_MS` delta / 1000 | actual worker wake interval, not nominal 1 s |
| PID gains | Kconfig thousandths divided by 1000 | Kp 0.115, Ki 0.001, Kd 0 (resolved config) |
| PID output | 0..1 duty; Kconfig percent divided by 100 | 0..1 (0–100%) |
| stage ramp rate | `delta_t_per_min_x10`; `planned_ms = Δ°C * 600000 / rate_x10` | profile-defined; editor limit ±3.0 °C/min |
| soft landing | final min(6 °C, span) uses quadratic ease-out; extends ramp by one linear ease-band duration | 6 °C |
| heat→hold handover | actual temperature >= target − (`HANDOVER_TOLERANCE_X10` / 10) | 0.5 °C |
| shared phase/cooling tolerance | `CONFIG_NEXTION_TEMP_TOLERANCE_C` | 2 °C |
| aggregate freshness | one-shot expiry uses aggregate stale timeout | 5000 ms; recovery requires 3 aggregates |
| hard overshoot guard | `current > current setpoint + threshold` | 30 °C |
| stall / hold alarm | 180 s window, ≥5 °C lag, <50% expected rise; hold outside ±4 °C for 300 s | as stated |
| completion | cooldown temperature strictly below threshold | <40 °C |

The graph API remains legacy linear interpolation by stored `t_min`
(`temperature_profile_core.c:101-157`), whereas runtime heating derives its
duration from actual stage-entry temperature and adds soft-landing time
(`:278-305`).  Therefore the graph is not a runtime trajectory oracle; this is
already the documented residual of F-048.

## Profile transition map

| Phase | Setpoint / advance rule | Heater and fan policy |
| --- | --- | --- |
| HEATING | eased moving setpoint; advances on actual target minus 0.5 °C | PID demand; stall monitor active |
| HOLDING | fixed target; advances after planned dwell | PID demand; continuous deviation monitor; stage entry resets/seeds PID |
| COOLING | fixed lower target; advances when actual temp <= target + 2 °C | coordinator skips PID and sends `kill_heater()` on entry |
| COOLDOWN | setpoint 0; completes below 40 °C | coordinator skips PID and sends `kill_heater()` on entry |
| COMPLETE | setpoint 0, profile-complete flag | direct control inhibit, zero target, completion event |

The independently event-driven fan controller holds the fan on from profile
started/resumed through stopped/completed, then returns to auto hysteresis using
the last temperature event (`fan_controller.c:20-108`).  The coordinator’s
`heating_task_state.fan_on` assignment on cooling/cooldown is only status state:
the source explicitly retains a TODO instead of commanding the fan.  Thus fan
operation is dependent on coordinator events reaching the fan callback, not on
that status bit or an airflow proof/interlock.

## Pause, recovery, stop, and completion

- Manual pause and fault pause set `ctx->paused`, assert the direct control
  inhibit, clear queued heater target, and reset PID.  A paused worker consumes
  wakeups without advancing profile elapsed time.
- Resume does a bumpless PID reset at the current target, attempts to clear only
  the control inhibit, then clears the paused state.  A sensor-data inhibit still
  prevents heater demand; automatic sensor recovery releases it only after the
  configured consecutive aggregate count.
- Temperature invalidity/expiry asserts the separate sensor-data inhibit before
  coordinator pause bookkeeping.  The expiry callback is in `ESP_TIMER_TASK`,
  not ISR context; comments claiming ISR context beside the PID callback are
  stale but do not describe the configured execution context.
- Overshoot uses direct control inhibit plus a critical furnace-error event and
  marks the run completed.  Stall and sustained hold drift enter recoverable
  fault pause, emit coordinator error + paused events, and skip the PID command
  for that tick.
- Completion directly inhibits, clears demand, emits completed, then the worker
  calls `stop_heating_profile()` to stop/delete the timer and acknowledge exit.

## Confirmed defect proposed for central register: STOP wakeup can reintroduce dispatcher-queue join deadlock

**Proposed ID:** F-061 — Coordinator STOP wakes an already-blocked control
worker into one final command-producing tick.

**Exact evidence:** `stop_heating_profile()` stops the timer, sets
`ctx->running = false`, and calls `xTaskNotifyGive(ctx->task_handle)` at
`coordinator_component_heater_controller.c:850-872`, but only asserts the
direct control inhibit later at `:878`.  The worker tests `ctx->running` only at
the `while` entry (`:325`), blocks in `ulTaskNotifyTake()` (`:327`), and does
not recheck it immediately after wake.  It can consequently call
`profile_tick()` then `send_heater_command(SET_POWER)` (`:358-532`) before it
reaches its self-exit `stop_heating_profile()` acknowledgement (`:545-547`).
Those submissions use the bounded dispatcher queue.

**Reachable trigger:** fill the dispatcher queue, then have its sole consumer
dispatch STOP while the coordinator worker is blocked waiting for its PID
notification.  STOP clears `running` and wakes that worker.  The worker performs
its stale final tick and blocks attempting to enqueue `SET_POWER`; the
dispatcher then waits indefinitely for the worker exit semaphore at `:894-897`.

**Impact and boundary:** the dispatcher can again deadlock, defeating complete
stop/restart teardown.  Once the dispatcher continues past its wake it asserts
the direct inhibit before waiting, so this trigger does not by itself establish
heater re-energization; physical off remains source-asserted and needs bench
evidence.  This is a recurrence edge of the F-055 queue-independent shutdown
invariant, not a claim that the earlier self-exit correction was ineffective in
every path.

**Direction:** establish the direct inhibit before waking the worker, and make a
woken worker exit without profile/PID/dispatcher work when `running` became
false.  Any correction needs queue-full stop/restart characterization.

## Verification gaps and deterministic tests

1. Host-test `profile_tick()` with ramp/ease boundaries, exact 0.5 °C handover,
   early/late stage advance, cooling, zero-stage cooldown, completion, and
   overshoot threshold.  Assert monotonic setpoint and rate never above the
   configured ramp rate.
2. Host-test PID zero/negative/non-finite `dt`, non-finite inputs/state, output
   clamps, conditional anti-windup, derivative-on-measurement, and
   pause/resume reset/seed semantics.  Do not treat a thermal model as tuning
   evidence.
3. Mock coordinator timer/task/dispatcher: verify no `SET_POWER` follows STOP,
   including a pending notification; with a full dispatcher queue, STOP must
   complete boundedly and off must be requested before any join.
4. Mock sensor expiry/recovery: expiry must prevent resume/PID demand until the
   configured number of fresh aggregates and all independent heater inhibits
   are clear.
5. Mock event delivery to fan controller: started/resumed holds fan on;
   stopped/completed restores hysteresis.  Separately test loss/delay of fan
   callbacks because the coordinator state flag is not a fan command.
6. Powered-controller validation is still required for SSR/contactor polarity,
   timer/queue latency to de-energization, fan polarity, airflow, independent
   cutoff, reset/brownout, and actual ramp/hold quality.  No source review or
   mock proves these properties.

## Improvement candidates (not authorized changes)

- Reconcile the legacy linear graph with runtime rate-derived/eased duration,
  or explicitly label the graph as planned rather than actual trajectory.
- Replace stale callback-context comments with the `ESP_TIMER_TASK` contract so
  future changes use valid API constraints.
- Define an explicit fan/airflow safety contract.  Current source has no
  airflow feedback and no direct coordinator-to-fan command at cooling entry.
- Create a small host-test seam around FreeRTOS time, ESP timer, and dispatcher
  submission; F-039/F-052 currently leave the critical logic uncharacterized.

## Wave-1 source reconciliation (2026-09-15)

The unchanged-baseline A/B/C reviews found that the earlier statement
"automatic sensor recovery releases it ... and resumes the profile" is not
current behavior. [F-069](BUG-069-sensor-recovery-pause-gate.md) shows the
sensor-induced pause also sets control inhibit; the recovery callback refuses
to call resume while that composite inhibit remains. [F-071](BUG-071-aggregate-acquisition-freshness.md)
shows the configured three recovery events need not be three distinct physical
read successes. Both affect the intended automatic-recovery policy.

[F-070](BUG-070-cooling-output-authorization.md) is a reachable no-heat output
gap: a live manual target decrement can change the phase to COOLING without
`stage_changed`, bypassing the only cooling `kill_heater` call and retaining the
prior positive PWM demand. Ordinary stage cooling queues off rather than
asserting direct inhibit. The earlier cooling row describes the ordinary
`stage_changed` branch only.

The downward cooling false-overshoot path is now confirmed
[F-076](BUG-076-cooling-overshoot-guard.md): the +30 °C generic guard applies
to a legitimate lower target (for example 150→100 °C). Other C package-local
candidates awaiting host vectors and deduplication: `C-EASE-EST` omits the
runtime ease-band extension in the initial display estimate (30 °C at
3 °C/min: 10-minute display budget versus 12-minute runtime plan);
`C-MANUAL-RATE` omits that extension for live manual ramp updates and can
initially exceed the requested rate; `C-DT-EXP` can accumulate fault downtime
in the first post-expiry profile/PID interval because the expiry wake skips
`last_wake_time` refresh. These are source-level mathematical/timing claims,
not cure-quality or hardware measurements. [Current behavior](CURRENT-CONTROLLER-BEHAVIOR.md)
separates current and intended scenarios. The operating oven has no
independent hardware protections; the ESP32 is not certified safety firmware.
