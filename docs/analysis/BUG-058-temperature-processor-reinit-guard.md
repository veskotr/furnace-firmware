# BUG-058: Temperature processor allows duplicate initialization

- **Subsystem:** temperature acquisition/device lifecycle
- **Severity:** high (lifecycle and control-input availability)
- **Confidence:** confirmed
- **Status:** confirmed; correction not authorized
- **Affected files and symbols:** `temperature_processor_core.c:init_temp_processor`, `temperature_processor_core.c:destroy_devices`, `temperature_processor_task.c:start_temp_processor_task`, `temp_sensor_device_core.c:temp_sensor_create`
- **Prerequisite or linked IDs:** F-006, F-010, F-029, F-056
- **Ready to fix:** no

## Observed behavior

`init_temp_processor` is intended to be idempotent while the processor is active, but its guard reads a file-static `processor_running` variable that is never written. A second call proceeds through device creation despite the existing worker and overwrites the context’s sensor pointers with newly allocated static-pool slots. The original devices remain registered and active but are no longer owned by the processor context.

## Expected behavior

Only one temperature processor may own the sensor array, task, subscription, and static sensor-device allocations. A duplicate active initialization must return without mutation; a failed/retryable initialization must retain or unwind one coherent ownership set.

## Reproduction

1. Initialize the device manager and call `init_temp_processor(5)` successfully.
2. Call `init_temp_processor(5)` again without shutdown.
3. The `processor_running` guard is still false, so the function reuses the live context, runs `init_devices`, and assigns five newly created sensor pointers over the original five array entries.
4. `start_temp_processor_task` observes an existing task handle and returns success; no second processor worker is created.
5. On later shutdown, `destroy_devices` sees only the second set. The first registered devices/static-pool allocations remain orphaned; repeated calls exhaust capacity or cause later initialization failure.

No hardware was operated for this investigation.

## Evidence

- `temperature_processor_core.c` defines `volatile bool processor_running = false`; repository-wide source search finds only its definition and the `if (processor_running)` read in `init_temp_processor`.
- The actual worker lifecycle uses `g_temp_processor_ctx->processor_running`, an `atomic_bool`, set true in `init_temp_processor` and cleared in `stop_temp_processor_task`/normal shutdown.
- `init_devices` calls `temp_sensor_create(&g_temp_processor_ctx->temp_sensor_devices[i])` for every index without rejecting an existing pointer.
- `temp_sensor_create` reserves the next unused static-pool entry and registers it with device manager. `destroy_devices` can subsequently destroy only the pointers currently retained in the context array.

## Root-cause hypothesis

The component retained an obsolete global lifecycle flag while the worker moved to context-owned atomic state. The two ownership models diverged, leaving the public initialization guard disconnected from the actual worker state.

## Violated invariant

Every device, task, and mutable state has one owner; initialization/reinitialization must not create overlapping ownership or leave live producers with orphaned state. Temperature is usable for control only through an explicit, live validity/freshness pipeline.

## Minimal root-fix boundary

Limit a correction to temperature-processor lifecycle ownership: use the context-owned atomic/handle state as the single initialization guard; reject or safely clean existing sensor-array entries before creation; and ensure all failure/retry paths join the worker and release every owned device before freeing context. Do not change quorum, PID, Modbus register behavior, or heater policy in the same change.

## Concurrency or timing relevance

The second initialization mutates the live processor context while its existing worker can concurrently read the sensor array. The defect therefore includes both orphaned-device lifecycle failure and an uncharacterized cross-task mutation window.

## Safety impact

The direct heater sensor-data inhibit remains fail-closed when no valid aggregate arrives, so this report does not claim immediate unintended heating. However, orphaned or overlapping sensor/device state undermines the temperature-validity pipeline and can leave a running system without a coherent sensing owner. This is release-relevant control-input lifecycle debt.

## Proposed minimal fix

Unify the lifecycle guard on context-owned atomic state and make duplicate init a no-op/defined error before any array or device mutation. Add reverse-order cleanup for the remaining initialization failure paths.

## Allowed scope and correction authority

Documentation and investigation are authorized on `audit-docs-verification`. No production-code correction has been authorized.

## ADR required, link, and status

No ADR is required for a narrow single-owner lifecycle correction. An ADR is required only if the device-manager mutation API needs a new cross-task synchronization contract.

## Regression test

Initialize once, snapshot device-manager count and sensor-pool allocation, invoke duplicate init, and assert no new device/task/subscription allocation or sensor-array mutation. Then shutdown and assert count/pool return to baseline. The failure-before oracle is increased allocation/count and orphaned entries after the duplicate call; no harness exists yet.

## Hardware validation

No powered test is needed to establish the software lifecycle oracle. Any corrected release still needs the existing temperature-loss/powered-controller validation for F-001/F-054; hardware authorization has not been granted.

## Dependencies

F-006, F-010, F-029, F-056, and the device-manager mutation-synchronization uncertainty.

## Remaining uncertainty

The event-subscription helper separately ignores the return value from `event_manager_subscribe`; this makes registration failure invisible and is recorded as a mapping/test gap, but is not the trigger for this finding. The exact Modbus/device behavior of orphaned entries requires a mocked lifecycle test or bench evidence.

## Resume conditions

Authorize the narrow lifecycle correction and deterministic test. Before release, show duplicate-init and stop/reinit coverage, then retain F-001/F-054 hardware validation as a separate requirement.
