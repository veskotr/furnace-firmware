# Change validation: F-053 non-finite control guards

- **Status:** build passed; deterministic regression coverage pending
- **Bug report:** [BUG-053-nonfinite-control-input.md](BUG-053-nonfinite-control-input.md)

## Files changed

- `components/pid_component/src/pid_component.c`
- `components/heater_controller_component/src/heater_controller_task.c`

## Behavior changed

Non-finite PID inputs/state/output reset PID and yield zero demand. A non-finite heater command clears existing target demand and is rejected. A non-finite stored target is treated as zero before PWM timing conversion.

## Verification

- Build command/result: `IDF_PATH=/home/vesko/.espressif/v5.5.4/esp-idf cmake --build build -j2` passed; binary `0x61980`, 62% free in the smallest app partition.
- Static oracle: the only PID return after any non-finite detected value is `0.0f`; the heater PWM reader returns `0.0f` for any non-finite stored target.
- Regression test: not executed; no repository test harness/GPIO or PID state injection seam exists.

## Assumptions and remaining risks

- `isfinite` follows the target toolchain's IEEE-754 semantics.
- F-001/F-021 remain: finite-but-stale or unsynchronized temperature is not detected here.
- Zero demand is sent through existing control behavior; this change does not replace the broader direct-inhibit work.
