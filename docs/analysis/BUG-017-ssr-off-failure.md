# BUG-017: SSR GPIO failure does not latch output inhibit or drop contactor

- **Subsystem:** heater controller / physical output safety
- **Severity:** critical
- **Confidence:** confirmed
- **Status:** ready-to-fix
- **Affected files and symbols:** `components/heater_controller_component/src/heater_controller_task.c:check_error_and_post_event`, `heater_controller_task`, `heater_controller_events.c:heater_command_handler`, `heater_controller.c:toggle_heater/stop_heater`, `heater_controller_internal.h`
- **Prerequisite or linked IDs:** F-002, F-003, F-016, F-017, F-053
- **Ready to fix:** yes, for the defined heater-component boundary; powered validation remains a release requirement.

## Observed behavior

The PWM task calls `toggle_heater(HEATER_OFF)` at the end of each SSR window. If that GPIO write returns an error, `check_error_and_post_event()` only logs and posts `FURNACE_ERROR_EVENT`; it does not clear demand, latch a fault, or call `stop_heater()` on the contactor GPIO. The task then continues its normal loop.

## Expected behavior

Any failed attempt to de-energize the SSR must immediately prevent further software authorization of heat, attempt the independent contactor-off path without using the command dispatcher, and report whether either physical-off attempt failed. Recovery must require explicit, reviewed action rather than a later queued power command.

## Reproduction

No hardware was operated. A source-level fault injection seam can make `gpio_master_set_level(CONFIG_HEATER_SSR_GPIO_PIN, 0)` return an error while the PWM task has positive target power. Current code takes the error branch in `check_error_and_post_event()` and returns to the loop without calling `stop_heater()`.

## Evidence

- `heater_controller_task.c:47-55` invokes `toggle_heater()` for SSR ON and OFF, then calls `check_error_and_post_event(err)`.
- `heater_controller_task.c:148-159` only logs and posts a furnace-error event on failure.
- `heater_controller_events.c:post_heater_controller_error` posts telemetry with `portMAX_DELAY`; no production `FURNACE_ERROR_EVENT` subscriber provides output mitigation.
- `heater_controller.c:66-73` contains the independent contactor-off implementation, `stop_heater()`, but the PWM error path does not invoke it.

## Root-cause hypothesis

The heater component treats GPIO failure as an observability event rather than an actuator-safety state transition. It has no fault latch at the final output boundary, so a failed SSR-off request leaves both the physical state and future output authorization unspecified.

## Violated invariant

If the firmware cannot confirm an SSR-off GPIO operation, it must immediately deny further heater demand and attempt independent contactor isolation before any logging, event delivery, queue operation, or HMI update.

## Minimal root-fix boundary

Limit the change to `heater_controller_component` and its public/internal output contract. Do not alter PID tuning, coordinator/profile behavior, command-dispatcher architecture, pin mapping, or HMI behavior in this fix.

## Concurrency or timing relevance

The PWM task can have read a positive demand before a fault. The latch must be checked at the output boundary immediately before SSR-on and must prevent later `SET_POWER`/`START` commands from reauthorizing output. The off and contactor-off attempts must execute directly in the heater component, not through the bounded dispatcher queue. GPIO-mutex failure/blocking and actual pin polarity remain hardware validation concerns.

## Safety impact

Without a latch and direct contactor-off attempt, a failed SSR-low operation may leave heating energized until another event changes it. The physical outcome depends on GPIO/SSR/contactor hardware, so software source review proves the missing action but not whether deployed hardware stays on.

## Proposed minimal fix

1. Add a heater-component-owned `output_inhibited`/fault-latched state protected by the existing power-state synchronization mechanism.
2. On any failed SSR GPIO operation, set the latch and target demand to zero first; then directly attempt SSR-low and `stop_heater()` in the heater task/component, without dispatcher or event-loop dependence.
3. Make the PWM task check the latch immediately before SSR-on and skip all further ON transitions while latched.
4. Reject or ignore heater `SET_POWER` and `START` commands while latched; provide no automatic clear path in this change.
5. Only after direct physical-off attempts, emit detailed error telemetry; if contactor-off also fails, preserve that distinct failure in logs/event data.
6. Define a separate, explicitly authorized fault-acknowledgement/recovery workflow before allowing a later change to clear the latch.

This does not solve F-002/F-003 globally, but it establishes a local actuator-boundary fail-off response for the SSR-write failure that triggers this bug.

## Allowed scope and correction authority

Authorized next step: a narrow production fix on `hardening/field-fixes` implementing the six steps above, plus focused tests and documentation updates. Any cross-component direct-inhibit API, fault acknowledgement UI, GPIO remapping, or changes to fault policy require a separate ADR/authority decision.

## ADR required, link, and status

No ADR is required for the local latch/direct-contact-off correction. An ADR is required before generalizing this into a system-wide independent inhibit and recovery model for F-001/F-002/F-003/F-004/F-017.

## Regression test

Host/mocked component test with injectable GPIO calls:

1. Set positive demand and make SSR-low fail.
2. Assert latch set, target demand zero, direct contactor-off attempted, and no queue/event result is needed before those calls.
3. Run a subsequent PWM iteration and assert SSR-on is not attempted.
4. Send `SET_POWER` and `START`; assert they cannot reauthorize output while latched.
5. Make contactor-off fail and assert the failure is separately reported.

### Failure-before oracle and result

Before the correction, the same injected SSR-low failure reaches only `post_heater_controller_error`; no `stop_heater()` call occurs and a subsequent loop can continue output handling. No existing automated test executes this oracle.

## Hardware validation

Validate on an approved powered-controller bench before release: force or simulate SSR GPIO failure; measure SSR and contactor outputs, verify contactor-off latency and polarity, confirm no re-energization while latched, and verify restart/brownout behavior. Do not test on a powered furnace until the controller-bench result and external interlocks are approved.

### Permission and required test level

No hardware authorization has been granted. Required sequence: host/mocked regression test, target test if injection is available, powered-controller bench, then separately authorized powered-furnace validation.

## Dependencies

The test needs a GPIO abstraction/fault injection seam. If adding one would expand beyond the local component, retain a small test-only wrapper rather than redesigning the dispatcher or PID path.

## Remaining uncertainty

Contactor/SSR active polarity, external wiring/interlocks, GPIO driver failure semantics, and whether contactor-off is electrically independent from the SSR are not established from source.

## Resume conditions

Implement only after agreement that a latched, manual-recovery output inhibit is the intended behavior. If automatic recovery is desired, stop and record its safety contract in an ADR first.
