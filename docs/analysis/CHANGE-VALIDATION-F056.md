# F-056 change validation

- **Change:** destroy temperature-processor-owned sensor devices during normal shutdown
- **Date:** 2026-07-15
- **Status:** source fix validated by inspection/build; executable regression pending

## Files changed

- `components/temperature_processor_component/src/temperature_processor_core.c`
- `docs/analysis/BUG-056-temperature-processor-device-teardown.md`
- `docs/analysis/FIRMWARE_FINDINGS.md`
- `docs/analysis/CURRENT-FIX-STATUS.md`

## Behavior changed

After the temperature processor worker acknowledges exit, normal shutdown now calls the existing `destroy_devices` helper before deleting the exit semaphore and freeing the processor context. Sensor device-manager entries and static sensor-pool slots are therefore released for reinitialization.

## Tests executed

- No host/target regression harness exists in this repository, so the required initialize → shutdown → initialize oracle was not executable.
- Source inspection verified event unsubscription and worker acknowledgement precede owned-device destruction and context free.
- `git diff --check` — pass.
- `python3 tools/codex/verify_repository_setup.py` — pass.

## Build result

`IDF_PATH=/home/vesko/.espressif/v5.5.4/esp-idf cmake --build build -j2` — pass. The application image is `0x63cb0` bytes with 61% free in the smallest app partition.

## Unverified behavior and remaining risks

- Device-manager table mutation has no documented synchronization primitive; the change preserves the existing ownership boundary but stop/reinit concurrency coverage is still required.
- No powered hardware was flashed or energized. Physical sensor communication remains outside this cleanup proof.
