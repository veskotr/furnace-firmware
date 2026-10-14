---
name: refactor-furnace-behavior-preserving
description: Refactor one stabilized firmware boundary while preserving externally visible behavior. Use only after current behavior is hardened, characterized, and refactoring is explicitly authorized. Do not use for bug fixes, safety behavior changes, feature work, broad cleanup, or pre-stabilization restructuring.
---

# Refactor Furnace Behavior-Preserving

## Inputs

Require explicit authorization, boundary, characterization tests, preserved behavior list, rollback point, and stabilized findings: every finding touching the boundary is verified fixed, accepted-risk, or explicitly deferred with owner/evidence, with no open critical/high safety finding on that path. Require an accepted ADR when changing ownership, dependency direction, task/synchronization model, public API/message contract, persistence format, or hardware-control boundary.

## Workflow

1. Independently recheck `docs/analysis/FIRMWARE_FINDINGS.md` and current source to prove the boundary meets the stabilization gate; then inventory public APIs, events/commands, timing, state, configuration, persisted formats, logs relied upon, and hardware behavior at that boundary.
2. Run characterization tests before editing and record baseline results.
3. Define a reversible migration that changes one dependency/ownership boundary without changing behavior or safety policy.
4. Implement in small reviewable steps; do not mix defect/safety corrections with structure.
5. Re-run characterization, focused specialist review, full build, and appropriate validation after each step.
6. Update ADRs and both repository maps; prepare separate reversible commits only when the user explicitly authorizes commits.

## Required output

Report preserved contract, baseline/after evidence, boundary changed, commits/rollback, architecture map/ADR updates, unverified hardware behavior, and residual risk.

## Stop and safeguards

Stop if characterization is absent/failing, behavior must change, a safety defect appears, or rollback is unclear. Route behavior changes to bug/feature workflows. Never combine boundaries, rename widely for aesthetics, alter defaults/formats/timing silently, or claim equivalence from build alone.
