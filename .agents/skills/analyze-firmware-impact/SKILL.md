---
name: analyze-firmware-impact
description: Analyze architectural and behavioral impact before changing this furnace firmware. Use for cross-component changes, interface or state changes, ownership/lifecycle questions, feature proposals, and refactor scoping. Do not use to choose and record a lasting decision, diagnose one concrete bug, or implement code.
---

# Analyze Firmware Impact

## Inputs

Require a proposed behavior/change, scope boundaries, and any compatibility or safety constraints.

## Workflow

1. Orient with the repository map and inspect the actual public APIs and call sites.
2. Trace producers/consumers through tasks, callbacks, queues/events, shared state, drivers, persistence, and physical outputs.
3. List affected modules, symbols, ownership, interfaces, state, initialization/shutdown, timing, configuration, stored formats, and tests.
4. Identify active findings, safety invariants, dependency-direction changes, and rollout/rollback needs.
5. Separate required impact from speculative redesign; escalate real tradeoffs to `$decide-furnace-architecture`.

## Required output

Produce an evidence table of affected paths, risk/compatibility analysis, minimal boundary, acceptance/verification needs, documentation updates, and unresolved questions.

## Stop and safeguards

Stop before implementation or architecture selection. Do not assume directory ownership, skip indirect callers, or recommend broad cleanup. Update maps only after an architecture change is accepted and implemented. Verify every claimed impact against exact current symbols.
