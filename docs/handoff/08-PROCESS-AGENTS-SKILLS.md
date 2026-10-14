# 08 — How the work was done: timeline, agents, skills and workflow

## 1. Project timeline (from git history)

Contributors: Veselin Trichkov ("vesko"), Kamen Aleksandrov.

| Date | Commit / PR | What happened |
| --- | --- | --- |
| 2025-11-05 → 11-12 | `1ebf5e3` … PR #1 | Logger component; first temperature monitor on **SPI/MAX31865** (PT100 front end on the PCB); SPI master driver; component naming refactor |
| 2025-11-18 → 11-19 | #2 – #6 | Temperature processor split from the monitor; setpoint calculation; coordinator; GPIO master driver; basic PID |
| 2025-12-15 → 12-17 | #7 – #9 | Large component refactor (70 files); health-monitor heartbeats; event-driven heater controller |
| 2026-01-13 | #10 | Unified error types; error-descriptor mechanism (never populated) |
| 2026-03-05 | #11 "Kamen merge" | **Nextion HMI** (39 files), program editor/storage, run indicator, a first `modbus_temp_monitor` |
| 2026-03-20 | #12 | Manual mode |
| 2026-03-21 | #13 "Refactor coms method" | **Switch to Modbus RTU MS9024 transmitters**: `modbus_master`, `device_manager`, `temp_sensor_device`; SPI path left as legacy |
| 2026-03-23 | #14 | Command dispatcher (queue between HMI/coordinator and heater); heater power averaging |
| 2026-03-27 | #15 | Heater, temperature sensor and event fixes |
| 2026-05-17 | #17, #18 | Stall and overshoot handling in the coordinator; crash-log storage (LittleFS, RTC snapshot) |
| 2026-05 → 2026-07 | branch `field-tests`, PR #19 `hardening/field-fixes` | Field commissioning on 15 kW/25 kW units: multi-sensor support, transmitter diagnostics, MS9024 register investigation, fan controller, by-value command payloads, runtime remaining-time estimate, HMI overlays, PID tuning rounds 1–4, ramp soft-landing, hold-deviation guard, feedforward. Then **Codex repository setup and a hardening pass**: 31 `fix:` commits and 11 `docs:` commits for findings F-001 … F-056, ADR-0001/0002, bug and validation reports |
| 2026-10-13 | #20, #21 "Field tests" | Squash-merge of all of the above to `main` (#21 has an identical tree) |
| 2026-10-13 | (this audit) | Gap analysis, documentation correction, rewrite handoff package |

Big lessons from the timeline:

- The sensing architecture changed twice (SPI → first Modbus monitor → device-manager model) before field use.
- The HMI grew to about 5 500 lines and absorbed domain logic (F-076).
- Field tuning happened **before** any automated test existed.
- The hardening pass produced documentation, ADRs and narrow source fixes, but **no executable tests** and **no hardware validation**.

## 2. The Codex repository setup (what it is)

Configured for OpenAI Codex discovery; layout described in [../codex/CODEX_LAYOUT.md](../codex/CODEX_LAYOUT.md). Verified by `python3 tools/codex/verify_repository_setup.py`, which checks required files, the exact agent/skill sets, skill front-matter and Markdown links.

### 2.1 Instructions

- [../../AGENTS.md](../../AGENTS.md) (root): read the map, standards and findings first. It also sets the current phase (harden, do not refactor), routes work to skills, and lists the non-negotiable safety rules:
  - trace every change to the physical output;
  - validity plus freshness before temperature is used for control;
  - fail-off defaults;
  - no `volatile` or delays as synchronization;
  - single owner and lifetime protocol;
  - never block a sole consumer on its own queue;
  - no flashing or energizing without permission;
  - "confirmed" needs a reachable trigger plus exact evidence.
- [../../components/AGENTS.md](../../components/AGENTS.md): public/private layout, explicit source lists (not followed, see 06 §C.5), checked creation and unwind, documented concurrency contracts, specialist review for safety-relevant changes, map updates.
- [../codex/ENGINEERING_STANDARDS.md](../codex/ENGINEERING_STANDARDS.md): ownership, messaging, six safety invariants, error and evidence rules.

### 2.2 Agents (`.codex/agents/*.toml`, 11)

| Agent | Mode | Role | Skill(s) |
| --- | --- | --- | --- |
| repository-analyst | read-only | Discovery, path tracing, impact | orient, analyze-impact |
| firmware-architect | read-only | Ownership/boundaries, ADR drafts | analyze-impact, decide-architecture |
| bug-investigator | read-only | Evidence and root cause | investigate-bug |
| bug-fixer | workspace-write | Narrow verified fix per bug ID | fix-defect |
| feature-implementer | workspace-write | Bounded features in the current architecture | implement-feature |
| test-engineer | workspace-write | Regression tests and validation plans | create-regression-test, plan-hardware-validation |
| refactoring-agent | workspace-write | Later behavior-preserving refactor, one boundary at a time | refactor-behavior-preserving |
| safety-reviewer | read-only | Every energize/de-energize path | review-safety |
| concurrency-reviewer | read-only | Tasks, callbacks, queues, lifetimes | review-freertos-concurrency |
| control-system-reviewer | read-only | PID, profile, SSR window | review-pid-profiles |
| modbus-device-reviewer | read-only | Modbus transport, device lifecycle, validity | review-modbus-devices |

`.codex/config.toml`: `max_threads = 4`, `max_depth = 1`. No model pinning, hooks, MCP servers or network grants.

### 2.3 Skills (`.agents/skills/*/SKILL.md`, 15; each has an `agents/openai.yaml` UI descriptor)

Every skill has the same skeleton: **Inputs → Workflow → Required output → Stop and safeguards**. That skeleton is the part worth copying.

| Skill | Purpose | Key safeguard |
| --- | --- | --- |
| orient-furnace-repository | Start-of-task orientation from the maps plus targeted source checks | Never a substitute for source verification |
| analyze-firmware-impact | Producer/consumer, ownership, interface and test impact | Stops before choosing or implementing |
| decide-furnace-architecture | Compare 2–4 options, write an ADR from the template | Rejects timing luck, `volatile`, unbounded safety queues, multiple owners |
| investigate-furnace-bug | Reproduce or establish a reachable trigger; classify; "ready-to-fix" gate | No production edits; no "confirmed" from suspicion |
| fix-furnace-defect | Smallest root fix for a ready-to-fix bug ID | Revalidate first; no delay/volatile fixes; no hardware claims from mocks |
| implement-furnace-feature | Bounded feature with acceptance criteria | Stops on unclear safety behavior or missing hardware authority |
| create-firmware-regression-test | Lowest test layer that reproduces the mechanism | Test-level classification; no tautological mocks |
| plan-hardware-validation | Bench, powered-controller and powered-furnace plans | Never executes; independent cutoff and supervision required |
| review-furnace-safety | Energize/de-energize inventory, GPIO ownership table | Unproven output-off path is a release blocker |
| review-freertos-concurrency | Ownership/access tables, interleavings | Handle clearing is not a join |
| review-pid-profiles | Units, timing, transitions, safety gate | No tuning from static inspection |
| review-modbus-devices | Transport, conversion, freshness | Never treat a cached value as valid; no register writes during review |
| review-furnace-release | Change validation, release verdict | Separates executed, inspected, simulated, planned and unverified evidence |
| refactor-furnace-behavior-preserving | Later phase; characterization first | Stabilization gate per boundary |
| maintain-furnace-docs-map | Keep the maps current after verified changes | Verification must pass |

### 2.4 Artifacts and templates

- `docs/templates/`: bug report, feature proposal, ADR, change-validation report.
- `docs/analysis/`: findings register (F-IDs, categories, evidence, trigger/impact, correction, residual risk), per-bug reports (BUG-001, 017, 053–056), change-validation reports (F001 A/B, F002, F17, F49, F50, F53–F56), hardware validation plans (F17, F001/F054, never executed), current fix status, future work items WI-001…005.
- `docs/decisions/`: ADR-0001 (defer central fault manager; local actuator inhibit now), ADR-0002 (separate permanent/recoverable/temporary inhibits; direct enforcement; coordinator-owned recovery).

### 2.5 Evidence taxonomy (keep it)

Host unit → target unit → mocked integration → device bench → powered controller → powered furnace. Each claim names its level. Compilation and mocks never prove physical behavior.

## 3. Claude Code assistance during field tuning

Separate from Codex, Claude Code sessions supported the June 2026 field-tuning work: the PID/feedforward/soft-landing rounds and the guard re-parameterisation in [03-CONTROL-ALGORITHMS.md](03-CONTROL-ALGORITHMS.md) §7. Their notes are personal session memory, not repository files; the durable content has been folded into this package. One claim in those notes was wrong (06 §C.10).

## 4. What worked, what did not

| Worked | Did not work |
| --- | --- |
| Narrow, ID-tracked findings with explicit trigger, impact and residual risk | Zero executable tests; every fix ended "validation pending" |
| ADRs for safety-relevant boundaries (inhibit reasons, deferring the fault manager) | The docs drifted from the code within the same pass (F-060, YAML map) |
| Read-only reviewer roles separated from writer roles | The config baseline was never compared against the field build (F-067) |
| Separating "confirmed" from "possible" | Commit squashing lost per-change history |
| Field logging of P/I/D/FF per tick made tuning possible | High log volume on real-time paths (F-074) |
| Explicit stop conditions in every skill | Rules written but not enforced by tooling (source globs, format warnings, map consistency) |

## 5. What to carry into the new project

1. Keep the **instruction hierarchy** (root plus per-directory `AGENTS.md`/`CLAUDE.md`) and the **non-negotiable safety rules** verbatim, adapted to Zephyr terms (threads, `k_msgq`, zbus, `k_timer`/`k_work`).
2. Keep the **skill skeleton** and the reviewer/writer split. Add a **TDD skill** that requires a failing test before production code, and a **requirements-trace skill** that maps each REQ-ID in [02](02-FUNCTIONAL-REQUIREMENTS.md) to tests.
3. Make the verification script **enforce** rules instead of only listing them:
   - map ↔ source consistency (task and channel inventory generated from code);
   - config variant ↔ build;
   - warnings-as-errors;
   - no unbounded waits on listed safety paths (grep/static check).
4. Keep the findings register format (F-IDs, categories, evidence, trigger, residual risk). Start the new project at its own ID space and link back to these F-IDs for provenance.
5. Keep the evidence taxonomy and the hardware-plan template. Treat "source fix implemented; validation pending" as **not done**.
6. Never squash-merge safety work; keep one change per commit with its finding ID.
7. Copy the **decisions and deferred-topics register** ([10](10-DECISIONS-AND-DEFERRED-TOPICS.md)) and its **remind-and-ask rule**. In this repository the rule is in `AGENTS.md`, the entry skills (orient, analyze-impact, decide-architecture, implement-feature, fix-defect, create-regression-test, maintain-docs-map), the review skills, and every agent definition. `verify_repository_setup.py` fails if any of these drops the reference.
