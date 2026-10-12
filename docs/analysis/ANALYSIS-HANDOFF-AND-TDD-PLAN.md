# Analysis handoff and staged TDD plan for the central ESP32

Status: proposal after A–F source reviews and the 2026-09-15 cross-component
challenge. This authorizes **no** production patch, test harness change,
Zephyr rewrite, flashing, or oven operation. Current-source evidence is
[baseline](ANALYSIS-BASELINE.md), [behavior](CURRENT-CONTROLLER-BEHAVIOR.md),
[coverage](ANALYSIS-COVERAGE.md), [findings](FIRMWARE_FINDINGS.md), and the
[regression backlog](REGRESSION-AND-VALIDATION-BACKLOG.md). The operating oven
has no independent hardware protection; the ESP32 checks are not certified
safety functions. The flashed source/configuration remains unverified even
though the user reports settings match `field-tests`.

## Outcome to stabilize before any framework migration

The central ESP32 must own process state, program management/validation,
temperature authorization, output requests, and truthful fault/command
results. The external HMI sends intent and receives controller status; it
does not own active-run or heater authorization
([POL-PROGRAM-OWNERSHIP-001](ESP32-ZEPHYR-PREPARATION.md)). The planned STM32
actuator and custom HMI hardware are deferred from this first controller
scope. Until hardware migration is separately designed/validated, the current
ESP32 still directly requests SSR/contactor/fan GPIO. Service-hour semantics
are deferred and must not hold up first-pass controller behavior.

Current behavior should be *characterized* first, then intentionally corrected
in bounded ESP-IDF batches with red-before/green-after tests. Only after the
corrected contracts are accepted should a small-agent Zephyr port reproduce
them. Zephyr's task/driver APIs cannot by themselves fix an unowned output
write, stale acquisition identity, or misleading RESUME status.

## Smallest test infrastructure and first red cases

1. Introduce a native GCC/CMake/CTest runner that compiles the actual
   profile, PID, and program-validation C with test-only `sdkconfig`,
   FreeRTOS-type, and logger shims. Keep it clearly separate from production
   build selection and physically output-safe. The first host red vector is
   valid 150→100 °C cooling false runaway (F-076/SCN-W1-006); pure profile
   math needs no oven or scheduler.
2. Add mocked integration seams around GPIO, private event post/queue,
   monotonic ticks/ESP timers, dispatcher queue/task admission, worker exit,
   Modbus read completion, and storage calls. Link the relevant real entry
   points where practical; do not replace the policy under test with a fake.
   Use named barriers and exact call-order counters, never sleeps or
   `volatile` timing guesses. Clean up/release every blocked fake and join
   test workers on failure.
3. Run SDK-specific queue/timer checks only in an ESP-IDF target test image on
   an isolated controller with heater outputs physically disconnected, under
   a separate approved plan. Host mocks are not proof of IDF callback
   latency, electrical polarity, or physical time-to-off.

No runner, seam, fixture or test has been implemented/run in this analysis.
WI-001/WI-002 in [future work](FUTURE_WORK_ITEMS.md) remain the test-runner and
output-safe-mode prerequisites.

## Correction batches and dependencies

| Batch | Red-before oracle and ordered correction boundary | Contract/test gate before the next batch |
| --- | --- | --- |
| 0 / characterize math | Host F-076 cooling false-trip vector, eased-ramp/time/rate vectors (`C-EASE-EST`, `C-MANUAL-RATE`), PID finite/clamp/reset and validator parity. | Explicit actual-versus-planned timing and direction-aware high-temperature policy; no gain tuning or cure-quality claim from host vectors. |
| 1 / final output response | F-075 full event queue must not delay SSR/contactor-low; F-077 persistent direct off-write error must latch/report and prevent later recovery/RESUME; F-074 admitted high writers must not follow completed off; F-070 live/ordinary cooling must revoke demand independent of dispatcher progress. | One authoritative output owner/admission/off protocol, selected software off deadline and fault recovery rule, barriers under queue-full and GPIO-error cases, no positive command after inhibited/no-heat state. F-078 requires truthful command/fault status. |
| 2 / coherent sensing and recovery | F-060 cache tuple race and F-063 device remove/update lifetime first; reject duplicate init F-058. Then F-071 one acquisition plus failed scans must not renew physical age/recovery credit; F-069 sensor-owned pause auto-resumes only after selected qualified acquisitions. Challenge `D-CAND-SENSOR-SPLIT` with timer/event barriers. | Explicit acquisition identity/time, validity/quorum/max age/skew, aggregate lease, pause reason and generation ownership; preserve operator/STOP/permanent inhibits. Modbus transmitter units/config need device evidence. |
| 3 / STOP and lifecycle | F-061 post-wake final command/full queue and F-072 destroy-before-join barrier; F-062 dispatcher producer admission; F-057/F-059 partial init; conditional handler/task-handle lifetime candidates. | Direct off before any join, no worker/profile/PID access after destroy, bounded stop/reinit with admitted producers/callbacks drained, no stale handler-argument high output. |
| 4 / program/HMI/ops | F-073 factory reset stack frame and explicit partial-delete result; controller-owned program validation/storage/active copy and command applied/rejected result; F-068 selected partition mismatch and logger task/storage/corrupt-record candidates. | External-HMI-independent domain behavior, stable active program across HMI loss, truthful STOP/status/fault, bounded logging that never blocks off. Service hours remain deferred. |

This is dependency order, not permission to perform the fixes or a claim all
findings have equal release risk. Output batches 1 and 2 interact: do not
enable automatic sensor recovery until output-failure latching, acquisition
qualification, and pause ownership are correct. In every patch, preserve
unrelated worktree edits and characterize current failure-before behavior.

## Provisional small-agent Zephyr work packages, after stabilization

Define interfaces and file ownership in a future Zephyr tree only after the
corresponding ESP-IDF corrected tests/contracts are accepted. One lead owns
cross-package safety/state integration; at most three bounded workers run
concurrently. Each package returns code, tests, config/ownership evidence and
an explicit handoff; none may flash or energize hardware without separate
authority.

| Package / proposed agent | Owned responsibility and required input | Integration gate |
| --- | --- | --- |
| Z-1 / platform and safe boot | Zephyr ESP32 board/config/build, output-safe test profile, monotonic time, boot failure defaults, partition/provisioning interface. No positive output until policy owner authorizes. | Build/config parity and safe default GPIO requests; physical polarity still unverified. |
| Z-2 / temperature and Modbus | Transmitter transport/device lifecycle, sequenced immutable measurement batch with identity/time/units/validity, configurable quorum and failure/reconnect. No control/output writes. | F-060/F-063/F-071 tests and agreed max age/skew/new-acquisition recovery contract. |
| Z-3 / pure cure profile and PID | Characterized phase/ramp/hold/cool/guard/PID mathematics with deterministic clock input; no HMI or GPIO dependency. | Host vectors for F-076, eased timing/rate, pause/resume/PID finite behavior. |
| Z-4 / coordinator and programs | Single owner for command authorization, fault/pause reasons, active-run state, program validation/management/snapshot, status and command applied/rejected outcomes. Consumes Z-2/Z-3 typed contracts. | F-069/F-078, STOP/recovery and immutable-program scenarios; explicit external command policy. |
| Z-5 / heater/fan output boundary | One owner for SSR/contactor/fan requests, direct off/fault latch, command generations/admission, window timing and bounded diagnostics; consumes coordinator intent, never HMI intent directly. | F-070/F-074/F-075/F-077 tests plus isolated powered-controller plan before electrical claim. |
| Z-6 / external HMI adapter and diagnostics | Versioned/bounded command/status/program transfer adapter, reconnect snapshot/results, logging/fault records with overload policy. No process or output ownership. | Controller-state-derived STOP/PAUSE, no implicit START/RESUME on reconnect; storage/logger failure vectors and protocol decision. |

Z-2 and Z-3 can run independently once their contract is fixed; Z-4 depends on
both. Z-5 can develop against mocked coordinator intent but must not be
integrated as positive hardware control until Z-4 authorization and the
output-safety acceptance gates pass. Z-6 depends on Z-4 command/program/status
contracts. This division is a proposal, not a selected architecture ADR or
permission to start the rewrite now.

## Open decisions and evidence gates

- Field flashed source/configuration and exact board/schematic/pin polarity.
  `field-tests` has no tracked `sdkconfig`; a saved field build/config/image
  record is needed before claiming these HEAD findings occurred on the oven.
- Physical/software off deadlines, contactor/SSR/fan feedback, airflow,
  absolute factory temperature ceiling and independent hardware cutout
  design. The production safety mechanism is separate from this operational
  firmware and is not installed on the current oven.
- Temperature acquisition identity, max physical age/scan skew, quorum,
  disagreement handling and qualified automatic sensor-recovery count.
- Exact cure-program bounds/acceptance: ramp accuracy, dwell tolerance,
  cooling behavior, overshoot/fault actions and interruption policy.
- Program persistence/import/versioning, external HMI transport/framing/
  authorization/command retry and reconnect snapshot contract. Furnace
  program ownership is confirmed; format is not. Service hours are deferred.

The [hardware-validation workflow](../../.agents/skills/plan-hardware-validation/SKILL.md)
cannot produce a powered-furnace procedure yet: independent de-energization
is absent on the operating oven, while qualified supervision,
schematic/polarity, an isolated rig/instruments and explicit authority have
not been established for this analysis. Classify future evidence as isolated RS-485 device
bench for transmitter units/faults, powered-controller dummy-load feedback
for GPIO/reset/off timing, then powered-furnace thermal/cure performance.
Existing [temperature-loss](F001-F054-HARDWARE-VALIDATION.md) and
[SSR-failure](F17-HARDWARE-VALIDATION.md) plans remain *planning inputs*, not
executed results or authority; recheck prerequisites before any use.
