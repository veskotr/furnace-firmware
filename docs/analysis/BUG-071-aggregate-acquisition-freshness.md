# BUG-071: Failed scans renew aggregate age and recovery credit

- **Subsystem:** sensor cache, temperature processor, coordinator lease
- **Severity:** high conditional control-input safety gap
- **Confidence:** confirmed source-level trigger; no field trace claimed
- **Status:** confirmed; correction not authorized
- **Affected files and symbols:** `temp_sensor_update`, `device_manager_task`, `read_temp_sensors`, `temperature_processor_event_handler`
- **Prerequisite or linked IDs:** F-001/F-054 expiry; F-060 cache race; F-063 sensor lifetime; F-069 auto-resume policy
- **Ready to fix:** no; acquisition identity/recovery qualification must be specified

## Observed behavior and reproduction

After a successful transmitter read, failed polls preserve the cached value
and successful-read tick. The manager still posts scan-complete events. The
processor can admit that cache for five seconds after the real read, then
stamps the aggregate with *processing* time. The coordinator accepts and arms
another five-second lease from that newer tick. One real acquisition followed
by failed scans can therefore remain accepted toward ten seconds of physical
age and can produce three valid aggregate events for recovery.

## Evidence

[Sensor update](../../components/temp_sensor_device/src/temp_sensor_device_core.c)
returns on failed PV read before cache writes.
[Manager scan](../../components/device_manager/src/device_manager_task.c)
posts `DEVICE_MANAGER_UPDATED_EVENT` after its update cycle regardless of
individual failures.
[Processor](../../components/temperature_processor_component/src/temperature_processor_task.c)
accepts cached successful-read ticks within
`CONFIG_COORDINATOR_SENSOR_AGGREGATE_STALE_TIMEOUT_MS`, then sets aggregate
`sample_tick=xTaskGetTickCount()`.
[Coordinator](../../components/coordinator_component/src/coordinator_component_events.c)
uses `sample_tick` for freshness and expiry, and increments recovery on each
accepted validity event. The tracked stale timeout is 5,000 ms and recovery
count is three; deployed values are unknown.

## Root cause and violated invariant

Aggregation time is substituted for acquisition time, and event count is
substituted for distinct acquisition evidence. The freshness invariant is that
control validity and recovery credit reflect explicit successful measurement
identity/time, not numeric plausibility or repeated processing of one cache.

## Minimal root-fix boundary, timing, and safety impact

Transfer a coherent per-sensor value/validity/acquisition-tick tuple (also
needed for F-060), preserve acquisition identity or oldest/newest accepted
tick in the immutable aggregate, and define a maximum physical sample age and
new-acquisition qualification rule at the coordinator boundary. Product
aggregation/quorum and scan-skew policy remain open; do not silently replace
the current average. The current one-shot expiry still mitigates total event
silence, but it does not enforce the intended physical acquisition age when
failed scans continue. There is no independent hardware sensor-loss cutout on
the operating oven; source evidence is not physical temperature proof.

## Allowed scope, ADR, and correction authority

Analysis/documentation only. A coherent tuple/interface or recovery-policy
change requires impact, Modbus, concurrency, control, and safety review;
explicit correction authority is not yet granted. A lasting aggregate schema
and recovery identity rule may require an ADR; none is accepted here.

## Regression test

`SCN-W1-004` in [current behavior](CURRENT-CONTROLLER-BEHAVIOR.md): fixed
monotonic ticks, one good read and repeated failed scans, cache/processor/
coordinator mocks. Failure-before: a newly stamped aggregate renews the lease
and increments recovery without a new acquisition. Passing correction:
physical sample age does not renew and recovery credit follows the agreed
distinct-acquisition rule; expiry still directly requests output off. No
deterministic test is implemented/run.

## Hardware validation, uncertainty, and resume conditions

No hardware action is authorized. Actual scan timings, transmitter PV units,
field configuration, GPIO polarity, and physical off latency remain unknown.
Resume after acquisition identity, maximum age/skew, quorum, recovery rule,
and injection seam are specified; then authorize a focused correction and
retain powered sensor-loss/off validation as a release gate.
