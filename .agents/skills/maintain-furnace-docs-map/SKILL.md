---
name: maintain-furnace-docs-map
description: Maintain repository-local Codex context, architecture maps, findings links, and engineering documentation after verified repository changes. Use when modules, paths, interfaces, tasks, state ownership, hardware, commands, or Codex conventions change. Do not use to rewrite history, duplicate source, or edit production behavior.
---

# Maintain Furnace Documentation and Map

## Inputs

Require the verified code/config change, affected source symbols, build/test evidence, and documents claimed stale.

## Workflow

1. Compare current source/config with `REPOSITORY_MAP.md`, `repository-map.yaml`, `AGENTS.md`, findings, templates, and affected skill/agent references.
2. Update only durable navigation, ownership, execution, hardware, workflow, and command facts; keep prose concise.
3. Preserve finding/ADR history and mark supersession/status instead of deleting evidence.
4. Keep Markdown and YAML views consistent and links repository-relative.
5. Run `python3 tools/codex/verify_repository_setup.py`, link checks, and relevant Codex config validation.

## Required output

Report source evidence, docs changed, facts added/removed, compatibility/version notes, validation result, and unresolved map uncertainty.

## Stop and safeguards

Stop if a source claim is unverified or architecture is still undecided. Do not copy large source bodies, create overlapping maps, invent commands, retain placeholders, or change production code. Verification must pass before handoff.
