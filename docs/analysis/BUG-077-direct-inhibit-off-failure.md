# BUG-077: Direct inhibit off-write failure is not latched

- **Subsystem:** heater direct inhibit, coordinator pause/STOP/recovery
- **Severity:** critical conditional fault-response gap
- **Confidence:** confirmed source-level error path; no electrical failure observed
- **Status:** confirmed; correction not authorized
- **Affected files and symbols:** `heater_controller_set_sensor_data_inhibit_for_context`, `heater_controller_set_control_inhibit_for_context`, `heater_controller_handle_ssr_failure`, `set_control_inhibit`, `pause_heating_profile`, `stop_heating_profile`, `temperature_processor_event_handler`, `resume_heating_profile`
- **Prerequisite or linked IDs:** F-017 PWM/CLEAR SSR fault latch; F-069 auto-resume; F-070/F-074/F-075 output ordering; F-001/F-054 expiry
- **Ready to fix:** no; authoritative off-failure latch/recovery policy and test seam need review

## Observed behavior and reachable error sequence

Both direct sensor/control inhibit setters set their gate and zero target, then
attempt SSR-low and contactor-low and return the first GPIO error. They do
**not** call the permanent heater fault latch on either failed off write.
Coordinator pause/STOP calls a `void` wrapper that logs a control-inhibit
error and then reports success. Sensor recovery can later clear the sensor
reason, and an operator RESUME can clear control reason. A positive PWM/START
request can then be admitted because no permanent fault reason was set.

One deterministic source trigger is: begin with positive demand, inject
**persistent** contactor-low errors on both the invalid-aggregate sensor
inhibit and subsequent pause control inhibit while SSR-low succeeds. The
sensor gate is set and coordinator pauses, but no persistent output fault
latch is set and pause returns `ESP_OK`. Queued CLEAR only retries SSR, not
contactor. A single-error variant can use STOP's one direct control inhibit.
After qualified valid acquisitions release the sensor reason, operator RESUME
clears control reason; PWM may request SSR-high while the contactor-low write
was never known to have succeeded. This is software error/fault-injection
behavior, not proof of a welded or energized physical contactor.

## Evidence and disconfirming paths

[Direct setters](../../components/heater_controller_component/src/heater_controller_task.c)
return `ssr_err != ESP_OK ? ssr_err : contactor_err` after both off attempts
without `heater_controller_handle_ssr_failure` or a contactor failure latch.
By contrast, [PWM](../../components/heater_controller_component/src/heater_controller_task.c)
and [queued CLEAR](../../components/heater_controller_component/src/heater_controller_events.c)
call the SSR fault handler on their own SSR write error. [Coordinator wrapper](../../components/coordinator_component/src/coordinator_component_heater_controller.c)
returns `void` and only logs; pause and STOP do not propagate that result.
[Recovery](../../components/coordinator_component/src/coordinator_component_events.c)
releases sensor inhibit after the configured validity events without a prior
off-success token; [resume](../../components/coordinator_component/src/coordinator_component_heater_controller.c)
clears control inhibit if its context exists, regardless of another reason.
Current F-069 blocks *automatic* run resume after a sensor pause, but an
explicit RESUME remains a reachable reauthorization route. A later PWM SSR
error may latch F-017, but does not make the earlier direct/contactor error
response authoritative.

## Root cause and violated invariant

Output-off write errors are reported locally, not converted into persistent
authorization/fault state across coordinator recovery. Coordinator pause/STOP
discard the failure as an operation result. The invariant is: failure to
confirm a required software off request cannot be silently cleared by normal
sensor recovery or operator RESUME; error status must be visible and positive
output admission must remain blocked until an explicit, validated fault rule
allows it.

## Minimal root-fix boundary and safety impact

Define heater-owned latching/retry behavior for both SSR and contactor off
errors, propagate a structured off-response to coordinator fault/command
outcome, and ensure sensor recovery clears only sensor validity—not an output
failure. Trace queued CLEAR, STOP, startup, reset and PWM through the same
fault owner. Keep direct off independent of dispatcher/event queue capacity
(F-075) and order against admitted high writes (F-074). The operating oven
has no independent hardware cutout; GPIO return values cannot detect welded
contacts or prove electrical off. Production hardware safety mechanisms and
feedback remain separately required.

## Allowed scope, ADR, and correction authority

Source analysis/documentation only. A fault-state/recovery behavior change
requires explicit patch authority, safety/concurrency/control impact review,
selected fault taxonomy and test oracle. An ADR may be needed if the fault
owner or external command/status contract changes; none is selected here.

## Regression test and hardware validation

`SCN-W3-001`: mock persistent contactor-low and SSR-low errors separately during direct
sensor/control inhibit, then deliver valid acquisitions and explicit RESUME/
START. Failure before correction: pause/STOP can return success and clear
normal reasons while no permanent output fault is latched. Passing correction:
original GPIO error is retained/reported, a persistent nonrecoverable output
reason blocks every positive command, and recovery does not clear it. Use
mocked GPIO/event/queue and fixed command order; no test implemented/run.
Actual contactor/SSR polarity, welded-output feedback, and response latency
require a separately authorized powered-controller plan. No hardware action
is authorized.

## Dependencies and resume conditions

Select the fault/recovery policy, test seams, output owner and deadline with
F-074/F-075, then authorize a bounded fix. Physical safety validation and
independent production hardware design remain release/commissioning gates,
not claims from this report.
