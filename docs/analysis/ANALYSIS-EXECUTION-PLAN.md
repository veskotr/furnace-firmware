# Agent execution plan: central ESP32 analysis

Date: 2026-09-14  
Status: first A–F and cross-component source-analysis pass complete;
corrections, tests, field/hardware evidence and product decisions pending.  
Scope: [central ESP32 preparation](ESP32-ZEPHYR-PREPARATION.md).  
Starting repository revision: `1fcb4b1`, with existing documentation changes;
capture a fresh baseline at execution start.

## Team and responsibility

Use one lead to maintain scope, review cross-component conclusions, reconcile
documents, and collect product questions. Run at most three specialist reviewers
concurrently, with no nested delegation. This conservative schedule fits the
repository's current `.codex/config.toml` settings (`max_threads = 4`,
`max_depth = 1`) without changing agent configuration.

Reviewers inspect source and return bounded reports. The lead writes the shared
documentation, findings register, and maps. Nobody edits production firmware,
configuration, dependencies, or tests during this analysis pass; nobody flashes
or operates hardware. Existing worktree changes are retained.

Use smaller models for well-defined extraction and inventory work when model
selection is explicitly configured/requested. Keep cross-component safety,
concurrency conclusions, and final synthesis under lead review. A narrower task
does not by itself select a smaller model. No model/configuration changes are
made by this plan, and no cost or duration saving is assumed.

## Baseline before delegation: lead

1. Record commit, branch, dirty/untracked paths, and the effective configuration
   provenance. Identify the relevant source/configuration hashes so later user
   edits can be distinguished from the reviewed baseline. Do not stash or reset.
2. Read the repository instructions, maps, engineering standards, findings,
   preparation scope, and existing audits. Record which existing claims need
   source revalidation or reconciliation rather than repeating the audit prose.
3. Inventory every local component and classify it using startup/build evidence
   as active, optional, dormant, excluded legacy, or build-only support. Assign
   each component to a primary package below. Generated/vendor internals are
   inspected only when an application claim depends on them.
4. Record available build/toolchain metadata and distinguish historical build
   results from checks actually run in this pass. A read-only analysis does not
   require installing Zephyr or rebuilding firmware to begin.
5. Give all reviewers the same baseline and the shared report contract below.
   Unknown field revision, interlocks, and cure limits become explicit questions;
   they do not prevent source analysis or authorize guessed physical behavior.

## Wave 1: operating behavior and output authorization

These packages can inspect their assigned paths independently after the baseline.

| ID / role | Primary source responsibility | Required report / existing home |
| --- | --- | --- |
| A / Safety reviewer | `heater_controller_component`, `gpio_master_driver`, `fan_controller_component`, `run_indicator`; output-relevant startup, coordinator, reset, and fault callers | State/output matrix covering boot, partial initialization, ready/running, pause, stop, completion, sensor loss, faults, reset and shutdown. Trace each energization and inhibition path; distinguish GPIO requests from electrical outcomes and identify missing fault responses. Reconcile [Audit 001](AUDIT-001-baseline-output-safety.md). |
| B / Modbus and device reviewer | `modbus_master`, `temp_sensor_device`, `device_manager`, `temperature_processor_component`; coordinator temperature acceptance/expiry | Sample lifecycle from transaction to control input: identity, conversion, timestamps, validity, scan timing, aggregation/quorum, disagreement, expiry and recovery. Include failure, reconnect and device/cache ownership evidence. Reconcile [Audit 002](AUDIT-002-temperature-authorization.md) and [Audit 003](AUDIT-003-modbus-device-ownership.md). |
| C / Control-system reviewer | `coordinator_component`, `temperature_profile_controller`, `pid_component`, `heating_program_validation`, process types in `common` | Operator scenarios and state transitions; units, timing, ramp/hold/cooldown, manual updates, PID clamps/reset, pause/resume/stop and fault handling. Compare displayed/planned versus executed timing, record config dependencies, and trace output publication to A's boundary. Reconcile [Audit 005](AUDIT-005-pid-profile-control.md). |

Lead work during wave 1: construct the component coverage index and consolidate
questions about deployed hardware/configuration and intended cure behavior.
Do not independently repeat all three source reviews. Check evidence at their
shared interfaces and resolve inconsistent terminology when results arrive.

Wave-1 integration gate: agree on current temperature authorization, process
state, and physical-output request paths before using those reports as inputs
to later synthesis. Unresolved product policy stays explicitly unresolved.

## Wave 2: ownership, HMI separation, and operational limits

| ID / role | Primary source responsibility | Required report / existing home |
| --- | --- | --- |
| D / Concurrency reviewer | `commands_dispatcher`, `event_manager`; cross-task state, timers, queues, locks, callbacks and resource lifetimes across every inventoried active/optional component | Owner/reader/writer/context table; admission and exit protocols; blocking/lock order; precise adverse interleavings. Inventory allocations, stacks, queues and message bounds, separating configured budgets from measured use. Cross-check A–C ownership claims; include HMI/logger lifetimes without repeating their functional reviews. Reconcile [Audit 006](AUDIT-006-dispatcher-device-lifecycle.md) and referenced lifecycle findings. |
| E / Repository analyst | `nextion_hmi` including nested Kconfig, program model, run/manual/settings handlers, transport, panel storage and HMI-owned service records | External operation inventory and HMI/domain separation matrix; data types, validation, settings, program persistence, service hours, reset and status. Identify controller-owned behavior, presentation-only behavior, compatibility decisions and unresolved protocol semantics. Reconcile [Audit 004](AUDIT-004-hmi-persistence.md) and the HMI section of the preparation note. No wire-format or transport selection. |
| F / Repository analyst | `main`, root/component build files, Kconfig/config/partition files, `logger_component`, `debug_console`, `error_manager`, `health_monitor`, `time_component`, `transmitter_diagnostics`, `spi_master_component`, excluded `temperature_monitor_component`, shared utility/build support | Active/dormant/build classification, effective-config drift, storage/flash contract, logging queues/backpressure/persistence, timestamps/reset evidence, watchdog/error integration and resource settings. Cover dormant legacy code proportionately with activation prerequisites. Reconcile [Audit 007](AUDIT-007-operations-diagnostics.md) and [Audit 008](AUDIT-008-configuration-build.md). |

The lead assigns any newly discovered unowned component before closing coverage.
Source overlap is intentional only at interfaces or for D's concurrency lens;
one package remains responsible for each component's functional description.
Reviewers forward cross-boundary questions to the lead instead of silently
expanding into another full subsystem review.

Lead work during wave 2: reconcile A–C reports, preserve existing finding IDs,
draft the current-behavior specification, and resolve unsupported prior claims.

## Shared reviewer assignment

Use this instruction with the selected package, role, and captured baseline:

> Analyze only your assigned package for the central ESP32 controller. Read
> AGENTS.md, applicable nested instructions, the repository map, engineering
> standards, findings, and ESP32-ZEPHYR-PREPARATION.md. Use the specialist skill
> required by your role. Check current source and effective configuration;
> historical documents are leads. The HMI is external in the eventual target;
> STM32 actuation and the other new hardware remain deferred. Return current
> behavior and evidence before proposing changes. You are not alone in the
> worktree: do not edit files, revert others' work, launch agents, install tools,
> change configuration, or operate hardware. Report any baseline drift to the
> lead. Return the report contract below and stop at the package boundary.

## Report contract

Every package returns:

1. Baseline and coverage: examined files/symbols/configuration, active/dormant
   status, and explicit omissions or source changes noticed during the review.
2. Behavior: inputs, outputs, state transitions, timing/units, defaults,
   preconditions, and failure/recovery behavior with exact file/symbol evidence.
3. Ownership: contexts, readers/writers, resources, synchronization, blocking,
   initialization and shutdown; reference another package for deeper evidence.
4. Findings: existing F-ID or package-local candidate ID, reachable trigger or
   interleaving, violated invariant, impact, confidence and uncertainty. A
   missing test or desired improvement is not a confirmed bug.
5. Acceptance scenarios: initial state, inputs/faults/time sequence, expected
   observable result, lowest useful test level, injection seam and remaining
   hardware evidence. Mark current characterization versus intended correction.
6. Exact documentation corrections and product questions. A recommendation
   must explain the affected contract; do not redesign the target architecture.

Use package prefixes for provisional behavior/scenario/candidate IDs so workers
do not collide. The lead alone allocates new permanent F-IDs and deduplicates
equivalent findings. Keep reports concise but do not omit evidence to meet an
arbitrary length target.

## Wave 3: cross-component review and test handoff

After A–F are integrated, use at most two bounded follow-up assignments:

- **Test engineer, design only:** review the accepted scenarios, recommend the
  smallest host/mocked/target harness and injection seams, distinguish pure
  logic from RTOS scheduling tests, and order the first regression cases.
  Update recommendations for the existing regression backlog through the lead.
  Do not implement a test harness or operate hardware in this assignment.
- **Safety reviewer, cross-component challenge:** check the combined traces for
  sensor silence during a run, STOP/pause with pending commands or full queues,
  failed initialization, reset, and HMI/storage/logging stalls. Resolve actual
  gaps in A–F evidence; this is not a second complete source audit.

The lead reconciles disagreements against source, identifies policy choices
that need the user, and produces the final analysis and ordered correction
plan. Architecture options can be proposed with tradeoffs; decisions needing
product input are not silently accepted. A Zephyr implementation task breakdown
follows only after the relevant behavior and test contracts are settled.

## Deliverables and completion gate

Reuse and update the existing eight audit notes, findings, fix-status report,
regression backlog and repository maps. Add one concise current-behavior
specification with scenario/requirement IDs and a coverage/traceability index
linking component → behavior/invariant → source → finding → proposed or executed
test. The index includes every local component, including dormant/excluded ones.

The pass is complete when:

- All inventoried components have an owner and evidence-backed disposition.
- Operating/failure states, temperature authorization and output responses are
  documented, with hardware assumptions and product-policy gaps visible.
- Shared-state/resource ownership and configuration/memory/logging limits have
  evidence or an explicitly identified measurement gap.
- HMI-dependent process behavior and proposed external operations are mapped.
- Existing hardening claims and finding statuses are reconciled; new confirmed
  findings have reachable triggers and no duplicate permanent IDs.
- Each prioritized correction has a proposed test oracle and dependencies;
  no unexecuted test is marked as passed.
- Cross-component contradictions are resolved or listed with a concrete owner
  and missing decision/evidence; documentation checks pass.

Run `python3 tools/codex/verify_repository_setup.py` and `git diff --check` after
documentation integration. Report any firmware builds/tests separately with
their actual commands and outcomes. Analysis completion does not imply
release readiness, absence of all defects, or physical safety validation.

## Execution tracking

| Work | Status |
| --- | --- |
| Lead baseline and coverage inventory | Complete — [baseline](ANALYSIS-BASELINE.md), [coverage](ANALYSIS-COVERAGE.md), source/configuration fingerprints and two read-only reviewer reports integrated. |
| A: output safety | Complete — read-only unchanged-baseline review integrated in [current behavior](CURRENT-CONTROLLER-BEHAVIOR.md), [Audit 001](AUDIT-001-baseline-output-safety.md), and F-069/F-070. Physical output state remains unverified. |
| B: temperature and Modbus | Complete — read-only review integrated in [current behavior](CURRENT-CONTROLLER-BEHAVIOR.md), [Audit 002](AUDIT-002-temperature-authorization.md), and F-071. Transmitter units/field configuration remain unverified. |
| C: coordinator/PID/profiles | Complete — read-only review integrated in [current behavior](CURRENT-CONTROLLER-BEHAVIOR.md) and [Audit 005](AUDIT-005-pid-profile-control.md). Mathematical/timing candidates need test vectors. |
| D: concurrency and memory | Complete — read-only ownership/interleaving review integrated in [Audit 006](AUDIT-006-dispatcher-device-lifecycle.md), F-072/F-074/F-075; several conditional teardown candidates need targeted tests/upstream guarantees. |
| E: HMI and protocol boundary | Complete — bounded read-only review integrated in [current behavior](CURRENT-CONTROLLER-BEHAVIOR.md), [Audit 004](AUDIT-004-hmi-persistence.md), and F-073; protocol/product choices remain open. |
| F: operations/configuration/build | Complete — bounded read-only review integrated in [current behavior](CURRENT-CONTROLLER-BEHAVIOR.md), [Audit 007](AUDIT-007-operations-diagnostics.md), [Audit 008](AUDIT-008-configuration-build.md); conditional logging candidates remain untested. |
| Cross-component review and test design | Complete — read-only safety challenge confirmed F-077/F-078 response gaps; test-design report integrated in [TDD handoff](ANALYSIS-HANDOFF-AND-TDD-PLAN.md). No tests or hardware were run. |
| Integrated specification and correction plan | Complete as an analysis proposal — [behavior draft](CURRENT-CONTROLLER-BEHAVIOR.md), [coverage](ANALYSIS-COVERAGE.md), [findings](FIRMWARE_FINDINGS.md), [regression backlog](REGRESSION-AND-VALIDATION-BACKLOG.md), [TDD/agent handoff](ANALYSIS-HANDOFF-AND-TDD-PLAN.md). Correction authority/product choices remain open. |

The initial baseline was executed on 2026-09-14 with two read-only reviewers for
component classification and build/configuration provenance. Their evidence is
integrated in the baseline and coverage documents. No A–F behavior-review wave,
firmware build/test, production change, or hardware action ran in that step.

The A–F and bounded cross-component reviews ran later against the unchanged
captured source. One D/F reviewer pair hit a usage-limit interruption; the
source leads they had already returned were source-checked and their bounded
remaining reviews completed in resumed assignments. No reviewer edited
production or documentation. The lead alone integrated documentation.
Product decisions and field/physical evidence remain explicit gaps rather
than assumed pass criteria. The user confirmed no independent hardware
protection on the operating oven, controller-owned program management after
external HMI, and deferral of service-hour semantics.

## Delegation reference

The division into bounded read-only reports and lead synthesis follows the
[official subagent guidance](https://learn.chatgpt.com/docs/agent-configuration/subagents).
Agent configuration remains unchanged; the lead should verify available roles
and execution limits when starting a new session.
