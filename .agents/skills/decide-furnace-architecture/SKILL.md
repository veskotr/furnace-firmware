---
name: decide-furnace-architecture
description: Compare and record meaningful architectural decisions for this furnace firmware. Use for ownership, module boundaries, dependency direction, task/synchronization models, messaging contracts, persistence formats, migrations, or hardening-versus-refactor choices. Do not use for local implementation details with no lasting tradeoff or before impact evidence exists.
---

# Decide Furnace Architecture

## Inputs

Require one decision question, impact evidence, constraints, safety invariants, compatibility needs, and decision authority.

## Workflow

1. Read maps/standards and verify source-level ownership and runtime behavior.
2. Compare two to four credible options on safety, determinism, lifecycle, coupling, testability, migration, rollback, stored-data compatibility, and hardware validation cost.
3. Reject timing luck, `volatile` synchronization, unbounded safety queues, multiple owners, or teardown without acknowledgement.
4. Choose one option or mark the decision provisional with the experiment needed.
5. When authorized, copy `docs/templates/architectural-decision-record.md` to `docs/decisions/NNNN-short-title.md` and complete every section.

## Required output

Provide context, options, decision, rationale, consequences, invariants, migration, rollback, safety/test implications, and uncertainties.

## Stop and safeguards

Stop without a decision when evidence or authority is missing. Do not redesign unrelated systems, confuse preference with constraint, or implement the migration in the ADR task. Update repository maps when the accepted decision changes architecture; verify the ADR against current source and internal links.
