# BUG-056: Temperature-processor shutdown leaves its sensor devices registered

- **Subsystem:** Temperature processor, temperature-sensor device, device manager
- **Severity:** medium
- **Confidence:** confirmed
- **Status:** source fix implemented; regression validation pending
- **Affected files and symbols:** `components/temperature_processor_component/src/temperature_processor_core.c:shutdown_temp_processor/destroy_devices`; `components/temp_sensor_device/src/temp_sensor_device_core.c:temp_sensor_destroy`; `components/device_manager/src/device_manager_core.c:device_manager_destroy`
- **Prerequisite or linked IDs:** F-006, F-012, F-029, F-038, F-056
- **Ready to fix:** implemented; regression pending

## Observed behavior

Normal temperature-processor shutdown joins the worker and frees its context but never destroys the sensor devices it created. The device-destruction helper exists but is called only for initialization and task-start failures.

## Expected behavior

After a successful processor shutdown, every processor-owned device-manager entry and sensor-pool allocation must be released before reinitialization.

## Reproduction

1. Initialize the temperature processor successfully.
2. Shut it down normally.
3. Initialize it again with the same number of sensors.

The prior sensor devices remain registered/running and their static pool slots remain allocated; reinitialization can exhaust the pool or device table.

## Evidence

- `shutdown_temp_processor` frees `g_temp_processor_ctx` without calling `destroy_devices`.
- `destroy_devices` iterates the processor's device pointers and calls `temp_sensor_destroy`.
- `temp_sensor_destroy` is the path that releases the device-manager entry and clears the sensor-pool allocation.

## Root-cause hypothesis

The worker-lifetime correction added an acknowledgement but did not extend normal teardown to all resources owned by the processor context.

## Violated invariant

An owner must stop producers/workers, receive acknowledgement, then release every resource it created before freeing the ownership context.

## Minimal root-fix boundary

Temperature-processor normal shutdown and its owned sensor-device list. Device-manager concurrency/lifecycle must be respected; no Modbus protocol or control-policy change is required.

## Concurrency or timing relevance

Destroy only after the temperature worker exits and after event unsubscription; establish whether the device-manager worker must be stopped or otherwise excluded before mutating its device table.

## Safety impact

This is not a direct heater-on path. Orphaned sensor updates and failed reinitialization can, however, undermine the temperature-data lifecycle assumed by F-001.

## Implemented minimal fix

`shutdown_temp_processor` now calls the existing owned-device destruction path after the processor worker acknowledgement and before freeing the processor context. The helper destroys each registered sensor through `temp_sensor_destroy`, which releases the device-manager entry and sensor-pool allocation.

## Applied scope and correction authority

The narrow owned-device teardown correction was implemented in the current hardening session. Stop/reinitialize coverage and device-manager mutation synchronization characterization remain required before release.

## ADR required, link, and status

No ADR is required for restoration of local resource ownership. An ADR is required only if device-manager ownership/concurrency changes.

## Regression test

### Failure-before oracle and result

Initialize → shutdown → initialize must fail before correction when the static pool/table remains occupied. After correction, verify device-manager count and all pool slots return to baseline, then repeat initialization successfully. The current repository has no executable regression harness, so this oracle remains pending.

## Hardware validation

### Permission and required test level

No powered hardware validation is required to prove resource cleanup. Device-bench validation remains required for later sensor communication claims.

## Dependencies

F-006, F-012, F-029, F-038, F-041, and test infrastructure WI-001.

## Remaining uncertainty

The current device-manager table has no documented mutation lock; the eventual fix must establish safe ordering with its periodic update worker.

## Resume conditions

Resume with explicit correction authority, a repeatable stop/reinit test, and device-manager lifecycle review.
