# BUG-057: Heater partial initialization bypasses cleanup

- **Subsystem:** heater controller initialization/lifecycle
- **Severity:** high (conditional startup-safety and lifecycle)
- **Confidence:** confirmed
- **Status:** confirmed; correction not authorized
- **Affected files and symbols:** `heater_controller_core.c:init_heater_controller_component`, `heater_controller_core.c:shutdown_heater_controller_component`, `heater_controller_events.c:init_events`, `heater_controller.c:init_heater_controller`
- **Prerequisite or linked IDs:** F-007, F-009, F-017, F-018
- **Ready to fix:** no

## Observed behavior

The heater component calls its common shutdown function for every initialization failure, but that function returns immediately until `context->initialized` is true. The flag is assigned only after command-handler registration, GPIO initialization, and heater-task creation have all succeeded. Consequently, every failure before the final assignment skips cleanup of the allocated context and any resources created earlier in the sequence.

## Expected behavior

Partial initialization must unwind completed steps in reverse order, unregister any live command handler before its context is freed, and explicitly request off for any actuator GPIO already configured. A failed attempt must leave a later retry with the same observable state and resource availability as a first attempt.

## Reproduction

Safely fault-inject any pre-completion failure after at least one successful setup step, then retry initialization:

1. `init_heater_controller_component` allocates `g_heater_controller_context` and creates the mutex/semaphore.
2. Make `init_events`, `init_heater_controller`, or `init_heater_controller_task` return an error.
3. The `CHECK_ERR_LOG_CALL_RET` failure action calls `shutdown_heater_controller_component`.
4. `shutdown_heater_controller_component` observes `initialized == false` and returns without deleting objects, unregistering the handler, de-energizing configured pins, or freeing the context.
5. A retry reuses the retained context and overwrites resource fields with newly created objects; after a successful `init_events` failure, the dispatcher also retains a handler pointing at the partially initialized context.

No hardware was operated for this investigation.

## Evidence

- `heater_controller_core.c:init_heater_controller_component` creates `power_mutex` and `exit_semaphore`, then registers the command handler, initializes GPIOs, starts the worker, and only then sets `initialized = true`.
- All three latter steps use `CHECK_ERR_LOG_CALL_RET(..., shutdown_heater_controller_component(), ...)`.
- `heater_controller_core.c:shutdown_heater_controller_component` returns when the global context is null **or** `!context->initialized`; the resource deletion and `free` statements therefore cannot execute on these failure paths.
- `heater_controller_events.c:init_events` registers `COMMAND_TARGET_HEATER`; an `unregister_command_handler` API exists, but this component has no corresponding shutdown-event cleanup.

## Root-cause hypothesis

One boolean conflates “fully initialized and operational” with “context/resources exist and require teardown.” The cleanup guard was hardened for normal task teardown but cannot represent partial ownership states.

## Violated invariant

Engineering standards require partial initialization and internal failure to leave contactor/SSR de-energized, and owners to unwind partially created resources in reverse initialization order.

## Minimal root-fix boundary

Restrict the correction to the heater component’s initialization state and teardown protocol. It must track which resources/registrations have been acquired, safely stop only a started task, unregister the heater command handler before freeing its context, explicitly request outputs off when GPIO setup completed, and avoid deinitializing the shared GPIO driver without a documented ownership contract.

## Concurrency or timing relevance

The dispatcher is active before heater initialization. If handler registration succeeds and a later setup step fails, its handler can remain registered against the partial context. The exact set of post-failure command producers is configuration/operational-context dependent, so the report does not claim an observed actuator-on event.

## Safety impact

The lifecycle defect is source-proven. Its physical impact is conditional: source does not guarantee a complete explicit off/unwind sequence on partial initialization, but GPIO polarity, reset state, and external isolation are unresolved hardware facts. This is a release blocker for any claim that partial initialization is fail-safe until deterministic failure-injection and powered-controller evidence are complete.

## Proposed minimal fix

Introduce explicit partial-state ownership (or a teardown routine that is safe whenever the context exists), with reverse-order cleanup and command-handler unregister. Do not change command, PID, profile, or HMI behavior as part of this correction.

## Allowed scope and correction authority

Documentation and investigation are authorized on `audit-docs-verification`. No production-code correction has been authorized.

## ADR required, link, and status

No ADR is required for the narrow cleanup correction. An ADR is required only if GPIO-driver ownership/deinitialization policy must change beyond this component.

## Regression test

Fault-inject each acquisition boundary (context, mutex, semaphore, handler registration, GPIO setup, worker creation), assert that no resources/handler remain after failure, retry initialization successfully, and assert explicit off requests occurred when either output pin was configured. The failure-before oracle is resource/handler retention after each injected failure; no harness exists yet.

## Hardware validation

Powered-controller validation is required after a correction to measure the actual contactor/SSR state during each partial-init failure. Authorization to flash or energize hardware has not been granted.

## Dependencies

F-007/F-009 lifecycle protocol; GPIO ownership/polarity evidence in F-017/F-018 and unresolved questions F-042/F-047.

## Remaining uncertainty

The active post-failure command producers, deployed active polarity, reset pulls, and independent external isolation are not established from source. Those gaps affect physical severity, not the confirmed cleanup failure.

## Resume conditions

Before a fix: authorize the narrow correction, establish the handler-unregister and GPIO ownership protocol, and define the fault-injection harness. Before release: complete the deterministic tests and an authorized powered-controller validation.
