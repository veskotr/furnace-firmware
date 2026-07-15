# F-050 change validation

- **Change:** harden the operational-time service override
- **Date:** 2026-07-15
- **Status:** source fix validated by inspection/build; persistence regression pending

## Files changed

- `components/nextion_hmi/src/program/Kconfig`
- `components/nextion_hmi/src/program/heating_program_models.c`
- `docs/analysis/FIRMWARE_FINDINGS.md`
- `docs/analysis/CURRENT-FIX-STATUS.md`

## Behavior changed

The override hours are constrained to `0..1193046`, the maximum range that converts to seconds without overflowing `uint32_t`. Runtime validation remains in place for manually edited or inconsistent generated configuration. The operational-time counter is updated only after NVS open/set/commit succeeds; failures are logged and the previously loaded value is retained.

The documented service workflow remains one-shot by operator action: enable the override, boot once, then disable it. If left enabled, it continues to apply on every boot by design.

## Tests executed

- No host/target persistence regression harness exists in this repository.
- Source inspection covered negative/oversized values, uint32 conversion bounds, NVS open/set/commit failure handling, and the successful write path.
- `git diff --check` — pass.
- `python3 tools/codex/verify_repository_setup.py` — pass.

## Build result

`IDF_PATH=/home/vesko/.espressif/v5.5.4/esp-idf cmake --build build -j2` — pass. The application image is `0x63cb0` bytes with 61% free in the smallest app partition.

## Remaining risks

- No fault-injection test proves the NVS error paths.
- The override is persistent while enabled; accidentally leaving the Kconfig option enabled will continue resetting the counter on boot.
- No hardware validation is required for this arithmetic/persistence correction.
