# Change validation: F-001 Phase A boot temperature gate

- **Status:** build passed; deterministic regression coverage pending
- **Bug report:** [BUG-001-temperature-freshness.md](BUG-001-temperature-freshness.md)

## Files changed

- `components/coordinator_component/src/coordinator_component_internal.h`
- `components/coordinator_component/src/coordinator_component_events.c`
- `components/coordinator_component/src/coordinator_component_heater_controller.c`

## Behavior changed

Profile start now returns `ESP_ERR_INVALID_STATE` until the coordinator has received at least one accepted temperature-processor event. The rejection happens before profile load, PID reset, and heater CLEAR/START command submission.

## Verification

- Build: `IDF_PATH=/home/vesko/.espressif/v5.5.4/esp-idf cmake --build build -j2` passed; binary `0x61a00`, 62% free in the smallest app partition.
- Static oracle: a zero-initialized `has_valid_temperature` flag causes start to return before the first `send_heater_command` in `start_heating_profile`; the event handler sets the flag only after its current invalid-value guard passes.
- Regression test: not executed; the repository does not yet have a runnable coordinator/event test harness.

## Deliberate limit

This is Phase A only. It does not establish sample freshness during an active profile, validity/quorum metadata, synchronized temperature ownership, or independent output inhibit on sensor timeout. Those remain F-001 Phase B and F-021/F-002/F-003 architecture work.
