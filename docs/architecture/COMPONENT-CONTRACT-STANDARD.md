# Revision-two component contract standard

Status: target development standard; not a claim about current components.
Date: 2026-08-04

This standard defines the intended shape of revision-two firmware components.
It favors explicit C types and small contracts over a universal component
framework. Current behavior must be characterized before a component is
migrated to this model.

## Required declaration

Every component must document:

1. Its one primary responsibility.
2. Owned mutable state, tasks, timers, queues, callbacks, buffers, and hardware.
3. Required dependencies and the direction of each dependency.
4. Boot/configuration inputs and immutable factory limits.
5. Public command, response, snapshot, and event types.
6. Execution context and whether each API can block.
7. Queue capacity, timeout, overload behavior, and delivery class.
8. Initialization acquisition order and failure cleanup.
9. Safe state, fault reporting, and physical mitigation responsibility.
10. Host, target, integration, and hardware test seams.

## Component forms

Use the smallest applicable form; do not force every module through one base
class, manager, virtual table, or lifecycle framework.

| Form | Typical examples | State model |
| --- | --- | --- |
| Pure logic | PID, profile interpolation, validation, aggregation policy | Caller-owned context; no task, hardware, or hidden singleton. |
| Device/driver instance | Temperature transmitter, actuator-controller client | One typed context per configured device; transport supplied as a dependency. |
| Active service | Coordinator, Modbus scheduler, configuration/program manager | One explicit owner for task/queue/state; normally one boot-created instance. |
| Transport/UI adapter | HMI, network, UART0 service | Converts external messages to typed furnace commands and snapshots; owns no furnace-domain policy. |

### Instance rule

Device drivers support multiple explicit instances even if a product configures
only one. Instances may come from a statically sized pool created during boot:

```c
temp_transmitter_t transmitters[MAX_CONFIGURED_TRANSMITTERS];

temp_transmitter_read(&transmitters[index], &snapshot);
```

This does not require runtime heap allocation. It prevents hidden globals from
making testing and reuse impossible. System services may remain single-instance
when the product has exactly one authority, but their state is still carried by
an explicit context rather than scattered file-global variables.

## Lifecycle and allocation

- Boot may allocate component contexts and RTOS primitives.
- After boot, normal operation uses bounded queues, fixed-capacity collections,
  and static/preallocated message buffers.
- The configured component/device graph remains fixed until reset.
- Initialization is transactional: acquire in order, record each acquisition,
  and unwind owned resources in reverse order on any failure.
- A component becomes visible to producers only after it is ready.
- Routine hot restart is not required. If shutdown exists, the owner stops new
  producers, unregisters callbacks, requests worker exit, waits for explicit
  acknowledgement, and only then destroys dependencies.
- Delay, `volatile`, task-handle clearing, and eventual timeout are not lifetime
  or synchronization protocols.

## API contract

### Commands

- Commands are typed; no untyped payload pointer may cross a component boundary.
- Each asynchronous command defines source, target, command ID, payload version,
  copy/ownership, admission timeout, result, and cancellation behavior.
- Queue admission means accepted for processing, not completed.
- Completion or rejection includes the command/correlation ID and a reason.
- Command submission is nonblocking or uses a documented short bound. An
  unbounded send is forbidden on a safety/control path.
- The sole queue consumer must never submit to its own bounded queue and block.

### Snapshots

- A component publishes or copies one coherent immutable snapshot of its state.
- Related value, validity, freshness, sequence, timestamp, and fault fields are
  captured together.
- Snapshot readers never inspect a driver's mutable working cache directly.
- A numeric value without explicit validity and freshness is not a valid control
  input.

### Events and delivery classes

| Class | Examples | Required behavior |
| --- | --- | --- |
| Safety action | all-output-off, local inhibit | Direct, idempotent, bounded; never waits behind telemetry. |
| State command | start, stop, pause, resume, clear request | Explicit admission and result; no silent loss. |
| State snapshot | actuator, furnace, sensor batch | Coherent versioned copy; latest-state replacement is allowed when documented. |
| Telemetry | display refresh, periodic diagnostics | Best effort and droppable under bounded overload. |
| Fault/history record | transitions and forensic snapshot | Bounded capture with explicit persistence/drop reporting; never substitutes for mitigation. |

## Ownership rules

- The Modbus master owns UART/RS-485 transactions and scheduling. Device drivers
  do not perform unsynchronized bus access.
- A device registry, if retained, stores identity, configuration, capability,
  and health. It does not own all component behavior or furnace policy.
- The coordinator owns furnace process state and authorization.
- The program manager owns program persistence and validation boundaries.
- The STM32 owns physical actuator/interlock state.
- HMI, network, and service adapters own protocol sessions only; they do not own
  programs, actuator state, or coordinator state.

## Concurrency rules

- Every mutable object has one writer/owner or one complete synchronization
  protocol.
- Queues transfer ownership; task notifications only wake an owner.
- Mutexes protect complete compound state, not selected fields from one logical
  snapshot.
- Callback registration includes an unregistration/quiescence contract.
- Timer callbacks perform bounded work and cannot become the sole queued route
  to a physical off action.
- A component documents behavior when a queue is full, a dependency is stopped,
  a response is late, or shutdown begins during an admitted operation.

## Error and safety contract

- Failure to meet a required dependency contract prevents the component from
  reporting ready.
- Default, partial-initialization, invalid-input, reset, and fatal states are
  fail-off for physical outputs.
- Error reporting and logging are observations, not physical mitigation.
- Components report typed faults upward while retaining local responsibility
  for invariants they alone can enforce.
- A remote clear request never directly clears a physical condition; the owning
  component revalidates it.

## Review checklist

A revision-two component is not ready merely because its API looks uniform. A
review must be able to answer:

- Who owns every mutable field and hardware resource?
- Can any operation block indefinitely or block its only consumer?
- Can a stale command restore authority after STOP, pause, timeout, or reset?
- Are value, validity, freshness, and identity coherent?
- Can partial initialization leave a callback, task, driver, or output live?
- Are all capacities and deadlines compatible with the product configuration?
- Can behavior be tested without energizing hardware, and what still requires
  device-bench or powered-controller evidence?

Related target contracts:
[system architecture](BOARD-REV2-SYSTEM-ARCHITECTURE.md),
[coordinator commands](COORDINATOR-STATE-AND-COMMAND-MODEL.md), and
[actuator protocol](ESP-STM32-ACTUATOR-PROTOCOL-REQUIREMENTS.md).
