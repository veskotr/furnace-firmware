# F-001 Phase B change validation

Date: 2026-07-13  
Finding: F-001 — heating control has no valid/fresh temperature contract  
Status: source fix implemented; regression and hardware validation pending  
Architecture contract: [ADR-0002](../decisions/0002-recoverable-sensor-data-inhibit.md)

## Behavior before and after

Before, failed Modbus reads left cached finite values indistinguishable from new samples, and the coordinator had no actuator-level sensor inhibit. After, successful physical reads carry tick metadata; stale samples older than the configured 5 seconds are excluded; the aggregate requires all-but-two sensors (minimum one); disagreement remains a warning; invalid/stale aggregates directly inhibit SSR and contactor, pause the profile, and preserve stage elapsed time; three consecutive valid aggregates release only the recoverable inhibit and resume the same profile. Existing legacy float events remain available to HMI/fan consumers.

## Verification

- Production build: `IDF_PATH=/home/vesko/.espressif/v5.5.4/esp-idf cmake --build build -j2` — passed.
- Resulting image: `build/furnace-firmware.bin`, `0x620c0` bytes; 62% of the smallest app partition remains free.
- Repository setup verification: passed previously; focused regression tests are not available yet.

## Hardware validation required

Bench-test sensor read loss, one/two sensor dropout, stale timeout, recovery, and pause duration. Measure SSR and contactor de-energization latency and verify active polarity/electrical independence. Test Modbus reconnect without automatic heating before three valid aggregates. No powered-controller or powered-furnace validation was performed.

## Remaining risks

- Quorum is a temporary model-level `sensor_count - 2` rule; per-board configuration remains future work.
- Direct GPIO calls may block on existing GPIO ownership/mutexes.
- F-003 (in-flight PID publication) and F-007 teardown remain separate findings.
