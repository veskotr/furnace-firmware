# ADR-0001: Defer centralized fault-state handling until after hardening

- **Status:** proposed
- **Date:** 2026-07-13

## Context

The firmware has `FURNACE_ERROR_EVENT` notifications but no centralized owner for machine fault state, acknowledgement, recovery, or HMI fault presentation. F-017 requires immediate output protection after an SSR GPIO failure. Adding a global fault manager during the current hardening pass would expand the change across coordinator, HMI, event contracts, startup order, and recovery behavior.

## Problem

The system needs a future, coherent way to record faults, set machine states, present them to the HMI, and define acknowledgement/recovery. Those needs must not delay or replace local actuator fail-off behavior.

## Constraints

- Safety actions must not depend on queues, event delivery, the HMI, or a future manager.
- The current hardening pass must preserve field-control behavior and avoid unrelated architecture changes.
- Existing `FURNACE_ERROR_EVENT` remains the notification mechanism until an approved consumer contract exists.
- HMI fault handling is not ready to be wired in this pass.

## Considered options

1. Add a global fault manager now, owning fault state and recovery.
2. Keep events only and add no local actuator interlock.
3. Add local actuator interlocks now; retain events for monitoring; design centralized fault ownership after hardening.

## Decision

Choose option 3. The heater component owns immediate output inhibition for its local GPIO failures and posts the existing event after physical-off attempts. No new global fault manager, coordinator fault gate, HMI acknowledgement, or automatic recovery is added in this hardening batch.

## Rationale

This fixes the source-confirmed F-017 failure at the actuator boundary with the smallest coherent scope. It avoids making output safety depend on new, uncharacterized event/state infrastructure while preserving the event stream needed by a later HMI and supervisory design.

## Consequences

- A local inhibit is intentionally component-scoped and cleared only on controller restart in the first implementation.
- Existing events remain observability only; they do not prove mitigation or establish machine-wide state.
- Future work must specify fault classes, state ownership, transition authority, persistence/history, HMI display, acknowledgement, recovery preconditions, and event compatibility.

## Migration plan

1. Implement and characterize local heater inhibit for F-017.
2. Preserve/document the emitted event and its source/code/severity semantics.
3. After hardening, create a separate ADR for global fault states and recovery policy.
4. Add a manager/HMI subscriber only with an explicit event payload/version and startup/lifecycle contract.

## Rollback strategy

The local hardening change can be reverted independently if it causes a verified integration failure, restoring the previous event-only behavior. Do not roll back a released actuator-safety change without an equivalent tested protection mechanism.

## Safety implications

Local physical-off attempts precede telemetry. A successful build or event delivery does not prove electrical isolation; powered-controller validation is required before release.

## Test implications

Regression tests must inject SSR GPIO failure and prove inhibit/demand-zero/direct-contactor-off occur before event publication and that later heater-on commands are rejected. Future fault-manager work needs state-transition, event-ordering, recovery, and HMI integration tests.
