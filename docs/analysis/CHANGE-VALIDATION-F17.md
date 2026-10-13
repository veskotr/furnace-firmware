# Change validation: F-017 local SSR-failure inhibit

- **Status:** software build passed; regression and hardware validation pending
- **Bug report:** [BUG-017-ssr-off-failure.md](BUG-017-ssr-off-failure.md)
- **Architecture decision:** [ADR-0001](../decisions/0001-future-fault-state-handling.md)

## Files changed

- `components/heater_controller_component/src/heater_controller_internal.h`
- `components/heater_controller_component/src/heater_controller_task.c`
- `components/heater_controller_component/src/heater_controller_events.c`

## Behavior changed

An SSR GPIO failure now latches a heater-local output inhibit, clears target demand, retries SSR-off, directly requests contactor-off, then posts the existing error event. Heater-on PWM, `SET_POWER`, `START`, and on-toggle commands are rejected while the latch is set. No HMI, global fault state, PID, profile, pin mapping, or configuration default changes were made.

## Verification

- Static review: source-level F-017 trigger rechecked; physical-off actions precede telemetry.
- Build: `IDF_PATH=/home/vesko/.espressif/v5.5.4/esp-idf cmake --build build -j2` passed; binary `0x61750`, 62% free in the smallest app partition.
- Repository tooling: `PYTHONDONTWRITEBYTECODE=1 python3 tools/codex/verify_repository_setup.py` must be rerun after final documentation updates.
- Regression test: not executed; the repository has no test harness/GPIO fault-injection seam. The oracle is documented in BUG-017 and remains required before release.

## Assumptions and remaining risks

- `stop_heater()` drives an electrically independent contactor-off path with known active polarity; unverified.
- GPIO driver calls may block on the shared GPIO mutex; this change removes dispatcher/HMI dependence but does not prove hard real-time latency.
- F-002/F-003 and global fault recovery remain unresolved.

## Hardware validation

Not performed. Follow [F17-HARDWARE-VALIDATION.md](F17-HARDWARE-VALIDATION.md) before release.
