# Furnace firmware repository instructions

## Read first

Use `$orient-furnace-repository` and read `docs/codex/REPOSITORY_MAP.md`, `docs/codex/ENGINEERING_STANDARDS.md`, and `docs/analysis/FIRMWARE_FINDINGS.md` before reviewing or changing production firmware. The source and actual command output remain authoritative.

## Current phase

Harden and standardize the current ESP-IDF/FreeRTOS architecture. Do not perform broad reorganization, module renames, framework replacement, or behavior-changing cleanup unless explicitly authorized. Behavior-preserving refactoring is a later phase and requires characterization coverage.

## Workflow routing

- Discovery/impact: `$orient-furnace-repository`, `$analyze-firmware-impact`
- Feature: `$implement-furnace-feature`
- Architecture/ADR: `$decide-furnace-architecture`
- Diagnosis then fix: `$investigate-furnace-bug`, then `$fix-furnace-defect`
- Specialist reviews: `$review-furnace-safety`, `$review-freertos-concurrency`, `$review-modbus-devices`, `$review-pid-profiles`
- Evidence: `$create-firmware-regression-test`, `$plan-hardware-validation`, `$review-furnace-release`
- Later refactor: `$refactor-furnace-behavior-preserving`
- Map/docs upkeep: `$maintain-furnace-docs-map`

## Non-negotiable rules

- This firmware can energize a furnace. Trace every safety-relevant change through the physical heater/SSR/contactor output and every de-energization path.
- Treat temperature as usable for control only when validity and freshness are explicit. Numeric plausibility is not freshness.
- Default and failure states must de-energize outputs. Error reporting is not physical mitigation.
- Do not use `volatile` or delays as synchronization. Define one owner and a lifetime protocol for every task, queue, timer, callback, driver, and mutable state.
- Do not block a sole consumer by submitting to its own bounded queue. Do not destroy resources until producers/callbacks stop and workers acknowledge exit.
- Preserve unrelated worktree changes. Do not flash or energize hardware without explicit user permission.
- Do not call a defect confirmed without a reachable trigger and exact source evidence. Do not claim hardware validation from compilation or mocks.

## Repository-local Codex setup

Codex 0.144.3 discovers agents in `.codex/agents/*.toml`, skills in `.agents/skills/*/SKILL.md`, and nested `AGENTS.md` instructions from repository root toward the working directory. See `docs/codex/CODEX_LAYOUT.md`. Run `python3 tools/codex/verify_repository_setup.py` after changing this setup.
