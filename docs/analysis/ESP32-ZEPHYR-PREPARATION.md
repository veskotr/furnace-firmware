# Central ESP32 controller: Zephyr preparation

Date: 2026-09-14  
Status: scope confirmed by the user; behavior analysis and documentation only.  
Source baseline inspected: `1fcb4b1`, with existing documentation worktree changes.

## Scope and sequence

The immediate deliverable is a source-backed behavior and architecture
specification for the industrial composites curing oven's central ESP32
controller. The eventual implementation target is Zephyr. The HMI will be
external, connected through a defined control/status protocol; building that
HMI is outside this controller work.

The user explicitly deferred the other planned hardware work. The separate
STM32 actuator controller, its session/lease protocol, custom transmitters,
and the broader board-revision-two rollout are later work. Existing ADRs and
revision-two contracts remain future direction, not prerequisites for this
phase. This clarification changes sequencing; it does not revoke those ADRs.

Current ESP32 sensing, profile/PID, fan, SSR-window, contactor, and output-inhibit
behavior is the analysis baseline. Using the present local-output hardware for
the first Zephyr implementation is the working assumption while external
actuation is deferred; exact board, pins, polarity, and deployed configuration
still require a hardware record. No STM32-enforced protection may be assumed
present in this baseline.

Work proceeds through behavior documentation and acceptance scenarios, focused
tests and ESP-IDF corrections, then bounded Zephyr implementation packages.
This note implements no firmware, protocol, test harness, or hardware change.

## Deployment and improvement priorities

The user confirmed that the ESP-IDF firmware operates a real oven. The exact
deployed source revision, effective configuration, and hardware revision have
not yet been established; repository HEAD must not be assumed to identify the
field image.

The user later reported that the operating oven's settings match the
`field-tests` branch (2026-09-15). That branch has no tracked resolved
`sdkconfig`/`sdkconfig.defaults`, and this analysis HEAD includes later source
hardening. The report is useful deployment context, not independent proof of
the flashed settings or source revision; see the [baseline provenance](ANALYSIS-BASELINE.md).

`POL-HARDWARE-SAFETY-001` (user confirmation, 2026-09-15): the operating oven
has no independent hardware safety protections today. The ESP32 firmware may
perform operational checks, but it is not certified safety firmware. Independent
hardware safety mechanisms are planned for the production version; their
design, installation, and validation are future work. Current software reviews
must not assume an over-temperature cutout, E-stop, airflow proof, contactor
feedback, or an independent power-removal path is present.

The user prioritizes safety, error handling, elimination of races, memory use,
logging, temperature processing, and overall structure. The following are
proposed acceptance criteria for the analysis and later corrections, with
numeric bounds and product policies to be established from evidence.

| Priority area | Required specification/evidence |
| --- | --- |
| Safety and error handling | Each fault has a detection source, authoritative owner, output response, recovery/clear rule, operator-visible result, and test. Trace the response through SSR, contactor, and fan behavior; define response deadlines and independent hardware protections. |
| Races and resource lifetime | Each shared state and resource has an owner and synchronization/admission/exit protocol. Test adverse command, callback, timer, and device-update interleavings, including STOP, partial startup, and shutdown. A host test alone is not proof that all races are absent. |
| Temperature processing | Specify sample identity, measurement time, validity, freshness, units, scan skew, aggregation/quorum, disagreement, and recovery. Consumers receive a coherent value/metadata tuple. Preserve field behavior until any filtering/aggregation change has an agreed oracle. |
| Memory and execution bounds | Inventory static storage, heap allocations, stacks, queues, message sizes, and lifetimes; define limits, overload/allocation-failure behavior, and repeated-operation tests. Obtain target high-water/heap evidence before claiming resource headroom. |
| Logging and diagnosis | Specify transitions, command outcomes, faults, relevant temperature/output evidence, timestamps, correlation, and reset cause. Bound logging work and storage; define overflow/storage-failure behavior so diagnostics cannot prevent required control or output inhibition. |
| Structure | Separate process policy from HMI/transport and hardware details through explicit contracts and state ownership. Scope each later change to a tested behavior or boundary; a new RTOS alone does not resolve ownership or policy defects. |

Start the analysis with the output/fault-response matrix and temperature-to-
output authorization path, then shared-state/resource ownership. Use those
results to prioritize memory, diagnostics, and HMI contract work. Existing
audit findings are leads and evidence to reconcile, not a substitute for
checking the current source and deployed configuration.

No quantitative cure-cycle acceptance criteria were supplied. Ramp/hold timing,
overshoot/deviation tolerance, sensor aggregation, and automatic recovery must
therefore be documented before intentionally changing control behavior.

## Confirmed sensor-recovery policy

`POL-SENSOR-RECOVERY-001` (user confirmation, 2026-09-14): a run interrupted by
a temporary sensor fault should resume automatically once temperature data is
trustworthy again. An operator acknowledgement is not required solely for that
sensor recovery.

This selects automatic versus operator-confirmed sensor recovery; it does not
select new freshness/quorum/count values or prove that the current implementation
satisfies the policy. The review must distinguish a sensor-induced pause from
operator pause, STOP, completion and permanent output inhibits, and specify
which fault reason recovery clears. Existing unrelated authorization gates
remain part of the engineering invariants.

## Confirmed external-HMI and program ownership

`POL-PROGRAM-OWNERSHIP-001` (user confirmation, 2026-09-15): after HMI
externalization, the furnace controller manages cure programs and receives
operator commands through the HMI. The HMI is an external command/status
client, not the owner of program validation, active-run state, or output
authorization. Program persistence/legacy import format and protocol transport
remain to be specified; this direction does not implement them.

Service-hour semantics are explicitly deferred by the user (2026-09-15).
Current HMI-local accounting is a characterization/migration gap, not a
first-pass controller acceptance criterion until its product definition is
selected.

## Immediate specification coverage

- Boot/readiness and partial initialization; temperature acquisition, validity,
  freshness, aggregation, and recovery.
- Saved and manual programs; ramp, hold, cooldown, timing, live updates,
  pause/resume, stop, completion, and PID reset behavior.
- Heater/contactor/fan state in each operating and failure condition, including
  the direct output-inhibit path and prevention of stale reauthorization.
- Program acceptance and storage, process settings, operating-time records,
  faults, status, and reset behavior independent of the HMI implementation.
- Task/callback/queue ownership, timing, failure handling, and resource lifetime
  in the current ESP-IDF implementation.
- External command/status semantics and candidate acceptance tests before
  selecting transport encoding or implementing Zephyr adapters.

For each behavior record current source/configuration evidence, intended
behavior, differences or unresolved policy, observable acceptance criteria,
and validation level. Existing findings remain findings, and source fixes
remain unvalidated until their stated checks have actually run.

## HMI separation: current evidence and specification work

The enabled Nextion component currently combines presentation with process
input, persistence, and service accounting. Removing its UI code therefore
requires explicit disposition of the following behavior.

| Behavior | Current source evidence | Required controller/HMI contract work |
| --- | --- | --- |
| Program start, pause/resume, and stop | `handle_run_start`, `handle_run_pause`, and `handle_confirm_end` in [run handlers](../../components/nextion_hmi/src/events/nextion_run_handlers.c) submit coordinator commands; pause/resume selection uses HMI-local state. | Specify explicit commands, controller-side validation, state preconditions, and applied/rejected outcomes independent of display state. |
| Manual operation | `start_manual_program` and `live_update_if_running` in [manual handlers](../../components/nextion_hmi/src/events/nextion_manual_handlers.c) build a one-stage program and submit live target/rate updates. | Preserve the user-facing capability; decide which validated request represents manual execution and its duration/termination rules. |
| Program files and drafts | `nextion_storage_save_program` and `nextion_storage_parse_file_to_draft` in [storage](../../components/nextion_hmi/src/storage/nextion_storage.c) transfer panel-SD files and replace the draft; `program_draft_get` in [program models](../../components/nextion_hmi/src/program/heating_program_models.c) supplies a copy for start. | Define program upload/validation and active-program ownership. Decide controller-local library persistence, legacy import, and interruption behavior before selecting a storage layout. |
| Active execution | `start_heating_profile` in [coordinator control](../../components/coordinator_component/src/coordinator_component_heater_controller.c) copies the supplied program into its own `run_program`. | Document the existing execution copy and define how later HMI edits or disconnection interact with a running program. |
| Process settings and display clock | `handle_save_settings` in [settings handlers](../../components/nextion_hmi/src/events/nextion_settings_handlers.c) updates ambient/cooldown preferences and the panel RTC. | Identify controller-owned process settings and validation; distinguish process elapsed time from any display/calendar clock requirement. |
| Service hours | `nextion_run_tick` in [run handlers](../../components/nextion_hmi/src/events/nextion_run_handlers.c) adds seconds while its local profile-active flag is true, including pause; [program models](../../components/nextion_hmi/src/program/heating_program_models.c) implement persistence and reset preservation. | Specify authoritative elapsed-time accounting independent of HMI scheduling or telemetry reception, persistence failures, and factory-reset retention. |
| Reset/factory reset | `handle_restart` and `handle_factory_reset_confirm` in [settings handlers](../../components/nextion_hmi/src/events/nextion_settings_handlers.c) invoke direct heater inhibition before reset work. | Decide service-command availability and preserve controller-side output authorization and reset prerequisites. |

These are migration impacts, not newly confirmed defects or authorization to
move the implementations during documentation work. Detailed current HMI
evidence remains in [Audit 004](AUDIT-004-hmi-persistence.md).

## Control/status protocol preparation

The first protocol artifact should define operations and outcomes independently
of display pages or button names. Candidate coverage is program submission,
start, explicit pause/resume, stop, manual target/rate changes, status/fault
queries, and the required settings/program-management operations. Service and
destructive operations need a separate availability/authorization decision.

For each operation specify payload types, units and bounds, controller state
preconditions, validation, correlation, duplicate/retry behavior, bounded
admission, applied/rejected outcome, and reconnect behavior. Define a coherent
status snapshot with program state, phase, progress, setpoint, temperature
validity/freshness, requested output, available output feedback, and faults.
Requested output must not be reported as measured physical output.

The prior revision-two documents specify continuation of a healthy program on
HMI loss and no implicit command authority on reconnect. Carry these as the
existing intended direction while documenting current behavior and testing
the controller's independence from the HMI. Exact disconnection detection,
status resynchronization, and command-session rules still need specification.

RS-422 appears in the later hardware plan. This clarification does not select
the first controller's physical transport, wire encoding, or baud rate. Define
the operation contract first and resolve those choices before implementation.

## First milestone and handoff

The [agent execution plan](ANALYSIS-EXECUTION-PLAN.md) divides this analysis into
bounded reviewer assignments, a shared evidence contract, and lead integration.
Its tracking table distinguishes planned work from completed reviews.

The [baseline](ANALYSIS-BASELINE.md) and [component coverage index](ANALYSIS-COVERAGE.md)
complete the first execution step and establish the source/configuration inputs
for the detailed reviews.

The first A–F and cross-component source pass is integrated in the
[current-behavior draft](CURRENT-CONTROLLER-BEHAVIOR.md), reconciled
[findings](FIRMWARE_FINDINGS.md), and provisional
[TDD/agent handoff](ANALYSIS-HANDOFF-AND-TDD-PLAN.md). These documents are not
ESP-IDF corrections or Zephyr implementation authority. Field-image identity,
physical outputs and cure limits remain outside source validation.

The first milestone is complete when the current operating/failure scenarios,
state/output matrix, ownership map, HMI separation requirements, and prioritized
acceptance scenarios are source-backed and policy questions are clearly listed.
Reuse the [audit plan](REPOSITORY_AUDIT_PLAN.md),
[findings register](FIRMWARE_FINDINGS.md), and
[regression backlog](REGRESSION-AND-VALIDATION-BACKLOG.md).

Early test candidates are temperature-valid-then-silent output inhibition,
STOP with pending control work/full queues, partial initialization unwind,
profile timing, and command retries or
reconnection. Tests must distinguish current characterization from intended
corrected behavior. Software tests do not establish physical output state or
thermal performance.

Service-hour semantics/accounting tests are deferred until the user chooses
their product definition; current HMI-local accounting remains documented as
a migration gap.

Later agent packages should each contain owned files/responsibility, required
interfaces, behavior/finding IDs, acceptance tests, dependencies, and completion
criteria. Shared contracts and cross-component integration require an explicit
owner. No Zephyr implementation agents are commissioned by this note.

## Open product inputs

- Exact deployed ESP-IDF revision/configuration, observed problems,
  representative cure programs, and temperature/timing acceptance limits.
- Board/sensor/output configuration, schematic/polarity and the planned
  production independent hardware protections; none are present on the
  currently operating oven.
- Intended hold-entry timing, operator-pause behavior, detailed recovery
  qualification under `POL-SENSOR-RECOVERY-001`, restart behavior, and fan policy.
- External HMI link, controller-owned program persistence/legacy import, required settings,
  and service-command exposure for the first controller release.

## Evidence limit

This update records user-confirmed scope and the A–F/cross-component static
source pass. No firmware tests, new firmware build, flashing, or hardware
operation was performed for this analysis. Stored historical build metadata
is not a new build result.
