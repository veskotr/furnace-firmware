# BUG-070: Cooling can retain positive heater demand

- **Subsystem:** profile/controller to heater output authorization
- **Severity:** critical conditional output-safety gap
- **Confidence:** confirmed source-level trigger; physical heating unmeasured
- **Status:** confirmed; correction not authorized
- **Affected files and symbols:** `profile_update_stage_target`, `profile_tick`, `apply_pending_target_update`, `kill_heater`, heater PWM task
- **Prerequisite or linked IDs:** F-003 pause inhibit; F-002 dispatcher saturation; F-017 SSR failure; F-074 high-write ordering
- **Ready to fix:** no; output-off contract, concurrency ordering, and test seam require review

## Observed behavior

During a running manual profile, a live target decrease can change the active
phase to COOLING without issuing any heater-off request. The coordinator then
skips PID publication while the previous positive target remains in the heater
PWM worker and the contactor remains requested high. Ordinary stage cooling
does request queued CLEAR/zero, but does not synchronously revoke output
authorization; congestion can defer those commands.

## Expected behavior

Entering a no-heat phase must revoke positive heater authorization and request
SSR/contactor off before stale commands or PWM can continue. The current code
comments claim cooling heater-off. The exact physical deadline still needs a
defined requirement and measured validation; source review proves only requests.

## Reproduction and evidence

1. Start a manual run with positive demand. In-tree [Nextion live update](../../components/nextion_hmi/src/events/nextion_manual_handlers.c)
   sends a lower target via the coordinator command mailbox.
2. [Control task](../../components/coordinator_component/src/coordinator_component_heater_controller.c)
   calls `profile_update_stage_target` before `profile_tick`.
3. [Profile update](../../components/temperature_profile_controller/src/temperature_profile_core.c)
   detects COOLING if new target is more than the configured 2 °C below the
   current temperature, but does not advance stage. `profile_tick` begins each
   result with `stage_changed=false` and does not set it for this phase change.
4. Coordinator calls `kill_heater()` for cooling only inside
   `if (tick_result.stage_changed)` and skips PID/SET_POWER in COOLING.
   [Heater PWM](../../components/heater_controller_component/src/heater_controller_task.c)
   still reads the old positive target. No off command is issued until a later
   fault, pause, STOP, or stage advance. The contactor is not stopped on phase
   entry.

For ordinary stage-change cooling, `kill_heater` submits bounded-dispatcher
CLEAR and SET_POWER-zero commands; when its queue cannot drain, that path can
block while old PWM demand remains. This is a separate reachable variant of
the same no-heat authorization invariant, not a proof of observed thermal
overshoot.

A read-only comparison of ancestor `field-tests` (`243a15a5`) shows the same
live phase update, `stage_changed`-conditioned cooling `kill_heater`, and
cooling PID skip in that branch's source. The user reports operating settings
match that branch, but the flashed source revision and resolved configuration
are unknown. This comparison raises relevance; it is **not** a claim that the
oven has been observed heating during cooling.

## Root cause and violated invariant

Output revocation is coupled to `stage_changed`, not to every transition into
a no-heat phase, and is queued rather than part of the authoritative heater
gate. The invariant is that a no-heat phase cannot retain or admit positive
heater demand, including commands already in flight.

## Minimal root-fix boundary, safety impact, and uncertainty

Characterize all phase-entry paths and define one output-revocation operation
at the coordinator/heater boundary, including direct off and rejection of
stale positive commands. F-074 is the separate check/write race that must
be corrected before any source fix can claim final-off ordering.
Because the current oven lacks independent hardware safety protection, a
software-requested high after cooling entry is a serious gap; actual relay/SSR
state and heater power are not measured. Configuration, field image, wiring,
and physical off latency remain unknown.

## Allowed scope and correction authority

Analysis/documentation only. A behavior-changing output correction requires
explicit authorization, safety/concurrency/control impact review, a
deterministic failure-before oracle, and an approved hardware validation plan.
No ADR is yet selected; escalate if the actuator command/ownership model
changes beyond the current hardening boundary.

## Regression test and hardware validation

`SCN-W1-001` in [current behavior](CURRENT-CONTROLLER-BEHAVIOR.md): host vector
for live HEATING/HOLDING→COOLING plus mocked coordinator/dispatcher/GPIO call
trace, positive demand, and a full-queue variant. Failure-before: no direct
off request on live transition and delayed ordinary-stage off under queue
congestion. Passing correction: positive demand revoked and low requests made
before a later high write; no cooling PID or old command reauthorizes output.
No test was implemented/run. Powered-controller observation of pin polarity,
contactor/SSR feedback, and bounded physical off is a separate release gate;
no flashing or oven operation is authorized here.

## Resume conditions

Complete package D interleaving review, select a no-heat output contract and
software deadline, define the mocked injection seam, authorize a bounded fix,
then plan—not perform—hardware validation.
