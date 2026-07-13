# Current-model engineering standards

These rules harden the existing architecture. Broad component reorganization and messaging replacement belong to a later refactor unless required to restore a safety invariant.

## Scope and ownership

- Make one behavior or invariant the unit of change; avoid opportunistic renames, moves, formatting, and cleanup.
- Give each mutable state, task, queue, timer, driver, and heap object one owner.
- Workers acknowledge their own exit; owners stop producers, unregister callbacks, join workers, then destroy dependencies in reverse initialization order.
- Preserve public APIs, HMI contracts, Kconfig behavior, and persisted formats unless migration is explicit.

## Concurrency and messaging

- Use queues for ownership transfer, notifications for wakeups, mutexes for compound state, and atomics only with a complete memory-order protocol.
- Never use `volatile`, a delay, or timing luck as synchronization.
- Never block the sole consumer by enqueueing to its own bounded queue.
- Define producer, consumer, execution context, payload type/size, copy/ownership, timeout, failure, and shutdown behavior together.
- Separate best-effort telemetry from reliable commands, state transitions, and safety faults.

## Safety invariants

1. Boot, reset, partial initialization, shutdown, invalid input, lost communication, and internal failure must leave contactor and SSR de-energized.
2. A direct idempotent output inhibit must not depend on a congested telemetry/control queue.
3. Heating requires explicit program authorization plus fresh, valid temperature input; cached numeric values alone are insufficient.
4. Pause, stop, cancel, completion, overtemperature, and loss of sensor quorum must prevent stale control work from restoring power.
5. Actuator policy is rechecked at the physical-output boundary; reporting an error never substitutes for de-energization.
6. Fan/airflow assumptions and external interlocks must be documented and validated at the appropriate hardware test level.

## Errors and evidence

- Propagate failures when a component cannot meet its contract; preserve the first error while reporting cleanup failures.
- Log actionable state transitions without flooding timing-sensitive loops.
- A finding needs a reachable trigger/interleaving, exact current-source evidence, impact, classification, and uncertainty.
- Run focused tests before a full build. Distinguish host, target, mocked integration, device bench, powered-controller, and powered-furnace evidence.
- Compilation is not physical safety validation.
