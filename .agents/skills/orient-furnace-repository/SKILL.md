---
name: orient-furnace-repository
description: Quickly orient work in this ESP-IDF/FreeRTOS furnace repository using its durable maps and targeted source checks. Use at the start of repository discovery, broad questions, unfamiliar subsystem work, or before selecting a specialist workflow. Do not use as a substitute for source verification or a focused defect, safety, concurrency, control, or Modbus review.
---

# Orient Furnace Repository

## Inputs

Obtain the user's objective, intended subsystem if known, and whether the task is read-only or permits edits.

## Workflow

1. Read `AGENTS.md`, `docs/codex/REPOSITORY_MAP.md`, and the relevant nodes in `docs/codex/repository-map.yaml`.
2. Locate the entry point, public interface, owner, execution contexts, hardware, and upstream/downstream modules for the objective.
3. Verify map claims with narrow `rg` searches and current source; do not crawl unrelated components.
4. Check `docs/analysis/FIRMWARE_FINDINGS.md` for active findings on the path.
5. Route to the smallest applicable specialist skill or agent.

## Required output

Report relevant files and symbols, end-to-end path, execution/hardware context, known findings, uncertainties, and recommended workflow.

## Stop and safeguards

Stop when the requested area is located and source-verified. Do not edit production code, infer behavior from folder names, treat historical reports as proof, or expand into a broad audit. If the map is stale, record the exact correction and use `$maintain-furnace-docs-map`; verification is source cross-check plus repository setup verification after documentation edits.
