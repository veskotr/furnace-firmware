# F-002 change validation

Date: 2026-07-13  
Finding: F-002 — dispatcher self-deadlock can precede heater-off  
Status: source correction implemented; hardware validation pending

## Files changed

- `components/commands_dispatcher/src/commands_dispatcher_internal.h`
- `components/commands_dispatcher/src/commands_dispatcher_task.c`
- `components/commands_dispatcher/src/commands_manager_core.c`
- `docs/analysis/FIRMWARE_FINDINGS.md`
- `docs/codex/REPOSITORY_MAP.md`

## Behavior before and after

Before, a command handler running on the dispatcher task could submit a follow-up command to the same bounded queue with `portMAX_DELAY`. If the queue was full, the sole consumer blocked waiting for itself and queued heater-off commands could not execute.

After, a submission made by the dispatcher task invokes the registered handler directly. Calls from other tasks still use the queue, so the existing external command contract is preserved. No HMI behavior or production configuration was changed.

## Verification

- Production build: `IDF_PATH=/home/vesko/.espressif/v5.5.4/esp-idf cmake --build build -j2` — passed.
- Resulting image: `build/furnace-firmware.bin`, `0x61a40` bytes; 62% of the smallest app partition remains free.
- No automated dispatcher regression harness exists yet; the self-full-queue interleaving is not executed in software tests.

## Safety and hardware validation

The fix removes dispatcher self-deadlock from the software path but does not prove GPIO polarity, contactor isolation, SSR timing, or physical heater de-energization. Bench validation must fill the dispatcher queue, trigger coordinator pause/stop from a handler, and verify CLEAR/STOP execute and the SSR/contactor are off. Do not claim powered-furnace validation from this build.

## Remaining risks

- Inline nested handler calls increase dispatcher stack use and require characterization.
- Handler registration/teardown remains unsynchronized (F-007).
- The in-flight PID publication race remains F-003.
