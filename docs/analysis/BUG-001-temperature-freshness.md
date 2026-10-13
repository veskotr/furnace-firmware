# BUG-001: Heating control has no valid/fresh temperature contract

- **Subsystem:** temperature acquisition, coordinator, heater safety
- **Severity:** critical
- **Confidence:** confirmed
- **Status:** Phase B source fix implemented; regression and hardware validation pending
- **Affected files and symbols:** `temp_sensor_device_core.c:temp_sensor_create/temp_sensor_update/temp_sensor_read`; `temperature_processor_task.c:read_temp_sensors/temp_process_task`; `temperature_processor_events.c:post_temp_processor_event`; `coordinator_component_events.c:temperature_processor_event_handler`; `coordinator_component_heater_controller.c:start_heating_profile/heater_controller_task`
- **Prerequisite or linked IDs:** F-001, F-002, F-003, F-004, F-021
- **Architecture decision:** [ADR-0002](../decisions/0002-recoverable-sensor-data-inhibit.md)
- **Ready to fix:** implementation complete in the current architecture; executable regression coverage and powered validation remain open.

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

Implemented: a coordinator-owned atomic "received valid temperature" gate rejects profile start until the processor has published a post-boot accepted sample. The check precedes profile loading and all heater command submission, preventing the source-confirmed boot-zero start path without changing output architecture.

**Limit:** it does not protect an active run from later stale finite values, so it is only a partial F-001 correction.

### Phase B: complete freshness contract (implemented)

1. Sensor devices record successful-read ticks and sample validity.
2. The processor selects fresh samples, compacts successful reads, applies a model-level quorum (all but up to two sensors), and publishes a validity snapshot alongside the legacy float event.
3. Coordinator temperature updates are mutex-protected; the validity snapshot drives the recoverable inhibit and recovery counter.
4. Invalid/stale aggregates directly inhibit the heater, force contactor-off, pause the profile, and stop elapsed-time accumulation.
5. Three consecutive valid aggregates release only the recoverable inhibit and resume a paused profile when automatic recovery is enabled.

## Implementation limits

The implementation keeps the legacy float event for HMI/fan consumers and adds a separate validity snapshot event, so no HMI contract changed. The quorum is temporarily derived as `sensor_count - 2` (minimum one); per-board sensor mapping/quorum remains future work. Disagreement remains a warning, matching the field requirement.

## Configuration

Defaults are `COORDINATOR_SENSOR_AGGREGATE_STALE_TIMEOUT_MS=5000`,
`COORDINATOR_SENSOR_RECOVERY_VALID_AGGREGATES=3`, and automatic recovery enabled.
These are model-level settings; per-board sensor count, mapping, timeout, quorum,
and plausibility ranges remain future work.

## Regression and hardware validation

Phase A oracle: without a processor sample, profile start returns an error and no heater START command is submitted; after recovery, existing start behavior remains available.

Phase B oracle: no successful read before start, repeated failed updates after a valid read, stale-age threshold, mixed fresh/stale sensors, and resume after recovery. Powered-controller validation must measure output behavior at expiry; mocks cannot prove physical off latency or wiring polarity.

## Remaining uncertainty

The temporary quorum is derived from sensor count. Per-board sensor topology, mapping, and exact quorum remain future configuration work; physical off latency and GPIO polarity require bench validation.
