# F-055 change validation

- **Change:** coordinator worker self-exit no longer submits dispatcher-backed heater commands
- **Date:** 2026-07-15
- **Status:** source fix validated by inspection/build; executable regression and powered validation pending

## Files changed

- `components/coordinator_component/src/coordinator_component_heater_controller.c`
- `docs/analysis/BUG-055-coordinator-stop-queue-deadlock.md`
- `docs/analysis/FIRMWARE_FINDINGS.md`
- `docs/analysis/CURRENT-FIX-STATUS.md`

## Behavior changed

`stop_heating_profile` now submits no queue-backed heater commands for either external STOP handling or coordinator worker self-exit. The direct control inhibit clears the target and drives SSR/contactor off before any worker acknowledgement; the next profile start reinitializes heater command state.

## Tests executed

- No host/target regression harness exists in this repository, so the required full-queue STOP/join oracle was not executable.
- Source review verified that the worker self-exit branch contains no dispatcher submission and that direct inhibit precedes the join acknowledgement.
- `git diff --check` — pass.
- `python3 tools/codex/verify_repository_setup.py` — pass.

## Build result

`IDF_PATH=/home/vesko/.espressif/v5.5.4/esp-idf cmake --build build -j2` — pass. The application image is `0x63cd0` bytes with 61% free in the smallest app partition.

## Static-analysis result

No standalone static-analysis tool is configured. Compiler/build diagnostics completed without a new warning for the changed path.

## Assumptions

- `heater_controller_set_control_inhibit(true)` is the authoritative direct output-off boundary and remains callable without the command dispatcher.
- The coordinator task handle remains published until the owner has joined the worker, so the self-exit check identifies the worker correctly.

## Unverified hardware behavior

SSR/contactors, GPIO polarity, wiring, and physical off timing were not tested. No firmware was flashed and no hardware was energized.

## Remaining risks

- The deterministic queue-full stop/restart regression remains to be implemented when a test seam is available.
- Other external producers can still block in unbounded dispatcher sends during global shutdown; that is outside F-055's confirmed coordinator self-exit boundary.
- F-056 has a source cleanup correction; its stop/reinitialize regression and device-manager synchronization characterization remain pending.
