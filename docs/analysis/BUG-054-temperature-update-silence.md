# BUG-054: A valid temperature aggregate does not expire when updates stop

- **Subsystem:** Temperature processor, coordinator, heater control gate
- **Severity:** critical
- **Confidence:** confirmed
- **Status:** fixed; deterministic regression and powered validation pending
- **Affected files and symbols:** `components/temperature_processor_component/src/temperature_processor_task.c:temp_process_task`; `components/temperature_processor_component/src/temperature_processor_events.c:device_manager_event_handler`; `components/coordinator_component/src/coordinator_component_events.c:temperature_processor_event_handler`
- **Prerequisite or linked IDs:** F-001, F-021, F-054
- **Ready to fix:** implemented

## Observed behavior

The processor produces a validity event only after a device-manager update notification. The coordinator evaluates aggregate age only while handling that event. Once it has accepted a valid aggregate and released the sensor-data inhibit, a silent/stalled update path produces no later event to make the aggregate invalid.

## Expected behavior

Temperature must become unusable for control no later than `CONFIG_COORDINATOR_SENSOR_AGGREGATE_STALE_TIMEOUT_MS` after the most recent accepted aggregate, even if no later device, processor, or event-loop activity occurs.

## Reproduction

1. Start with a valid aggregate and released sensor-data inhibit.
2. Stop or indefinitely block the device-manager/temperature-event path without posting an invalid aggregate.
3. Allow the coordinator PID timer to continue ticking beyond the aggregate stale timeout.

The coordinator retains the old temperature snapshot, remains authorized, and can continue publishing heater demand.

## Evidence

- The processor stamps/posts its sample, then blocks indefinitely on a task notification.
- Its only wake source is `DEVICE_MANAGER_UPDATED_EVENT`.
- The coordinator's freshness comparison uses the received event's `sample_tick`; it has no stored accepted-aggregate tick or periodic expiry check.

## Root-cause hypothesis

Freshness is modeled as an attribute checked on arrival rather than a time-bounded lease owned by the control path. Absence of a producer event is therefore indistinguishable from a still-fresh value.

## Violated invariant

Temperature is usable for control only when validity and freshness are explicit. A plausible cached float must not remain control-valid merely because no new failure event arrived.

## Minimal root-fix boundary

Coordinator validity/freshness state and the PID/control-tick safety gate; no HMI, Modbus protocol, profile, or global fault-manager redesign is required.

## Concurrency or timing relevance

The failure is an absence-of-publication condition across the device-manager task, temperature-processor task, private event loop, and coordinator control timer. The control timer must make expiry independent of those producers.

## Safety impact

Critical: stale temperature can continue through PID to queued heater demand. The existing heater-side sensor inhibit is the appropriate physical-off boundary but is not asserted on update silence.

## Implemented minimal fix

The coordinator now creates a one-shot `ESP_TIMER_TASK` expiry timer. Every accepted aggregate records its source tick and rearms that timer for the remaining age budget. At expiry, the callback sets the recoverable sensor-data state, directly calls `heater_controller_set_sensor_data_inhibit(true)`, and wakes the control task. The task pauses before it performs any profile/PID work. A later fresh aggregate clears the expiry state and follows the existing three-valid-aggregate recovery contract. Profile start independently rejects an expired aggregate.

Coordinator event shutdown now also unregisters the temperature-processor callback before the expiry timer is destroyed, so a later event cannot rearm a timer whose context is being released.

## Applied scope and correction authority

The narrow coordinator freshness/inhibit correction was implemented in the current hardening session. Deterministic regression and powered-controller validation remain required before release.

## ADR required, link, and status

No new ADR is required if the existing ADR-0002 recoverable-inhibit contract is preserved. Update ADR-0002 only if ownership or recovery semantics change.

## Regression test

### Failure-before oracle and result

No host/target test harness or fault-injection seam exists in this repository, so this oracle was not executed. The required mocked control-time test establishes a valid sample, advances beyond the stale timeout without another validity event, and shows the fixed path directly asserts sensor inhibit and pauses before a post-expiry positive heater command.

## Hardware validation

### Permission and required test level

No hardware operation was authorized or performed. After deterministic coverage, execute [F-054/F-001 powered-controller validation](F001-F054-HARDWARE-VALIDATION.md) to measure SSR/contactor-off state and latency after aggregate expiry.

## Dependencies

F-001, F-021, F-041, and test infrastructure WI-001/WI-002.

## Remaining uncertainty

The exact deployed GPIO polarity, electrical isolation, and output-off latency remain unverified.

## Resume conditions

Resume with a deterministic time/fault-injection seam for regression coverage and explicit powered-controller authorization before release.
