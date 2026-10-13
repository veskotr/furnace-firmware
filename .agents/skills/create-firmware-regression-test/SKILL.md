---
name: create-firmware-regression-test
description: Design and implement focused regression coverage for a firmware bug, feature, or preserved behavior. Use when a deterministic oracle can be tested on host, ESP target, or a mocked integration boundary. Do not use to claim physical hardware behavior or when the only meaningful evidence requires an approved hardware plan.
---

# Create Firmware Regression Test

## Inputs

Require bug/feature ID, failure-before and pass-after behavior, affected interfaces, timing/fault conditions, and available test environment.

## Workflow

1. Classify the test: host unit, target unit, mocked integration, device bench, powered-controller, or powered-furnace.
2. Choose the lowest layer that preserves the defect/acceptance mechanism; define inputs, oracle, timing tolerance, and cleanup.
3. Add seams/mocks only when they do not alter production behavior or obscure ownership.
4. Implement deterministic normal, boundary, and failure cases; include race scheduling/fault injection where applicable.
5. Run the exact test, capture result, and state what it does not prove.

## Required output

Report classification, oracle, files changed, command/result, coverage boundaries, flake controls, remaining hardware validation, and documentation updates.

## Stop and safeguards

Stop when the proposed layer cannot reproduce the mechanism; route to `$plan-hardware-validation`. Do not add tautological mocks, sleeps as synchronization, tests of implementation details only, new dependencies without need, or claim hardware proof. Update the bug/feature and validation report after execution.
