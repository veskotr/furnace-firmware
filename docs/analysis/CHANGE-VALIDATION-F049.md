# F-049 configuration validation

- **Change:** record the current resolved ESP-IDF configuration as the repository default baseline
- **Date:** 2026-07-15
- **Status:** baseline recorded; clean configuration passed; defaults-only build and field evidence pending

## Files changed

- `sdkconfig.defaults` (new; copied from the current resolved `sdkconfig`)
- `docs/analysis/FIRMWARE_FINDINGS.md`
- `docs/analysis/CURRENT-FIX-STATUS.md`

## Behavior/configuration changed

New configurations now inherit the complete reviewed `sdkconfig` baseline, including ESP-IDF and project options—not only locally added Kconfig values. The generated/local `sdkconfig` remains ignored and can still be changed per furnace or developer environment.

## Evidence

- `cmp -s sdkconfig sdkconfig.defaults` — pass; both contain 2,651 lines and are byte-identical.
- `git diff --check` — pass.
- `python3 tools/codex/verify_repository_setup.py` — pass.

## Limitations

- `idf.py save-defconfig` could not run because the configured ESP-IDF Python environment is missing in this workspace; the verbatim copy preserves the requested current configuration but is not minimized.
- A clean ESP-IDF configuration using only `sdkconfig.defaults` completed successfully and reproduced the recorded key effective symbols. A separate defaults-only build remains pending.
- No hardware or field tuning evidence is implied by recording the defaults.
