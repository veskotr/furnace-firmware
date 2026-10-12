# Coordinator state and command model

Status: target contract with explicitly unresolved pause/recovery policy.
Date: 2026-08-04

The revision-two coordinator owns furnace process state, one active program
execution, command authorization, profile progression, and PID demand. It acts
on coherent component snapshots. It does not own device transport details or
physical actuator state.

## State model

Avoid a single enumeration containing every combination of program, fault,
communications, and service state. The coordinator maintains three related but
separate dimensions and publishes them together in one furnace snapshot.

### System availability

| State | Meaning |
| --- | --- |
| `STARTING` | Boot configuration and required components are not yet verified. |
| `OPERATIONAL` | Required components and fresh control inputs satisfy the current readiness policy. |
| `DEGRADED` | Operation may continue, but a warning or optional dependency is unavailable. |
| `INHIBITED` | Heating cannot start or continue until the reported condition is resolved. |
| `SERVICE` | A physically local technician session is active; production commands are restricted by service policy. |
| `UPDATING` | Configuration or OTA operation is active in an enforced non-running safe state. |

### Program execution

| State | Meaning |
| --- | --- |
| `NONE` | No program is loaded. |
| `LOADED` | One validated program is ready but not executing. |
| `RUNNING` | Profile time advances and bounded PID demand may be produced. |
| `PAUSED` | Program execution remains loaded but its time/output policy is selected by the unresolved pause decision below. |
| `STOPPING` | New control work is rejected while the current actuator session is invalidated and execution state is closed. |
| `COMPLETED` | Program reached its terminal stage and cannot be resumed. |
| `STOPPED` | Program was explicitly terminated and cannot be resumed. |

### Fault state

Fault state is a typed collection, not a Boolean. At minimum it distinguishes
no active fault, warnings, recoverable inhibition, and latched inhibition.
Detailed classes and clear policy remain a future fault-table decision. The
published snapshot includes active faults, retained warnings, and the most
recent actuator and temperature evidence.

## Single-writer rule

One coordinator execution context serializes program-state transitions. Timer,
HMI, network, temperature, actuator, and fault inputs submit typed messages or
snapshots; they do not mutate coordinator state directly.

STOP and safety inhibition are exceptions only in physical urgency, not in
state ownership: the caller may trigger the bounded all-output-off path while
the coordinator serializes the corresponding process transition. Stale queued
control work carries an execution/session generation and cannot restore output.

## Program ownership

- At most one program is loaded into the coordinator and at most one execution
  exists.
- A separate program manager owns filesystem enumeration, versioned parsing,
  storage, and loading.
- The coordinator accepts only a fully read and validated immutable program.
- `START` never replaces a running or paused execution. It returns an error
  requiring the caller to STOP the current execution first.
- `START` and `RESUME` are distinct commands. A stopped or completed program
  cannot be resumed.

## Command sources

| Source | Start | Stop | Pause | Resume | Status/logs | Service/configuration |
| --- | ---: | ---: | ---: | ---: | ---: | --- |
| Local structured HMI | Yes | Yes | Yes | Yes | Yes | Authorized local workflows |
| Remote network API | No | Yes | Yes | No | Yes | Restricted; future sensitive actions require on-site confirmation |
| Local UART0 technician | Yes | Yes | Yes | Yes | Yes | Broad service access in an explicit local service context |
| Remote service console | No | Yes | Yes | No | Yes | Restricted; no direct fan/heater ON/OFF |

Physical access does not bypass STM32-enforced E-stop, airflow, output-feedback,
current, or factory safety limits. Manufacturing output exercise, if later
needed, is a separately identified service contract and cannot be enabled by a
normal production command.

## Command contract

Every state-changing command includes:

- protocol/interface version;
- source identity and source session;
- command/correlation ID;
- typed payload;
- authorization context;
- bounded admission result;
- eventual applied/rejected result with reason.

Admission means queued for evaluation, not completed. The coordinator validates
the command again when it is applied because state or safety evidence may have
changed after admission.

Duplicate command IDs return the previously recorded outcome where practical;
they do not repeat non-idempotent effects. Telemetry may be coalesced, but STOP,
pause, fault transitions, and final command results cannot be silently dropped.

## Core command semantics

### `START(program)`

Accepted only from a local-authorized source when:

- system availability permits operation;
- no program is `RUNNING`, `PAUSED`, or `STOPPING`;
- the selected program is complete and valid;
- temperature validity/freshness policy is satisfied;
- actuator snapshot reports ready with no output-inhibiting fault;
- a new actuator control session can be established.

Any partial failure leaves the execution non-running and actuator outputs off.

### `STOP`

STOP is idempotent and accepted from every defined command source. It:

1. invalidates the coordinator execution generation;
2. requests the prioritized STM32 global all-output-off operation;
3. closes the active actuator control session;
4. prevents queued/stale control results from being published;
5. moves execution through `STOPPING` to `STOPPED`.

STOP does not promise that a fault or history record is cleared.

### `PAUSE`

Remote and local sources may request pause. The exact product behavior remains
unresolved between:

1. remove heat and freeze profile time; or
2. freeze profile progression while controlling to the current temperature.

Until selected and safety-reviewed, no revision-two implementation may infer
one behavior from the command name. Resume is local-only and must revalidate
temperature, actuator state, faults, and any required airflow before creating
fresh output authority.

### `RESUME`

Accepted from the local HMI or local UART0 service context only when execution
is `PAUSED` and its pause/fault policy explicitly permits resume. It is never an
alias for START and never reconstructs a stopped/completed execution.

### Queries

Queries return a versioned coherent furnace snapshot. Read-only requests do not
change the program, renew an actuator heating lease, or clear faults.

## HMI and network loss

- Loss of HMI does not terminate a healthy running program.
- The coordinator records HMI availability independently of execution state and
  may drive a future status indicator.
- Network connectivity is never a heating prerequisite.
- Neither reconnecting interface receives implicit command authority from its
  pre-disconnect session.

## Published furnace snapshot

The coordinator snapshot should include, at minimum:

- snapshot version and monotonically increasing generation;
- system, execution, and fault dimensions;
- loaded program identity/version and execution progress;
- current phase, setpoint, aggregate temperature, validity, and freshness;
- requested PID duty;
- actuator session/lease state and complete STM32 snapshot identity;
- HMI/network/service availability;
- active warnings/fault references and most recent transition reason;
- firmware, board-configuration, and program-format identities.

## Open decisions

- Pause-off versus pause-and-hold behavior and fan behavior in each case.
- Reset/checkpoint recovery and whether any failure class permits continuation.
- Exact recoverable/latched fault transitions and local clear authorization.
- Cooldown/completion fan behavior.
- Final status-indicator states.

Related documents:
[system architecture](BOARD-REV2-SYSTEM-ARCHITECTURE.md),
[component standard](COMPONENT-CONTRACT-STANDARD.md), and
[actuator protocol](ESP-STM32-ACTUATOR-PROTOCOL-REQUIREMENTS.md).
