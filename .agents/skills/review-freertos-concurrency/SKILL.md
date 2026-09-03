---
name: review-freertos-concurrency
description: Review FreeRTOS tasks, ESP event/timer callbacks, ISRs, shared state, queues, locks, and resource lifetimes in this firmware. Use for concurrency changes, races, deadlocks, timing, startup/shutdown, or cross-task APIs. Do not use for single-context pure logic or as permission to fix findings.
---

# Review FreeRTOS Concurrency

## Inputs

Require subsystem/change, expected concurrency contract, and relevant task priorities/timing/configuration.

## Workflow

1. Inventory every task, callback, timer/ISR context, reader/writer, owner, primitive, and resource lifetime in scope.
2. Trace creation, publication, blocking, cancellation, stop, acknowledgement, join, unsubscribe, and destruction.
3. Inspect queue capacity/lifetime, blocking calls, lock ordering, startup/shutdown races, stale data, volatile/atomic usage, and cross-task API assumptions.
4. Prove races/deadlocks with a concrete interleaving and check existing guards. For safety-relevant concurrency, trace the interleaving through the physical output and require an independent output-inhibit contract; a bounded send that can drop a stop command is not sufficient.
5. Assess ISR-safe API use and actual callback dispatch context, not comments alone.

## Required output

Return an ownership/access table, interleavings, exact evidence, confidence/severity, smallest synchronization/lifecycle direction, test strategy, and uncertainty.

## Stop and safeguards

Stop when all scoped accesses/lifetimes are accounted for or an unresolved context needs runtime evidence. Do not call `volatile` synchronization, use delays as fixes, assume handle clearing is a join, edit code during review, or omit teardown. Update findings/maps when authorized and source-verify every access.
