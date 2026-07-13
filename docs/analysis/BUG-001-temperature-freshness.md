# BUG-001: Heating control has no valid/fresh temperature contract

- **Subsystem:** temperature acquisition, coordinator, heater safety
- **Severity:** critical
- **Confidence:** confirmed
- **Status:** investigated; split implementation decision required
- **Affected files and symbols:** `temp_sensor_device_core.c:temp_sensor_create/temp_sensor_update/temp_sensor_read`; `temperature_processor_task.c:read_temp_sensors/temp_process_task`; `temperature_processor_events.c:post_temp_processor_event`; `coordinator_component_events.c:temperature_processor_event_handler`; `coordinator_component_heater_controller.c:start_heating_profile/heater_controller_task`
- **Prerequisite or linked IDs:** F-001, F-002, F-003, F-004, F-021
- **Ready to fix:** boot-start gate yes; complete stale-sample protection no, pending actuator-boundary decision.

## Observed behavior

Each newly allocated temperature sensor starts with `last_temperature = 0.0f`. The device manager can fail a physical Modbus update but still posts its update event. The temperature processor then reads and republishes cached floats, while the coordinator receives only a bare float with no validity, age, contributor, or physical-read-success information. `start_heating_profile()` uses the coordinator's zero-initialized `current_temperature` without a prior-sample check.

## Reproduction

No hardware was operated. Source-level paths:

1. Start a profile before the first successful physical read; coordinator uses its initial `0.0f` for duration/profile/PID setup.
2. After a successful read, cause repeated Modbus-update failures; the sensor cache remains unchanged, device-manager update events continue, processor republishes the cache, and coordinator has no way to distinguish it from a new measurement.

## Evidence

- `temp_sensor_create` initializes cache to zero; `temp_sensor_update` changes it only after successful `ms9024_read_float`.
- `temp_sensor_read` always returns the cache and no metadata.
- `device_manager_task` posts `DEVICE_MANAGER_UPDATED_EVENT` after each scan even when an update failed.
- `temperature_processor_task` posts a bare average whenever cached reads succeed; its event carries no validity/freshness contract.
- Coordinator stores only a plain float and profile start does not require a sample.
- The Modbus decoder already rejects NaN/Inf/out-of-range values, but that does not make an old finite cache fresh.

## Violated invariant

Heating may begin or continue only while the control task holds a synchronized temperature snapshot derived from successful physical reads, with defined age and contributor/quorum validity.

## Proposed phased correction

### Phase A: narrow boot-start hardening

Add a coordinator-owned "received valid temperature" gate and reject profile start until the processor has published a post-boot valid sample. This prevents the source-confirmed boot-zero start path without changing output architecture.

**Limit:** it does not protect an active run from later stale finite values, so it is only a partial F-001 correction.

### Phase B: complete freshness contract

1. Store a sample snapshot at the sensor boundary: value, successful-read tick, validity, and sensor identity.
2. Make the processor select only fresh valid snapshots and publish aggregate value, age, contributor count, and validity/quorum as one immutable message.
3. Transfer that message to the coordinator with one synchronization/ownership contract, resolving the overlapping F-021 race.
4. Reject profile start without a fresh valid snapshot; during a run, expiration must deny new demand and invoke an independent actuator inhibit.
5. Define recovery/resume after fresh data returns as part of the later fault-state architecture.

## Why Phase B cannot be a casual local patch

Timestamping a coordinator event is insufficient: a device-manager event can be emitted after failed Modbus reads while cached data is republished. Also, merely skipping PID on timeout leaves the previous positive heater target active; queued clear/stop commands inherit F-002/F-003. Complete stale-sample protection therefore requires a physical-output contract, not just another timeout.

## Decision needed

Choose one:

1. Implement Phase A now as an explicitly partial hardening fix, then defer stale-timeout handling until the direct inhibit/fault-state architecture is designed.
2. Design the sample snapshot and direct-inhibit contract now, addressing F-001/F-021 and the relevant F-002/F-003 interface together.

## Regression and hardware validation

Phase A oracle: without a processor sample, profile start returns an error and no heater START command is submitted; after a valid sample, existing start behavior remains available.

Phase B oracle: no successful read before start, repeated failed updates after a valid read, stale-age threshold, mixed fresh/stale sensors, and resume after recovery. Powered-controller validation must measure output behavior at expiry; mocks cannot prove physical off latency or wiring polarity.

## Remaining uncertainty

The required maximum sample age, quorum, sensor topology, and recovery policy are not defined. Current source initializes five sensors but configuration supports different counts; no schematic or operational requirement identifies whether one, a majority, or all sensors are required for safe heating.
