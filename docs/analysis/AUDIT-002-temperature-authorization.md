# Audit 002: Temperature authorization and expiry gate

- **Date:** 2026-08-04
- **Commit:** `1fcb4b1` (`audit-docs-verification`)
- **Scope:** source trace from Modbus/device update through temperature validity, coordinator expiry, profile authorization, and heater inhibit
- **Hardware state:** not operated

## End-to-end contract

`device_manager_task` updates running sensor devices and posts `DEVICE_MANAGER_UPDATED_EVENT`. The temperature-processor event callback notifies the processor task. The processor reads cached sensor values only when each device reports a successful sample within `CONFIG_COORDINATOR_SENSOR_AGGREGATE_STALE_TIMEOUT_MS`, compacts accepted values, applies a fresh-count quorum of `sensor_count - 2` (minimum one), and publishes a validity sample containing the aggregate tick/count/valid flag.

The coordinator starts with `sensor_data_inhibited=true` and directly asks the heater to inhibit. On an accepted validity sample it records the aggregate tick and arms an `ESP_TIMER_TASK` one-shot deadline for the remaining stale-age budget. The expiry callback sets the expiry/inhibit atomics, calls the heater’s direct sensor-data inhibit, then only notifies the control task for profile bookkeeping. A profile start requires a received float event, cleared sensor-data inhibit after configured recovery aggregates, and a fresh coordinator lease.

## Ownership and execution table

| State/resource | Owner/context | Writers | Readers | Synchronization/lifetime result |
| --- | --- | --- | --- | --- |
| Per-sensor cache and successful-read tick | temp sensor/device-manager task | `temp_sensor_update` | temperature processor task | Freshness uses tick age; device-manager mutation synchronization remains uncharacterized. |
| Aggregate sample | temperature processor task | processor task | event loop callback | Event payload copy through private ESP event loop; processor blocks for the next device update after posting. |
| Current temperature and aggregate lease | coordinator | event-loop callback | coordinator control task | `temperature_mutex` protects float, last-valid tick, aggregate flag, and recovery counter. |
| Expiry/inhibit flags | coordinator | event-loop callback and ESP timer task | control task/event callback | C atomics; callback performs direct heater inhibit before task notification. |
| Sensor-data actuator gate | heater controller | coordinator event/timer contexts | heater commands/PWM task | heater power mutex; inhibit clears target and synchronously requests SSR/contactor off. |

## Safety-gate result

Static review supports the intended F-001/F-054 contract: a previously plausible float does not by itself satisfy profile start; silence is covered by the coordinator-owned expiry timer; and expiry removal of output does not wait for a dispatcher/HMI queue. The existing direct-inhibit contract is the safety boundary for any in-flight PID/queued-command race.

The evidence remains source-only. `esp_timer` scheduling latency, sensor timestamp fidelity, Modbus update behavior, GPIO polarity, SSR/contact isolation, and actual time-to-off require the existing deterministic and powered-controller validation plans. The operating oven has no independent hardware safety protection, per the user's 2026-09-15 confirmation.

Wave-1 review at the unchanged baseline narrows the contract: F-001/F-054
mitigate complete aggregate-update silence, but [F-071](BUG-071-aggregate-acquisition-freshness.md)
shows failed scans can restamp an accepted cached measurement with processing
time, extending physical acquisition age toward 10 s and allowing three
validity events from one read. [F-069](BUG-069-sensor-recovery-pause-gate.md)
shows the recovery callback releases only the sensor gate; a sensor-paused run
retains its control gate and does not auto-resume. The earlier end-to-end
paragraph describes *intended* automatic recovery, not current behavior.

Additional B candidates: `B-CAND-ZERO` is a float/metadata mismatch because
the coordinator compares a float to `-0.0f` and excludes both signed zero
values. `B-CAND-COUNT` is a conditional public-API out-of-bounds path if
`init_temp_processor` receives more than its fixed array capacity; main passes
five. The active MS9024 path assumes register 728, CDAB float order, and
Celsius; device configuration and units need transmitter documentation/bench
evidence. See [current behavior](CURRENT-CONTROLLER-BEHAVIOR.md).

## Lifecycle and mapping observations

- Confirmed [BUG-058-temperature-processor-reinit-guard.md](BUG-058-temperature-processor-reinit-guard.md): duplicate processor initialization is not guarded and can orphan sensor/device ownership.
- `init_temp_processor_events` ignores the `event_manager_subscribe` result and always reports success. This is an unverified failure-propagation gap; add it to the fault-injection matrix before deciding whether it merits a separate finding.
- Confirmed [BUG-059-coordinator-partial-init-cleanup.md](BUG-059-coordinator-partial-init-cleanup.md): coordinator partial initialization can retain its dispatcher handler after context teardown.

## Required deterministic tests

1. Valid aggregate then silence: prove direct heater inhibit occurs before coordinator pause bookkeeping and profile start rejects expiry.
2. Invalid/quorum-lost aggregate: prove target clears and sensor-data inhibit remains until configured consecutive recovery aggregates.
3. Duplicate temperature-processor init: prove no device count/pool/task/subscription change, then prove shutdown returns all allocations to baseline (F-058).
4. Event-subscription failure injection: prove initialization reports failure and no worker/device/context remains live after a correction.

## Next verification slice

Complete the coordinator task/timer stop and resume interleavings, then map Modbus device-table ownership and HMI command producers. No hardware action is authorized.
