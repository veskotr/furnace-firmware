# BUG-075: Blocking heater telemetry can delay physical-off requests

- **Subsystem:** heater I/O, private ESP event queue, SSR/contactor inhibit
- **Severity:** critical conditional off-latency gap
- **Confidence:** confirmed source-level full-queue trigger; physical latency unmeasured
- **Status:** confirmed; correction not authorized
- **Affected files and symbols:** `toggle_heater`, `post_heater_controller_event`, `event_manager_post_blocking`, heater inhibit setters and PWM task
- **Prerequisite or linked IDs:** F-003/F-017 direct inhibits; F-074 high-write ordering; F-002 bounded dispatcher queue
- **Ready to fix:** no; output-versus-diagnostics contract and deadline must be chosen

## Observed behavior and reachable trigger

`toggle_heater` writes SSR GPIO, then posts `HEATER_TOGGLED` to the private
32-entry ESP event queue with `portMAX_DELAY` before returning. A non-event-
loop PWM caller can therefore wait unboundedly for a full/stalled event queue
after requesting SSR-high and before its planned SSR-low window. A direct
sensor/control inhibit calls SSR-low through the same function, then only
after it returns calls contactor-low. With the event queue full and its sole
consumer stalled, an inhibit can wait after SSR-low and before contactor-low.
This is source-level unbounded ordering, not measured heater power or latency.

## Evidence, ownership, and disconfirming guard

[Heater I/O](../../components/heater_controller_component/src/heater_controller.c)
performs GPIO write before calling `post_heater_controller_event`.
[Event publisher](../../components/heater_controller_component/src/heater_controller_events.c)
uses `portMAX_DELAY`; [event wrapper](../../components/event_manager/src/event_manager.c)
forwards it to `esp_event_post_to`. [Inhibit setters](../../components/heater_controller_component/src/heater_controller_task.c)
request SSR-low then contactor-low sequentially. The local ESP-IDF 5.5.4
event-loop implementation switches a post by its **own** loop task to zero
wait; therefore this report does not claim same-loop self-post deadlock. PWM
and timer/control callers are other tasks and retain the blocking exposure.
Telemetry post errors are ignored by `toggle_heater` once GPIO succeeds.

## Root cause and violated invariant

Output I/O completion is coupled to an unbounded diagnostics submission on a
queue unrelated to final physical mitigation. The invariant is: required
SSR/contactor-off requests must not wait for telemetry capacity, HMI, logger,
or another sole consumer, and their software/physical deadlines must be
specified separately.

## Minimal root-fix boundary, timing, and safety impact

Separate output-write response from bounded/best-effort telemetry publication
and define explicit overload/error reporting. Preserve traceable fault logs
without letting them block off, and review all callers (PWM, startup,
shutdown, direct inhibits). F-074 needs the same separation before any
check+write synchronization can be claimed safe. No source-level maximum
contactor-off latency exists here. The operating oven has no independent
hardware cutout; electrical result and actual delay remain unknown.

## Allowed scope, ADR, and correction authority

Documentation/diagnosis only. A behavior-changing output/telemetry split
requires explicit authorization, impact/safety/concurrency review, a mock
queue-saturation oracle, and chosen off deadline. No lasting ADR is selected;
escalate if the event/status contract changes externally.

## Regression test and hardware validation

`SCN-W1-005`: fill and hold the private event queue, then request PWM-high
and direct inhibit. Failure before correction: PWM or inhibit blocks in
telemetry; contactor-low is not requested until the queue drains. Passing
correction: low requests complete independently and boundedly, with defined
telemetry loss/error accounting. Use mocked event queue and GPIO call-order
barriers, not scheduler sleeps. No test implemented/run. A physical deadline
requires separately authorized powered-controller feedback/polarity/off
measurement; no hardware operation is authorized.

## Resume conditions

Specify software and physical off deadlines and output/diagnostic ownership,
then authorize a bounded correction with regression coverage. Retain powered
validation as a release gate.
