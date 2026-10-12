# BUG-069: Sensor recovery cannot resume a sensor-paused run

- **Subsystem:** coordinator, heater authorization
- **Severity:** high (availability and cure-cycle integrity)
- **Confidence:** confirmed source-level defect; no field reproduction claimed
- **Status:** confirmed; correction not authorized
- **Affected files and symbols:** `temperature_processor_event_handler`, `pause_heating_profile`, `resume_heating_profile`, `heater_output_is_inhibited`
- **Prerequisite or linked IDs:** F-001/F-054 expiry and recovery; F-003 separate control gate; `POL-SENSOR-RECOVERY-001`
- **Ready to fix:** no; behavior/pause-cause representation and deterministic test seam must be settled

## Observed behavior

In the tracked configuration, auto recovery is enabled after three valid
aggregate events. After a sensor-induced pause, the callback releases the
sensor-data inhibit but the run remains paused and logs that another heater
inhibit remains. This is a reachable source sequence, not a field observation.

## Expected behavior

The user-confirmed policy is automatic resume after trustworthy temporary
sensor recovery, without operator acknowledgement solely for that fault.
Operator pause, STOP, completion, permanent output fault, and unrelated
recoverable faults must not be overridden by sensor recovery.

## Reproduction and evidence

1. Begin a valid running, unpaused profile; deliver an invalid aggregate or
   let its expiry timer fire. The [validity handler](../../components/coordinator_component/src/coordinator_component_events.c)
   inhibits sensor data then calls `pause_heating_profile`; the expiry worker
   likewise pauses at its next wake.
2. [Pause](../../components/coordinator_component/src/coordinator_component_heater_controller.c)
   sets `ctx->paused=true` and the heater's `control_inhibited=true`.
3. Deliver three accepted validity events. [Recovery](../../components/coordinator_component/src/coordinator_component_events.c)
   clears only `sensor_data_inhibited`, then tests
   `!heater_controller_output_is_inhibited()` before calling resume.
4. The [combined predicate](../../components/heater_controller_component/src/heater_controller_task.c)
   still includes `control_inhibited`; the condition stays false. Recovery
   resets its counter and clears its sensor flag, so later valid events do not
   retry the auto-resume branch. An operator RESUME can clear the control flag.

Resolved [sdkconfig.defaults](../../sdkconfig.defaults) has
`CONFIG_COORDINATOR_SENSOR_AUTO_RECOVERY=y` and
`CONFIG_COORDINATOR_SENSOR_RECOVERY_VALID_AGGREGATES=3`; field-image
configuration remains unknown.

## Root cause and violated invariant

The same control-inhibit reason represents operator and sensor-induced pause,
while recovery checks an all-reasons inhibit before calling the routine that
would clear that control reason. The policy invariant is: a sensor-only
recoverable pause releases its own reasons and resumes once data are
trustworthy, without releasing unrelated reasons.

## Minimal root-fix boundary and safety impact

Explicitly distinguish pause causes at the coordinator authorization boundary
and clear/resume only a sensor-owned pause after qualified recovery. Do not
blindly clear the composite inhibit or bypass the heater's permanent fault
latch. The current state keeps the heater requested off, so the direct defect
is cure interruption/availability; an unsafe correction could re-energize it
after an operator or permanent inhibit. F-071 makes the *trustworthiness*
qualification itself a separate prerequisite.

## Allowed scope and correction authority

This pass authorizes source analysis and documentation, not a production patch.
Any change to pause-reason ownership, event ordering, or resume conditions
requires impact/safety/control review and explicit correction authority.
No ADR is recorded for the local representation; escalate if the solution
changes the external state/command contract.

## Regression test

`SCN-W1-002` in [current behavior](CURRENT-CONTROLLER-BEHAVIOR.md): mock event
delivery, monotonic expiry, heater-inhibit calls, and profile state. Failure
before correction: sensor gate clears after qualification while paused/control
gate remain set. Passing correction: automatic resume only for a sensor-induced
pause, with no start/high request during invalid data or another inhibit.
No test is implemented or run.

## Hardware validation, uncertainty, and resume conditions

No hardware action is authorized. Software tests cannot prove GPIO polarity,
actual time-to-off/on, or cure quality. The operating oven has no independent
hardware protection. Resume investigation when pause-cause semantics,
distinct-acquisition recovery policy, and the injection seam are decided;
then authorize a bounded fix and keep powered validation as a release gate.
