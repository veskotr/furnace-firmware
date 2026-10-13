# Change validation: F-054 aggregate-expiry safety gate

Date: 2026-07-15
Finding: F-054 — valid temperature never expires when updates stop
Status: source fix implemented; deterministic regression and powered-controller validation pending
Architecture contract: [ADR-0002](../decisions/0002-recoverable-sensor-data-inhibit.md)

## Intended behavior

After each accepted temperature aggregate, the coordinator owns a one-shot expiry deadline derived from that aggregate's source tick. If no later accepted aggregate arrives before the configured stale timeout, expiry must directly assert the heater recoverable sensor-data inhibit and request physical SSR/contactor off without waiting for dispatcher, HMI, or another sensor event. The coordinator control task is then awakened and pauses before profile/PID work can publish another demand. A later fresh aggregate follows the existing three-valid-aggregate recovery policy.

## Source evidence

- `coordinator_component_events.c` creates a one-shot `ESP_TIMER_TASK` expiry timer, arms it for each accepted aggregate's remaining age budget, and directly calls `heater_controller_set_sensor_data_inhibit(true)` in the expiry callback.
- The callback sets coordinator expiry state and notifies the control task; `coordinator_component_heater_controller.c` checks that state immediately after wake-up and pauses before elapsed-time, profile, or PID work.
- `start_heating_profile` checks the coordinator-owned aggregate lease and refuses a stale start while asserting the same recoverable inhibit.
- Event shutdown now unsubscribes from temperature-processor events before deleting the expiry timer, preventing a callback from rearming it during coordinator teardown.

## Executed verification

- Production build: `IDF_PATH=/home/vesko/.espressif/v5.5.4/esp-idf cmake --build build -j2` — passed.
- Resulting image: `build/furnace-firmware.bin`, `0x63cc0` bytes; 61% of the smallest app partition remains free.
- `git diff --check` — passed.
- `python3 tools/codex/verify_repository_setup.py` — passed (11 agents, 15 skills).
- No host or ESP-IDF target test harness exists in this repository; the deterministic regression below was not executed.

## Required deterministic regression

Use a mocked ESP timer, coordinator context, and heater-inhibit boundary to establish a valid aggregate, advance to just before expiry, then cross the expiry deadline without any later event. Assert all of the following:

1. the expiry callback invokes recoverable sensor-data inhibit before coordinator pause/telemetry;
2. no positive heater demand is accepted after expiry;
3. coordinator pauses before profile/PID work on the notified wake-up;
4. a fresh aggregate resets the expiry lease but cannot release the inhibit until the configured recovery count is met;
5. stale start is rejected even if no control task is currently running;
6. coordinator teardown unsubscribes temperature events before expiry-timer destruction.

## Hardware validation

No hardware was flashed, powered, or operated. Execute [F001-F054-HARDWARE-VALIDATION.md](F001-F054-HARDWARE-VALIDATION.md) only with explicit authorization.

## Residual risks

- Compilation does not prove timer dispatch latency, GPIO polarity, electrical isolation, or physical de-energization latency.
- F-003 still covers in-flight output publication outside this aggregate-expiry boundary.
- F-055/F-056 source corrections are present but their deterministic lifecycle regressions remain pending.
