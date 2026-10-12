# BUG-072: STOP destroys profile state before control worker exits

- **Subsystem:** coordinator/profile worker lifecycle
- **Severity:** high conditional crash and incomplete STOP
- **Confidence:** confirmed source-level interleaving; no runtime crash claimed
- **Status:** confirmed; correction not authorized
- **Affected files and symbols:** `stop_heating_profile`, `profile_tick`, `shutdown_profile_controller`
- **Prerequisite or linked IDs:** F-061 STOP wake/final command; F-055 queue-independent stop; F-003 direct inhibit
- **Ready to fix:** no; worker exit/ownership protocol and deterministic seam need impact review

## Observed behavior and reachable trigger

A dispatcher-handled STOP can free the singleton profile controller before
the control worker has acknowledged exit. If the worker is preempted inside
`profile_tick` after its non-null check but before it reloads the singleton's
program pointer, it can resume into a null/freed singleton dereference. A
worker already past that pointer can race the reset of file-static tick state.
This is a source-level interleaving, not an observed field crash.

## Evidence and ownership

The [control worker](../../components/coordinator_component/src/coordinator_component_heater_controller.c)
calls `profile_tick` after a task notification while `ctx->running` is true.
`profile_tick` checks `g_temp_profile_controller_ctx` and its program, then
dereferences `g_temp_profile_controller_ctx->program` in a separate statement
([profile source](../../components/temperature_profile_controller/src/temperature_profile_core.c)).
The dispatcher STOP path stops the timer, sets `running=false`, notifies the
worker, requests direct heater control inhibit, resets PID, calls
`shutdown_profile_controller`, and **only then** waits indefinitely for the
worker's exit semaphore. Shutdown frees the singleton, sets its pointer null,
and resets shared `s_tick`. No lock or worker acknowledgement protects those
operations. A post-wake `running` check would fix F-061's final-command
variant, but does not protect a tick already entered before STOP.

## Root cause and violated invariant

Resource destruction and controller reset precede the sole consumer's exit
acknowledgement. The lifetime invariant is: STOP first revokes physical-output
authorization, prevents new work, then joins all admitted worker work before
destroying profile/PID state. `ctx->running` is an ordinary cross-task field;
its write is not a join or synchronization protocol.

## Minimal root-fix boundary, safety impact, and uncertainty

Define a single owner/exit handshake for profile and PID state, including
worker self-exit versus external STOP, and move destruction/reset after all
worker access has ended. Keep direct SSR/contactor-off independent of bounded
queues and before any wait. Current STOP requests direct off before freeing
the profile, but the off request itself has unclosed check/write and telemetry
backpressure races; physical off cannot be credited. The operating oven has
no independent hardware protection. Exact field source revision is unknown.

## Allowed scope, ADR, and correction authority

This turn authorizes documentation/diagnosis, not code correction. A lifecycle
change requires concurrency/control/safety impact review, a deterministic
failure-before interleaving, and explicit patch authority. An ADR is not yet
selected; escalate if worker/profile ownership or dispatcher command semantics
change beyond the current hardening boundary.

## Regression test and hardware validation

`SCN-D-001`: barrier the worker after `profile_tick`'s singleton check, dispatch
STOP, then release the worker. Failure-before: shutdown frees/reset state
before exit acknowledgement and worker touches null/reset state. Passing
correction: direct off request precedes bounded join, no profile/PID state is
destroyed until the worker acknowledges exit, and STOP completes without a
new positive command. Mocked task/dispatcher/profile lifetime test; no test
implemented/run. Powered controller validation of output-off timing and
reset/crash behavior remains a separate approved release gate; no hardware
action is authorized.

## Dependencies and resume conditions

Reconcile F-061 final tick and the PID reset-before-join race in one STOP
lifetime design. Check all self-exit/completion paths and queue-full STOP
cases. Resume after the ownership model and injection seam are characterized,
then authorize a bounded correction.
