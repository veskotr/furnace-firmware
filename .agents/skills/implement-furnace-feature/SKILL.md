---
name: implement-furnace-feature
description: Implement a bounded feature in the current ESP-IDF/FreeRTOS furnace architecture. Use after a feature objective is authorized and when behavior, commands, configuration, HMI, devices, diagnostics, or controls must be added before the later refactor. Do not use for diagnosis-only work, unrelated architecture redesign, or behavior-preserving refactoring.
---

# Implement Furnace Feature

## Inputs

Require an objective, scope/non-goals, acceptance criteria, and hardware permissions. Create `docs/templates/feature-proposal.md` when requirements need a durable proposal.

## Workflow

1. Use `$analyze-firmware-impact`; identify known findings, safety/timing effects, owners, and asynchronous contracts.
2. Define acceptance and failure behavior, state changes, interfaces, rollout, and fallback.
3. Follow existing boundaries unless an authorized ADR changes them; resolve lasting tradeoffs with `$decide-furnace-architecture`.
4. Implement the smallest coherent feature, checking creation/error/cleanup paths and preserving safe defaults.
5. Use `$create-firmware-regression-test`; otherwise produce `$plan-hardware-validation` evidence appropriate to the test level.
6. Run focused checks and `$review-furnace-release`; update maps/docs when behavior or architecture changed.

## Required output

Deliver behavior, files/symbols changed, acceptance results, build/tests, timing/safety review, unverified hardware behavior, and documentation updates. If blocked, provide prerequisite finding IDs, violated invariant, ready/not-ready status, exact missing authority/evidence, required next workflow, and deterministic resume conditions.

## Stop and safeguards

Stop on unclear safety behavior, missing hardware authority, an unresolved architecture decision, or a confirmed/high-confidence prerequisite that violates a safety invariant on the feature path. Route that prerequisite to `$investigate-furnace-bug` or `$decide-furnace-architecture` and name the resume package. When inputs are incomplete, draft safe non-authoritative acceptance criteria where possible and stop only when the missing choice changes scope, safety, or behavior. Do not mix cleanup/refactoring, silently alter defaults/pins/PID constants/formats, claim mock evidence as hardware evidence, or omit failure-path verification.
