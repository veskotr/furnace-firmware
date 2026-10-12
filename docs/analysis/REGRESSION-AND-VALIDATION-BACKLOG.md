# Regression and validation backlog

Status: planned; no tests, firmware flashing, or powered validation executed on this branch.

## Purpose and boundary

This backlog turns the audit findings into deterministic acceptance work. It does
not authorize a correction or introduce a test framework. The repository has no
host or ESP-IDF unit-test harness (F-039), so the commands below are intentionally
not presented as runnable. Test infrastructure and an output-safe test build are
separately tracked by WI-001 and WI-002 in [FUTURE_WORK_ITEMS.md](FUTURE_WORK_ITEMS.md).

Use the lowest layer that reproduces the mechanism. A mock or target result proves
only the stated software contract; it never proves GPIO polarity, electrical
isolation, or physical time-to-off.

## Prioritized deterministic regression cases

| Priority | IDs | Test level and required seam | Failure-before / pass-after oracle | Flake controls |
|---|---|---|---|---|
| 1 | F-070 / SCN-W1-001 | Host profile phase vector plus mocked coordinator/dispatcher/heater/GPIO order; live manual decrement with positive demand and ordinary cooling with full queue | Before: manual cooling emits no off request and retains positive PWM target; ordinary cooling off waits in queue. After: every no-heat entry revokes demand and requests low before stale high writes; no queued positive command reauthorizes cooling. | Fixed current/target and barriers for command admission/queue congestion; no heating hardware or scheduler sleeps. |
| 1 | F-069 / SCN-W1-002 | Mocked validity/expiry callback, coordinator pause reasons, heater inhibits and fake monotonic time | Before: three accepted events clear sensor gate but sensor-paused run stays paused. After: the selected *qualified-acquisition* rule auto-resumes only a sensor-owned pause, never operator/fault/permanent/STOP. | Fixed event/tick trace and explicit pause-cause assertions; current count of three is a test vector, not an accepted future policy. |
| 1 | F-071 / SCN-W1-004 | Mocked Modbus failure, cache tuple, processor aggregate, coordinator lease with fixed ticks | Before: failed scans renew aggregate tick and recovery credit despite no new physical read. After: lease age uses acquisition time and recovery credit follows the selected distinct-acquisition rule. | One named successful read followed by controlled failed scans; count identities, not wall-clock events. |
| 1 | F-072 / SCN-D-001 | Mocked coordinator worker/profile lifetime with a barrier after profile singleton guard | Before: STOP frees/reset profile before worker exit acknowledgement and a preempted tick resumes into null/reset state. After: direct off precedes bounded join; profile/PID state is destroyed only after worker exit, with no late positive command. | Precise worker barrier and call-order/exit counters; combine queue-full STOP variant with F-061, never scheduler sleeps. |
| 1 | F-074 / SCN-W1-003 | Mocked heater PWM/command/inhibit/GPIO interleaving; barrier both positive writers after inhibit check | Before: SSR/contactor-high writes follow completed direct low requests despite active inhibit. After: stale admission is rejected or generation-checked and low is final; include sensor expiry, pause and STOP. | Explicit check/off/high barriers and pin call-order assertions; no scheduler sleeps or volatile timing guesses. |
| 1 | F-075 / SCN-W1-005 | Mocked private event queue full/stalled consumer and GPIO call sequence | Before: PWM blocks after SSR-high, or inhibit blocks after SSR-low before contactor-low. After: both required low requests complete within selected software bound without telemetry queue admission. | Fixed queue occupancy and consumer barrier; count diagnostics drops/errors separately, not wall-clock flake hunts. |
| 1 | F-077 / SCN-W3-001 | Mocked GPIO failures at direct sensor/control inhibit, coordinator pause/STOP, qualified recovery and explicit RESUME | Before: persistent contactor-low errors can be logged while pause reports success; normal reasons later clear without permanent output fault. After: original GPIO error and incomplete-off status persist, positive commands are denied until selected serviced recovery. | Inject both pause-path contactor-low attempts, or one STOP-path error; make event queue nonblocking to isolate this cause. |
| 1 | F-076 / SCN-W1-006 | Native host `profile_tick` and run-validator vectors | Before: valid 150→100 °C cooling step trips +30 °C generic guard. After: normal downward step continues, while separately specified genuine high-temperature case still inhibits/reports. | Exact temperatures/phases/fake elapsed time; pure math, no thermal simulator or wall-clock wait. |
| 2 | F-078 / SCN-W3-002 | Mock heater permanent SSR fault, coordinator PAUSE/RESUME command and event/status capture | Before: RESUME succeeds and emits RESUMED while heater stays permanently inhibited. After: truthful fault rejection/status and no positive command admission. | Named fault injection and event call-order trace; distinguish queue admission from command application. |
| 2 | F-073 / SCN-F-001 | Static/compiled frame-versus-task-stack budget plus mocked factory-reset registry/storage | Before: 12,896-byte delete-all frame enters on 8,192-byte HMI worker stack. After: bounded frame and defined partial-delete outcome for empty/full registry. | Compile-size and fixed config assertion; no factory reset on a real oven or timing-based overflow hunt. |
| 1 | F-057 | Mocked heater-component lifecycle; fail each context, mutex, semaphore, handler-registration, GPIO-setup, and worker-start acquisition | Before: a failed attempt retains an acquired resource or handler. After: every acquired item is released in reverse order, configured outputs receive an off request, and one retry has the same resource state as a first start. | Inject one named boundary at a time; use call counters and completion acknowledgements, never delays. |
| 1 | F-059 | Mocked coordinator/event/dispatcher lifecycle; fail the subscription after handler registration | Before: timer/context/handler survives and a later dispatcher command can address stale state. After: all acquired objects are released and a post-teardown command is rejected or has no coordinator target. | Gate handler invocation with a test-controlled barrier; assert unregister before context free. |
| 1 | F-060 | Mocked device-cache/processor boundary with an update barrier between sample-field writes | Before: the processor can accept a mixed temperature/tick/success tuple. After: aggregation observes exactly the complete old or complete new tuple; invalid or stale input still asserts sensor-data inhibit. | Deterministic producer/consumer barriers and fixed ticks; do not use scheduler sleeps to seek the interleaving. |
| 1 | F-061 | Mock coordinator worker/timer and bounded dispatcher queue | Stop while the worker is blocked, refill the queue after STOP is dequeued, then release the worker. After correction no `SET_POWER` is submitted after STOP begins and the join completes boundedly after the direct off request. | Notification and queue-refill barriers; capture call order without wall-clock scheduling. |
| 1 | F-062 | Host or mocked FreeRTOS dispatcher lifecycle with a producer admission barrier | Pause an external producer after its running check, complete shutdown, then release it. After correction it returns the defined rejection/result without touching freed queue/context and clean reinit succeeds. | Admission barrier plus heap poison/ASan where the host runner supports it. |
| 1 | F-063 | Mock device manager and Modbus update with a read-completion barrier | Block a manager update after it accepts a sensor, concurrently stop/reinit processor, then release the read. After correction no old transaction writes a destroyed/reused generation and table/pool counts stay coherent. | Generation/counter assertions and explicit worker acknowledgements; no timing delays. |
| 2 | F-058 / F-056 | Mocked temperature-processor/device-manager lifecycle with pool and registration accounting | Before: duplicate init increases registrations/pool use and shutdown leaves the original set. After: duplicate init does not mutate devices, task, subscription, or sensor array; stop/reinit returns all counters to baseline. | Snapshot counts before each operation; worker exit acknowledgement precedes destruction. |
| 2 | F-001 / F-054 | Mocked coordinator time and heater interface | Establish valid aggregates, advance past expiry without a replacement, and prove sensor inhibit and pause occur before any later positive heater command; verify configured recovery count. | Inject a monotonic clock/timer callback; no wall-clock tolerance. |
| 2 | F-055 | Mocked bounded dispatcher queue and heater interface | Fill the queue and issue stop: direct SSR/contactor-off requests occur before bounded dispatch wait and shutdown completes without self-queue deadlock. | Deterministic full queue and trace ordering, with bounded completion timeout. |
| 3 | F-050 | NVS/persistence fault-injection test | Each storage read/write/commit failure returns a defined error and leaves the in-memory registry/state coherent. | Per-call injected errors, clean NVS fixture, and a restart/readback case. |
| 3 | F-017 / F-053 | Mocked GPIO and control-input boundary | Inject SSR-low failure or NaN/Inf at each control input; prove permanent inhibit/demand zero/direct contactor-off and finite zero output as applicable. | Capture ordered calls and reject subsequent heater-on commands in the same fixture. |
| 3 | F-048 / F-052 | Host unit tests for profile mathematics and PID pure logic once extracted without behavior change | Check fixed/eased trajectory points, handover tolerances, stage timing, clamps, reset, and non-finite boundaries. | Exact test vectors and fake monotonic time; no hardware or FreeRTOS scheduler dependency. |

## Test-framework acceptance before implementing cases

1. Choose and document a host/ESP-IDF runner, source layout, command, and CI policy under WI-001.
2. Add narrowly scoped test seams for time, GPIO, event/dispatcher registration, task completion, and device-cache transfer. Seams must preserve production behavior and cannot bypass output safety.
3. Provide a clearly identified output-safe test mode under WI-002; production configuration must not be able to select it accidentally.
4. Every case must state its finding ID, exact source mechanism, test classification, command/result, coverage boundary, and remaining hardware work in its change-validation report.

## Hardware-validation routing

No hardware action is authorized by this document. The current concrete
powered-controller plan is [F001-F054-HARDWARE-VALIDATION.md](F001-F054-HARDWARE-VALIDATION.md):
it covers stale/silent temperature input and output de-energization. The focused
SSR-failure plan is [F17-HARDWARE-VALIDATION.md](F17-HARDWARE-VALIDATION.md).

After corrections to F-057 through F-060, retain those plans as release gates:

- F-057 needs a powered-controller observation of output state through partial GPIO/task setup failure, with independent cut-off, schematic-confirmed polarity, qualified supervision, and explicit authorization.
- F-058/F-060 need software lifecycle/coherency proof first; the existing temperature-loss bench plan remains necessary for any claim about the corrected sensing-to-output path.
- F-059 needs no powered test to prove handler lifetime, but any release containing it retains F-001/F-054/F-017/F-018 powered-controller requirements.

Any new bench procedure must separately document prerequisites, safe initial
state, instrumentation, independent cut-off, abort/recovery, measurements, and
retained evidence. It must stop before execution if those controls or explicit
authority are absent.

## Current evidence

- Repository setup verification passes: `python3 tools/codex/verify_repository_setup.py`.
- The configured ESP-IDF build passed on this branch with `IDF_PATH` set to the
  local ESP-IDF installation. This is a compilation result only, not a regression
  or hardware result.
- No deterministic cases in this backlog have been implemented or run.
