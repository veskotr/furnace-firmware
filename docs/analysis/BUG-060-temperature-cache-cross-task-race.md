# BUG-060: Temperature sensor cache is read and written concurrently

- **Subsystem:** device manager / temperature acquisition
- **Severity:** high (control-input concurrency)
- **Confidence:** confirmed
- **Status:** confirmed; correction not authorized
- **Affected files and symbols:** `device_manager_task.c:device_manager_task`, `temp_sensor_device_core.c:temp_sensor_update`, `temp_sensor_device_core.c:temp_sensor_read`, `temperature_processor_task.c:read_temp_sensors`
- **Prerequisite or linked IDs:** F-001, F-021, F-056, F-058
- **Ready to fix:** no

## Observed behavior

The device-manager task writes a sensor’s cached temperature, successful-read tick, and success flag. The temperature-processor task independently reads those same ordinary fields to form the control aggregate. No mutex, atomic protocol, queue ownership transfer, or immutable snapshot separates the tasks.

## Expected behavior

Every aggregate must consume a coherent sensor snapshot with an explicit happens-before relationship to the successful physical read. A notification may wake a consumer but cannot exclude a later producer cycle.

## Reproduction

1. Device manager updates a running sensor and posts `DEVICE_MANAGER_UPDATED_EVENT`.
2. Event-loop callback/processor scheduling is delayed.
3. Device manager reaches a later update cycle and writes the same cache fields while the processor calls `temp_sensor_read_device`, `temp_sensor_has_successful_sample`, and `temp_sensor_get_last_update_tick`.
4. The tasks have equal configured priority and no shared lock. The C accesses race; the processor can combine a temperature, tick, and valid flag from different updates.

No hardware was operated for this investigation.

## Evidence

- `temp_sensor_update` assigns `last_temperature`, `last_update_tick`, and `has_successful_sample` in device-manager task context.
- `temp_sensor_read` returns `last_temperature`; the two public freshness helpers return the tick and flag directly.
- `read_temp_sensors` calls all three paths from the temperature-processor task.
- `DEVICE_MANAGER_UPDATED_EVENT` causes only `xTaskNotifyGive`; neither cache accessor takes a synchronization primitive.
- Resolved configuration assigns priority 5 to both tasks; neither component pins them to a core.

## Root-cause hypothesis

The update event was treated as though it serialized cache ownership. It reports a completed manager cycle but does not prevent future manager writes while the notified worker reads.

## Violated invariant

Temperature is usable for control only when validity and freshness are explicit, coherent, and safely transferred. Mutable state shared across tasks requires a complete synchronization protocol.

## Minimal root-fix boundary

Define one cache-transfer contract at the temperature-device/processor boundary. A narrow correction could protect a complete cache snapshot with one mutex, or have device manager publish immutable samples through a queue/event payload. It must include temperature, tick, and success status together; making one field `volatile` is not sufficient.

## Concurrency or timing relevance

This is a reachable cross-task C data race. It is independent of F-021’s coordinator float race: F-021 protects the downstream coordinator snapshot, while F-060 occurs upstream before aggregation and validity publication.

## Safety impact

A mixed/torn/stale snapshot can affect quorum and the temperature used for PID/profile decisions. Existing F-001/F-054 expiry and heater-inhibit logic mitigates loss of valid aggregates but does not make a racy accepted aggregate coherent. The exact physical effect requires deterministic concurrency testing and powered validation; no unintended heating is claimed.

## Proposed minimal fix

Provide one owner and a coherent snapshot API for cached samples. Preserve the existing freshness expiry/inhibit behavior and add a bounded producer/consumer lifetime rule for device destruction.

## Allowed scope and correction authority

Documentation and investigation are authorized on `audit-docs-verification`. No production-code correction has been authorized.

## ADR required, link, and status

An ADR is not required for a contained snapshot/mutex correction. It is required if the project chooses a broader device-manager message/ownership redesign.

## Regression test

Use a controllable mock device update that pauses between cache-field writes while the processor reads. Assert complete old or complete new samples only, never a mixed tuple; verify invalid/stale transitions still assert sensor-data inhibit. The failure-before oracle is a deliberately mixed aggregate accepted as valid.

## Hardware validation

No powered test proves this software race. Retain F-001/F-054 powered-controller requirements after a correction; no hardware authorization exists.

## Dependencies

F-001/F-054 freshness, F-021 coordinator snapshot, F-056 device destruction, and F-058 duplicate lifecycle ownership.

## Remaining uncertainty

The bundled ESP-Modbus serial master serializes requests with its own semaphore, so this finding is not a Modbus wire-request race. Device-table mutation during destruction remains a separate lifecycle boundary requiring focused review.

## Resume conditions

Authorize a bounded snapshot/ownership correction and its mocked concurrency regression. Review device destruction against the chosen protocol before implementation.
