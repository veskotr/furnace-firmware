# BUG-059: Coordinator partial initialization leaks timer/context and can retain a handler

- **Subsystem:** coordinator initialization/lifecycle
- **Severity:** high (lifecycle; conditional command-safety relevance)
- **Confidence:** confirmed
- **Status:** confirmed; correction not authorized
- **Affected files and symbols:** `coordinator_core.c:init_coordinator`, `coordinator_core.c:stop_coordinator`, `coordinator_component_events.c:init_coordinator_events`, `coordinator_component_events.c:shutdown_coordinator_events`
- **Prerequisite or linked IDs:** F-007, F-008, F-055, F-057
- **Ready to fix:** no

## Observed behavior

If coordinator event initialization fails after command-handler registration, `init_coordinator` returns directly without calling `stop_coordinator`. `events_initialized` remains false, so a later `stop_coordinator` also skips handler unregistration before freeing the context. Retrying initialization overwrites the retained mutex/semaphore fields and reuses the expiry timer/context.

## Expected behavior

Every initialized coordinator resource—sensor-data inhibit state, expiry timer, dispatcher handler, event subscription, synchronization object, and worker—must have one owner and reverse-order cleanup even when initialization fails midway. No dispatcher handler may outlive its context.

## Reproduction

1. Let mutex/semaphore setup, startup sensor-data inhibit, expiry-timer creation, and `register_command_handler(COMMAND_TARGET_COORDINATOR, ...)` succeed.
2. Fault-inject failure of the following temperature-event subscription in `init_coordinator_events`.
3. The event initializer returns an error before assigning `events_initialized=true`; `init_coordinator` propagates it without cleanup.
4. Call `stop_coordinator`, or retry initialization then stop. The event shutdown guard skips `unregister_command_handler`, but core teardown frees the context. The dispatcher may retain a handler argument pointing to freed context.

The normal `app_main` path returns before Nextion startup on an `init_coordinator` failure, so no HMI-originated command after this failure is claimed. The lifecycle leak and stale-handler ownership failure are source-proven; other post-failure producers are unresolved.

## Evidence

- `coordinator_core.c:init_coordinator` calls `init_coordinator_events` with `CHECK_ERR_LOG_RET`, not a cleanup action.
- `coordinator_component_events.c:init_coordinator_events` registers the dispatcher handler before subscribing to `TEMP_PROCESSOR_EVENT`, and sets `events_initialized` only after both calls succeed.
- `shutdown_coordinator_events` returns without unregistering when `events_initialized` is false.
- `stop_coordinator` calls the guarded event shutdown then deletes timer/mutex/semaphore objects and frees `g_coordinator_ctx`.

## Root-cause hypothesis

The boolean records completed event initialization rather than the independent ownership of handler registration and event subscription. Core initialization assumes that returning an error is enough to abandon a partial context.

## Violated invariant

Owners must stop producers/unregister callbacks, join workers, and then destroy dependencies in reverse initialization order. Partial initialization must leave outputs safely inhibited and no live callback/handler may reference freed state.

## Minimal root-fix boundary

Add explicit acquisition state for the handler/subscription/timer and make coordinator teardown safe for any allocated context. On any initialization failure, retain the direct heater sensor-data inhibit, unregister registered command handlers before freeing context, stop/delete the timer, and release only acquired mutexes/semaphores.

## Concurrency or timing relevance

The dispatcher worker exists before coordinator initialization. A retained coordinator handler can be invoked on the dispatcher context after its argument becomes stale. The direct heater sensor-data inhibit is established before this path and should remain asserted through failure cleanup, but handler lifetime remains unsafe.

## Safety impact

No hardware behavior is asserted from this finding. The source-level lifecycle defect could undermine command safety if a post-failure producer reaches the stale handler; physical output outcome depends on the invalid context contents and hardware configuration. Any correction must retain direct fail-closed inhibition and be regression-tested.

## Proposed minimal fix

Use a partial-safe coordinator teardown and independent registration/subscription flags. Do not redesign the dispatcher, event loop, profile logic, or sensor recovery policy in the same change.

## Allowed scope and correction authority

Documentation and investigation are authorized on `audit-docs-verification`. No production-code correction has been authorized.

## ADR required, link, and status

No ADR is required for the narrow lifetime correction. A broader event/dispatcher ownership redesign would require one.

## Regression test

Fault-inject every coordinator acquisition boundary, verify no timer/handler/subscription/semaphore/context remains after failure, and invoke a dispatcher command after teardown to prove it cannot target freed coordinator state. The failure-before oracle is a registered handler or allocated timer/context after failed initialization; no harness exists yet.

## Hardware validation

No powered test is needed for the source-level ownership oracle. Maintain F-001/F-054/F-017/F-018 powered-controller validation requirements after any correction.

## Dependencies

F-007/F-008/F-055 dispatcher/coordinator lifecycle, F-057 partial initialization, and the temperature-expiry direct-inhibit contract.

## Remaining uncertainty

The event-loop subscription failure must be fault-injected; the exact standard-boot command producer set after coordinator init failure is not established. These affect observed reachability of a stale handler, not the proven cleanup omission.

## Resume conditions

Authorize the narrow correction and define fault-injection seams; complete handler/timer teardown regression before a production fix is handed off.
