# Repository documentation and verification plan

Date: 2026-08-04  
Branch: `audit-docs-verification`  
Scope: document and verify the current ESP-IDF/FreeRTOS firmware without changing production behavior or operating hardware.

Scope update (2026-09-14): the audit now also supplies the behavior baseline for
[central ESP32 Zephyr preparation](ESP32-ZEPHYR-PREPARATION.md). The external
HMI control/status contract is in scope for specification; the separate STM32
actuator and other board-revision-two hardware work remain deferred. The
workstreams below continue to describe current-source analysis.

Execution update (2026-09-14): [baseline capture](ANALYSIS-BASELINE.md) and the
[complete component inventory](ANALYSIS-COVERAGE.md) are finished. Detailed
review waves and firmware regression work remain pending.

## Objective and boundaries

Build a source-backed, maintainable description of every production subsystem, its ownership and failure behavior; validate the existing hardening claims at the strongest safe level available; and record improvement opportunities without presenting them as defects until their triggers are proven.

This plan does **not** authorize firmware redesign, flashing, energizing outputs, configuration changes, or hardware operation. Each production-code correction needs its own investigated finding, safety review, and explicit authorization. The active architecture remains the one documented in [REPOSITORY_MAP.md](../codex/REPOSITORY_MAP.md); its safety invariants are in [ENGINEERING_STANDARDS.md](../codex/ENGINEERING_STANDARDS.md).

## Evidence rules

Every audit artifact must name the commit/configuration examined and distinguish these states:

| Status | Meaning |
| --- | --- |
| Source-mapped | Source/configuration and execution context were traced; no runtime claim follows. |
| Reviewed | The relevant specialist checklist was applied and source evidence is recorded. |
| Regression-covered | A deterministic host/target/mocked test proves its stated oracle. |
| Bench-planned | A safe hardware procedure, instrumentation, entry/exit criteria, and authorization boundary are recorded. |
| Bench-validated | Authorized hardware evidence exists; it is never inferred from build or mock results. |
| Finding candidate | A concern worth investigating; not a confirmed defect. |
| Confirmed finding | Reachable trigger/interleaving, exact current source evidence, violated invariant, and impact are recorded in `FIRMWARE_FINDINGS.md`. |

Keep historical findings and decisions intact. Update the map only when source verification changes a durable fact; record unresolved design choices as proposals or ADR candidates, not as assumed architecture.

## Safety-first execution order

1. Establish the reproducible audit baseline: commit, branch, ESP-IDF/toolchain, resolved `sdkconfig`, generated build metadata, and repository setup verification.
2. Trace every heater/SSR/contactor path from boot, commands, profile control, callbacks, errors, reset, shutdown, and failure returns. Confirm each path’s intended de-energization mechanism before reviewing secondary functions.
3. Verify temperature validity/freshness through Modbus, device lifecycle, aggregation, coordinator expiry, and the physical-output gate.
4. Review task/callback/queue/timer ownership and all stop/restart sequences before claiming lifecycle safety.
5. Review PID/profile behavior, HMI/persistence, configuration/build, fan and diagnostics.
6. Add focused regression coverage only after its oracle and fault-injection seam are documented; plan hardware validation for claims software cannot prove.

The currently highest-value validation backlog is already identified by F-001/F-054 (freshness expiry), F-055 (queue-full stop), F-056 (temperature-device teardown), F-050 (operating-time persistence), F-048 (profile shape), and the output-off checks in F-017/F-018. See [CURRENT-FIX-STATUS.md](CURRENT-FIX-STATUS.md) and [FIRMWARE_FINDINGS.md](FIRMWARE_FINDINGS.md).

## Workstreams and deliverables

| Order | Workstream | Coverage and required documentation | Verification / completion evidence |
| ---: | --- | --- | --- |
| 0 | Baseline and build | Replace the template root README with a project overview; record supported build commands, ESP-IDF version, resolved configuration authority, partition/app size, generated-versus-tracked files, and a component/dependency inventory. | Clean/configured build where available; `tools/codex/verify_repository_setup.py`; configuration-drift report. |
| 1 | Output safety | Heater controller, GPIO master, contactor/SSR pins, fan, startup/reset/shutdown, direct inhibits, command failure paths, and inactive run indicator. Add an output-state matrix covering boot, invalid temperature, pause, stop, completion, fault, reset, and teardown. | Safety review; deterministic command/inhibit tests; powered-controller plan for polarity, isolation, and time-to-off. |
| 2 | Sensing and Modbus | Modbus master lifecycle, MS9024 register/float contract, sensor pool/device manager ownership, freshness/quorum/aggregation, stale-data expiry, and consumer contracts. Add a data-validity diagram with timestamp source and expiry owner. | Modbus/device review; fault-injection tests for failures, sparse samples, silence, create/destroy, and reinit; device-bench plan. |
| 3 | Control and profiles | Program validation, profile states/timing/eased handover, PID input/output and reset rules, timer scheduling, output authorization, and fan policy dependencies. Document units, clamps, invariants, and expected transitions. | PID/profile review; mathematical/unit tests; controlled thermal characterization plan. |
| 4 | Concurrency and lifecycle | App startup order; all FreeRTOS tasks, event callbacks, timers, queues, mutexes, notifications, shared objects, producers, consumers, and teardown protocols. Add one lifecycle table per owner and a stop/restart sequence diagram for dispatcher, coordinator, heater, device manager, temperature processor, and HMI. | FreeRTOS review; queue-full and stop/reinit mocked tests; no destruction claim without worker/callback quiescence evidence. |
| 5 | HMI and persistence | Nextion RX/coordinator/transport, command grammar, destructive commands, program model, panel file protocol, NVS preferences, factory reset, and error/lifecycle display contracts. Document payload formats, bounds, retry/timeouts, and power-loss limits. | Parser/storage fault-injection tests; disabled-HMI build; panel-storage and power-loss bench plan. |
| 6 | Operations and diagnostics | Logger, debug console, error manager, health monitor, watchdog, crash data, operational-time records, maintenance workflows, and recovery behavior. Document active versus dormant components and escalation/mitigation boundaries. | Source review plus a proposal register; no activation of watchdog/fault paths without an approved architecture decision. |
| 7 | Configuration and release | All Kconfig symbols that affect safety/control, defaults versus resolved configuration, optional component gates, pins, timing, task resources, build profiles, and release provenance. | Defaults-only and alternate-Kconfig build matrix design; effective-symbol checks; release review. |
| 8 | Documentation closure | Reconcile `REPOSITORY_MAP.md`, `repository-map.yaml`, finding links, decisions, public header comments, README, and validation reports. | Link/path checks, repository setup verification, and a traceability index from subsystem → invariant → evidence → open item. |

## Artifact conventions

- Put source-backed subsystem notes in `docs/analysis/` and keep them concise: scope, source symbols, contexts, contracts, failures, verification, gaps, and links.
- Use `docs/analysis/BUG-<id>-<slug>.md` only after investigation meets the confirmed-finding rule. Use `CHANGE-VALIDATION-<id>.md` for test/build/bench evidence.
- Use `docs/decisions/` only for a durable decision with real alternatives; prospective improvements belong first in the proposal register below or [FUTURE_WORK_ITEMS.md](FUTURE_WORK_ITEMS.md).
- Keep `REPOSITORY_MAP.md` and `repository-map.yaml` as navigation summaries, not a duplicate of detailed audit notes.
- Each new or updated document must link to current source symbols and identify the configuration/commit reviewed.

## Improvement proposal register

These are intentionally not defect claims. Promote an item only after the appropriate investigation or architecture decision.

| ID | Candidate improvement | Why it matters | Next action |
| --- | --- | --- | --- |
| P-001 | Reproducible build profiles and effective-config checks | The tracked baseline and component defaults can diverge (F-049). | Document the configuration authority and design the build matrix (WI-003). |
| P-002 | Test pyramid and safe test mode | Current hardening claims lack deterministic coverage and hardware proof is distinct (F-039–F-041/F-052). | Define test seams, tiers, commands, artifacts, and output-safe test profile (WI-001/WI-002). |
| P-003 | Per-board hardware identity/configuration | Pin polarity, hardware revision, calibration, and validation state are unresolved deployment facts. | Inventory facts needed for a board record and prepare an ADR when rollout scope is known (WI-005). |
| P-004 | Central fault-state/mitigation design | Furnace-error events and disabled health monitoring do not currently define authoritative output mitigation (F-035/F-036). | Create an architecture decision only after the direct-inhibit paths and required severity/recovery policy are fully documented. |
| P-005 | Interface compatibility policy | Event payload evolution lacks version/size guarantees (F-051). | Inventory public event payloads and consumers; decide compatibility rules before the next incompatible interface change. |
| P-006 | HMI file-system resilience | Final Nextion replacement has a narrow power-loss limitation and registry visibility is incomplete (F-024/F-026). | Obtain panel protocol guarantees and run an authorized storage/power-loss validation plan. |
| P-007 | Sensor mapping and quorum policy | Current quorum and disagreement policy need hardware-informed confirmation (F-001/F-004). | Record sensor placement/identity and collect chamber evidence before changing aggregation policy. |
| P-008 | Operational documentation | The root README is still ESP-IDF sample text, obscuring actual use and safety boundaries. | Replace it during workstream 0 with a concise project/operator/developer entry point. |
| P-009 | Explicit component source lists | `heater_controller_component/CMakeLists.txt` uses `file(GLOB ...)`, conflicting with the repository component convention and weakening source-set review. | Inventory all component CMake source declarations and make any behavior-preserving build-file correction a separately reviewed task. |
| P-010 | Deployable partition and flash contract | The selected 2 MiB single-app layout lacks the logger's `storage` LittleFS partition; the unselected table assumes 8 MiB. | Choose one board/flash/table contract, then clean-configure and bench-validate mount/crash persistence. |
| P-011 | Configuration provenance | `sdkconfig.defaults` duplicates a generated resolved configuration, obscuring intentional product defaults and ESP-IDF provenance. | Define reviewed profile defaults and record the exact clean-resolution toolchain/lock state. |
| P-012 | Consistent source-list declarations | Most active components use `file(GLOB ...)`, hiding source-set changes until reconfigure. | Convert one stabilized component at a time to explicit `SRCS`, with clean configure/build evidence. |

## Cadence and definition of done

Use the [agent execution plan](ANALYSIS-EXECUTION-PLAN.md) for the central ESP32
analysis: independent read-only reviews may run in parallel, while one lead
integrates shared documentation one workstream at a time. For each subsystem:
map first, perform the applicable specialist review, write the concise note,
run the strongest available check within the assigned scope, add finding
candidates or proposals, then update the traceability index. Do not batch
source corrections with discovery.

The documentation/audit program is complete when every production component and dormant-but-retained component has a current owner/context/contract record; all physical-output and temperature paths have traceability to an invariant and validation status; every active finding has a planned or recorded verification level; every improvement proposal has a disposition; maps/README/decisions do not contradict source; and remaining hardware claims are explicitly marked planned rather than validated.
