---
name: fix-furnace-defect
description: Apply a safe narrow correction for a specifically investigated furnace firmware bug. Use only with a bug ID, evidence, violated invariant, and correction authority. Do not use for diagnosis-only requests, unconfirmed findings, unrelated cleanup, feature work, or architectural refactoring.
---

# Fix Furnace Defect

## Inputs

Require a bug ID/report explicitly marked `ready-to-fix`, current trigger evidence, violated invariant, minimal root-fix boundary, allowed scope and correction authority, ADR status when required, fail-before software oracle, and hardware permission/test level.

## Workflow

1. Revalidate that the bug still exists in current source and reproduce/fail the regression oracle where practical.
2. Trace affected ownership, concurrency, lifecycle, and physical-output consequences again before editing.
3. Implement the smallest coherent root fix; preserve current boundaries and fail-safe behavior.
4. Check adjacent instances only when they violate the identical invariant.
5. Use `$create-firmware-regression-test`; document `$plan-hardware-validation` without claiming it ran.
6. Run focused checks, full production build, specialist reviews, and `$review-furnace-release` in proportion to risk.
7. Update bug status and affected maps/docs only after evidence passes.

## Required output

Report bug ID, root fix, exact diff scope, regression/build results, hardware validation status, safety invariants, residual risks, and documentation updates.

## Stop and safeguards

Stop if the defect disappeared, evidence conflicts, status is not `ready-to-fix`, scope/authority is missing, architecture must change without an ADR, or no software oracle can verify the mechanism. Pending powered-hardware validation can permit code handoff only when explicitly recorded; it blocks release whenever the safety claim depends on it. Never fix by delay/volatile, broaden into refactoring, alter unrelated defaults, delete finding history, or claim hardware validation from mocks.
