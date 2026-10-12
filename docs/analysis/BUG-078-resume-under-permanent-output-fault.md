# BUG-078: RESUME reports success under a permanent output inhibit

- **Subsystem:** coordinator command/state, heater permanent fault gate
- **Severity:** high command/status and cure-cycle integrity defect
- **Confidence:** confirmed source-level trigger; no field symptom claimed
- **Status:** confirmed; correction not authorized
- **Affected files and symbols:** `heater_controller_handle_ssr_failure`, `heater_controller_set_control_inhibit_for_context`, `resume_heating_profile`, `coordinator_command_handler`
- **Prerequisite or linked IDs:** F-017 permanent SSR fault latch; F-035 fault-state architecture; F-069 sensor pause; F-077 direct off-error response
- **Ready to fix:** no; fault-state/command outcome ownership requires policy and test seam

## Observed behavior and reachable trigger

Inject an SSR GPIO write error in PWM or queued CLEAR so the heater's
`output_inhibited` permanent reason latches. The coordinator has no production
`FURNACE_ERROR_EVENT` subscriber that pauses or marks its run faulted. An
operator PAUSE followed by RESUME calls `heater_controller_set_control_inhibit(false)`.
That call returns `ESP_OK` whenever the context/mutex exists, even while the
permanent reason remains true. Coordinator then clears `ctx->paused` and
publishes `PROFILE_RESUMED`; the heater still rejects heat. Source-level
command/status success therefore disagrees with output authorization.

## Evidence and disconfirming guard

[SSR failure handler](../../components/heater_controller_component/src/heater_controller_task.c)
sets `output_inhibited=true`, zeros target, retries off and publishes a
furnace-error event. There is no production subscriber that changes
coordinator process state. [Control inhibit setter](../../components/heater_controller_component/src/heater_controller_task.c)
does not test other inhibit reasons when clearing only its control reason.
[Resume](../../components/coordinator_component/src/coordinator_component_heater_controller.c)
checks only that setter's return, then marks unpaused;
[command handler](../../components/coordinator_component/src/coordinator_component_events.c)
publishes RESUMED on success. The combined heater predicate still blocks
positive commands, so this report does **not** claim heater re-energization.

## Root cause and violated invariant

Coordinator command outcome is derived from clearing a temporary control
reason rather than authoritative output/fault state; permanent heater faults
are reported by event but not owned as coordinator run state. The invariant
is: RESUME succeeds and publishes RUNNING/RESUMED only when the controller
has actually authorized the run under all independent gates, or otherwise
returns a truthful fault/inhibited status without clearing its pause reason.

## Minimal root-fix boundary and safety impact

Define a fault-state/authorization result at the coordinator-heater boundary
and distinguish accepted command, applied transition, and persistent fault
reason. Preserve heater's fail-closed permanent latch; do not bypass it to
make status look resumed. F-077 separately covers direct off-write errors
that fail to latch at all. Current oven has no independent hardware safety
protection; source status does not establish physical output state or cure
quality. A running profile may continue elapsed bookkeeping without heat
under the permanent latch, which warrants separate acceptance policy.

## Allowed scope, ADR, and correction authority

Documentation/diagnosis only. A behavior-changing fault/state correction
requires explicit patch authority, safety/control/HMI contract impact review,
fault taxonomy and deterministic oracle. Existing ADR-0001 defers a central
fault manager; a lasting state/command contract decision should be recorded
before broad redesign. A narrow honest rejection may be scoped sooner if its
external semantics are accepted.

## Regression test and hardware validation

`SCN-W3-002`: mock PWM SSR failure to latch permanent output reason, then
PAUSE and RESUME. Failure before correction: RESUME returns success and emits
RESUMED while heater remains inhibited. Passing correction: command rejected
with explicit permanent-fault reason, process state remains fault/paused, no
positive command is admitted; only an authorized serviced recovery policy
can clear the reason. Mocked event/heater/coordinator, no test implemented/run.
Powered-controller validation of physical fault/off behavior is a separate
approved gate; no hardware action is authorized here.

## Resume conditions

Specify fault-state and command outcome semantics for the external HMI,
authorize a bounded correction and test seam, then reconcile with F-017/
F-077 output fault response and later hardware feedback. Service hours are
deferred and do not govern this defect.
