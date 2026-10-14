---
name: review-furnace-release
description: Perform change-validation and release-readiness review for furnace firmware and repository tooling. Use before handing off or releasing a bug fix, feature, hardening batch, configuration change, or migration. Do not use as a substitute for specialist review or to claim unperformed hardware validation.
---

# Review Furnace Release

## Inputs

Require intended change/acceptance contract, actual diff, risk classification, required test levels, and known findings/dependencies.

## Workflow

1. Confirm scope and inspect the actual diff, including generated/config changes and unrelated worktree state.
2. Recheck interfaces, ownership/lifecycle, error paths, safety invariants, timing/units/bounds, persistence compatibility, and required specialist reviews.
3. Run the narrowest tests, static checks, repository verification, and full ESP-IDF build appropriate to the change.
4. Confirm maps/docs/findings/ADR and rollback/rollout are current.
5. Complete `docs/templates/change-validation-report.md`, separating executed, inspected, simulated, planned, and unverified evidence.

## Required output

Give release verdict/blockers, files/behavior changed, exact commands/results, static analysis, assumptions, unverified hardware behavior, manual steps, remaining risks, and rollback.

## Stop and safeguards

Stop with a blocker on failing tests/build, unresolved safety output path, missing required evidence, undocumented migration, or scope contamination. Do not waive failures silently, flash/energize without permission, call mocks hardware tests, or claim commands ran when they did not.
