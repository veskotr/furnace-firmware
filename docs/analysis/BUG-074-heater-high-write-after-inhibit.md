# BUG-074: Admitted heater-on write can follow a completed inhibit

- **Subsystem:** heater authorization, PWM/command writers, GPIO driver
- **Severity:** critical conditional output-safety race
- **Confidence:** confirmed source-level interleaving; electrical result unmeasured
- **Status:** confirmed; correction not authorized
- **Affected files and symbols:** `heater_output_is_inhibited`, `heater_controller_task`, `heater_command_handler`, sensor/control inhibit setters, `gpio_master_set_level`
- **Prerequisite or linked IDs:** F-003 narrow pause gate; F-017 SSR fault latch; F-070 cooling gap; F-075 telemetry backpressure
- **Ready to fix:** no; one output owner/atomic authorization-write contract and test seam need review

## Observed behavior and reachable interleaving

PWM computes a positive on-time and checks the combined inhibit while holding
`power_mutex`; the check releases that mutex before `toggle_heater(ON)` writes
SSR high. HEATER_START and positive HEATER_TOGGLE likewise check then write
contactor/SSR high after releasing the same mutex. A sensor or control inhibit
can set its flag and finish both direct low requests in those gaps. Resume the
admitted writers afterward: SSR-high and contactor-high software requests can
both follow the completed inhibit while its flags remain active. This is an
exact source-level multi-context order, not observed oven energization.

## Evidence, ownership, and disconfirming guards

[PWM worker](../../components/heater_controller_component/src/heater_controller_task.c)
performs `get_heater_target_power_level` and `heater_output_is_inhibited`
separately before `toggle_heater(HEATER_ON)`.
[Command handler](../../components/heater_controller_component/src/heater_controller_events.c)
performs the same check before positive TOGGLE or START.
[Inhibit setters](../../components/heater_controller_component/src/heater_controller_task.c)
set flag/zero target under `power_mutex`, release it, then call SSR-low and
contactor-low directly. [GPIO driver](../../components/gpio_master_driver/src/gpio_master_driver.c)
serializes each pin write but does not hold authorization across check+write.
An on writer that checks *after* inhibit is rejected; this does not protect
one admitted before the flag change. The 5,000 ms PWM window and task
notifications are not synchronization or a final-off guarantee.

## Root cause and violated invariant

Authorization and high GPIO write are separate operations owned by different
execution contexts without a common lifetime/generation protocol. The output
invariant is: once an inhibit completes its off response, no pre-inhibit
writer can request either output high until an authorized new generation
begins. F-003's historical source fix protects new checks under an active
inhibit; it does not close this final-write ordering race.

## Minimal root-fix boundary and safety impact

Choose one heater-output owner or equivalent atomic admission/generation and
write protocol that orders both SSR and contactor high requests against all
direct inhibits. Keep fault/pause/STOP off independent of bounded dispatcher
and event queues. Simply extending `power_mutex` around a GPIO call is not
yet a safe design because `toggle_heater` currently posts blocking telemetry
(F-075); the output I/O and diagnostics work must be separated/characterized.
The operating oven has no independent hardware cutout. Source high requests
after inhibit are serious, but actual SSR polarity, contactor state, heater
power, and off latency remain unverified.

## Allowed scope, ADR, and correction authority

Analysis/documentation only. A bounded correction requires explicit patch
authority, impact/safety/concurrency review, test seam and call-order oracle.
A lasting output-owner/task command contract may require an ADR; none is
selected by this report.

## Regression test and hardware validation

`SCN-W1-003`: barrier a positive PWM and START after their inhibit checks;
complete direct sensor/STOP off writes; release both high writers. Failure
before correction: high requests follow off while flags remain inhibited.
Passing correction: low remains the final software request and stale writers
return rejection without high, including a full dispatcher/event queue.
Mocked GPIO/task barriers, no scheduler sleeps; no test implemented/run.
Powered-controller feedback/polarity/off timing needs an approved plan and
explicit authority; no hardware operation is authorized here.

## Resume conditions

Reconcile F-070 no-heat phase output policy and F-075 diagnostic blocking,
select output ownership/admission semantics, add a deterministic seam, and
authorize a focused fix. Release safety remains gated by physical validation.
