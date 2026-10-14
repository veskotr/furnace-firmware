# 01 — Rules for the new project

These are normative. The new repository's instruction files must carry them, adapted to its real paths and tools. Each rule names the legacy lesson behind it; the full story is in [07-LESSONS-LEARNED.md](07-LESSONS-LEARNED.md).

## 1. Safety non-negotiables

1. This firmware can energize a furnace. Trace every safety-relevant change through the physical heater outputs (SSR and contactor) and through **every** de-energization path.
2. Boot, reset, partial initialization, shutdown, invalid input, lost communication and internal failure leave SSR and contactor **de-energized**.
3. Heating requires explicit run authorization **and** fresh, valid temperature. A numeric value is not proof of freshness or validity.
4. A direct, idempotent output inhibit never depends on a queue, an event bus, a logger or the display link.
5. Once an inhibit has returned, nothing may re-energize the heater until the inhibit is released by its owner. This is decided as unacceptable to violate (D-08); prove it with a test.
6. Pause, stop, completion, abort, over-temperature and loss of sensor quorum must prevent stale control work from restoring power.
7. Reporting an error never substitutes for de-energizing.
8. Default and failure states de-energize outputs.
9. Do not flash, power or energize hardware without explicit permission from the product owner.
10. Do not call a defect confirmed without a reachable trigger and exact evidence. Do not claim hardware behavior from compilation, simulation or mocks.

## 2. Remind and ask (deferred topics)

The decisions register lists decided items (D-xx) and deliberately postponed topics (T-xx) with trigger areas. The starter copy is [02](02-DECISIONS-AND-DEFERRED-TOPICS.md); the living copy is created in the new repository.

- Before planning, designing, reviewing, testing or changing anything, check whether the work touches a trigger area of a `deferred` T-entry.
- If it does: stop before choosing a solution in that area, remind the product owner of the topic and the constraints already decided, and ask the listed questions. Options and a recommendation are welcome; a decision is not.
- A missing answer is not permission. Work outside the deferred area may continue, marked provisional.
- Record each answer as a dated D-entry, mark the T-entry `resolved → D-xx`, and update affected documents and tests.
- The repository verification script must fail if the root instructions or the entry skills stop referencing the register.

## 3. Integration pending: build against the unknown, never invent it

Many external facts are not yet known:

- MCU, board and schematic: pins, polarity, peripherals, flash layout;
- the display (HMI) protocol and its commands;
- the temperature sensors and their bus;
- log and crash-record formats and storage;
- the cure-program format and import;
- configuration values: PID per furnace, thresholds, limits, precision;
- the time source.

Until the product owner supplies them, the code is written so that it does not need them.

1. **Define the interface the domain needs**, not the one you guess the hardware has: for example `sensor_source`, `actuator`, `display_link`, `program_store`, `settings_store`, `clock`, `crash_log`. Domain logic depends only on these interfaces.
2. **Provide a fake** for tests. The fake belongs to the test tree.
3. **Where a real implementation would need the unknown fact, stop and mark it** with exactly this comment form, referencing the deferred topic:

   ```c
   /* TODO(INTEGRATION T-02): display protocol command set not defined yet.
    * Needed: command IDs, payload layout, acknowledgement rules.
    * Until then: this handler is not wired and returns -ENOTSUP. */
   ```

   - `TODO(INTEGRATION T-xx): <what is unknown>`, followed by what is needed and what the stub does.
   - This is the **only** allowed TODO form in production code. Plain `TODO`/`FIXME` are rejected by the verification script.
4. **Stubs fail safe and loudly.** They return an explicit "not integrated" error (`-ENOTSUP`) and never fabricate data. Callers treat that as "unavailable": no heating, no fake temperature, no silent success. A stub on a heater-output path keeps outputs off.
5. **No invented values.** Do not invent pin numbers, register addresses, protocol command codes, sensor scaling, thresholds or tuning constants. Name unknown configuration values `*_PLACEHOLDER` (for example `CONFIG_FURNACE_ABS_LIMIT_C_PLACEHOLDER`). Placeholders are allowed in test and simulation builds and rejected by the production-build check.
6. **Track them automatically.** A script lists every `TODO(INTEGRATION …)` marker and `*_PLACEHOLDER` symbol with its T-ID. CI publishes the list. A release is blocked while any marker remains on an enabled production path.
7. When work reaches a marked area, the remind-and-ask rule applies: ask for the missing information; do not fill it in.

## 4. Test-first

1. No production code without a failing test that demands it. The test names the requirement (REQ-ID) or defect it covers.
2. Use the lowest layer that exercises the mechanism:
   - host unit;
   - `native_sim` integration with fakes and emulators;
   - plant-model simulation;
   - target unit;
   - device bench;
   - powered controller;
   - powered furnace.
3. Every claim states its evidence level. Mocks and simulation never prove physical behavior.
4. Do not encode an answer to a deferred topic in a test oracle. Ask, or mark the test provisional.
5. Characterize field-validated behavior (for example the tuned ramp→hold arrival, [04](04-CONTROL-ALGORITHMS.md) §7) with regression tests before changing it.

## 5. Architecture rules (learned from the legacy firmware)

| # | Rule | Legacy lesson |
| --- | --- | --- |
| R1 | The control/domain core is pure C with no RTOS calls: program, validation, profile, ramp planner, PID, feedforward, guards, sensor aggregation, run state. Instances, not singletons. Tested on host first. | The core was untestable; PID was a file-static singleton |
| R2 | One owner thread for run state (an explicit state machine). Everything else sends it messages; no shared plain flags. | Run flags were written from four execution contexts |
| R3 | The actuator owns SSR and contactor. Authorization and the GPIO write are one critical section; every inhibit change wakes the actuator; demand is accepted only with the current run-epoch token. | Check-then-act race re-asserted the SSR after an inhibit; stale PID output after pause |
| R4 | Safety paths do GPIO and atomics only: no logging before the action, no queue waits, no event publishing. | The inhibit path logged first, then blocked on an unbounded event post before opening the contactor |
| R5 | Telemetry is non-blocking and droppable. Commands and state transitions use a reliable channel with defined overflow behavior. Never publish into a channel from its own consumer; do not publish what nobody consumes. | The event loop posted into its own full queue; unsubscribed events were posted with infinite wait |
| R6 | Temperature is one record (value, monotonic timestamp, status, contributing sensors) published atomically; validity and freshness travel with the value to every consumer, including the display. | Value and timestamp were shared unsynchronized; the display showed a frozen value after sensor loss |
| R7 | An always-on supervisor enforces an absolute temperature limit and sensor-freshness loss in every state (idle, paused, running) and opens the contactor directly. Limit values are deferred (T-10). | Over-temperature was only checked relative to the moving setpoint, only while running |
| R8 | Terminal outcomes are distinct (COMPLETED, STOPPED, ABORTED with cause); every fault reaches the operator with a cause. A fault manager owns latching and acknowledgement. Presentation is deferred (T-01). | An over-temperature abort was reported as "completed"; error events had no subscriber |
| R9 | Stage kind is explicit in the program (RAMP / HOLD / COOL); "already at target" is handled deliberately. Manual mode is its own mode and holds until stopped (D-09). | Stage kind was inferred from the measured temperature; manual mode switched the heater off on arrival |
| R10 | One ramp planner for every entry path; its rate bound is a tested property. | The manual retarget bypassed the planner and ramped up to 2× too fast |
| R11 | Every thread is registered with the task watchdog from day one; the watchdog's consequence is outputs off, then reset. | Health monitoring was disabled; the system watchdog only watched idle tasks |
| R12 | Static threads that never stop, unless a lifecycle is required and tested. | Many shutdown/join defects in runtime teardown nobody needed |
| R13 | One tracked configuration per board/furnace variant; configuration identity printed at boot and stored in crash records; devicetree encodes pins **and** polarity; a build-time check rejects pin collisions. | The tracked config was not the field build; a test indicator shared the contactor pin |
| R14 | Persistence has versioned schemas and explicit migrations; service hours are never erased implicitly. | A storage-format error path erased all settings, including service hours |
| R15 | Crash records hold reset cause, uptime, wall time if available, firmware/config identity and the last N log lines; tested by forcing each reset cause. | The "store logs on critical error" function was never called |
| R16 | Commissioning tools are read-only by default; device writes are explicit, per unit, and verified against that unit's own baseline. | A hard-coded "repair" table contained a wrong value and copied calibration between units |
| R17 | Warnings as errors, format checking, static analysis, sanitizers on host tests, all in CI. | Format-string bugs and dead guards went unnoticed |
| R18 | Documents that describe behavior are generated from, or checked against, the code (thread/channel inventory, configuration table, integration markers). | Maps and decision records drifted from the code within weeks |
| R19 | A sensor that differs drastically from the others is excluded from the aggregate and reported; exclusion and re-inclusion rules are explicit and tested (D-07; algorithm T-05). | Disagreement was only a warning; field errors came from sensor problems |
| R20 | During COOLING no guard ends the program; a temperature that is not falling raises an alarm (D-06; parameters T-04). | The overshoot guard aborted every cooling step larger than its threshold |
| R21 | The controller is the sole authority; the display is a remote client that can disconnect at any time without affecting safe operation (D-01). | Domain logic, validation, programs and service hours lived inside the display component |

## 6. Concurrency and messaging

- Queues for ownership transfer, semaphores/events for wake-ups, mutexes for compound state, atomics only with a stated memory-order protocol.
- Never use `volatile`, delays or timing luck as synchronization.
- Never block a sole consumer by submitting to its own bounded queue.
- For every channel, define together: producer, consumer, execution context, payload type and size, copy/ownership, timeout, overflow behavior, and shutdown.
- Keep best-effort telemetry separate from reliable commands, state transitions and safety faults.
- Timer callbacks may run in interrupt context: keep them to atomics, GPIO and wake-ups.

## 7. Evidence and claims

- Levels: host unit → `native_sim` integration → plant simulation → target unit → device bench → powered controller → powered furnace.
- Categories for problems:
  - **confirmed**: reachable trigger plus exact evidence;
  - **highly likely**: one dependency not demonstrated;
  - **possible**;
  - **design weakness**.
- A source change without its test is "not done". "Fix implemented, validation pending" is not closure.

## 8. Process

- One behavior or invariant per change; one change per commit, with its requirement or finding ID. Never squash-merge safety work.
- Every hardware validation has a written plan (prerequisites, independent cut-off, procedure, pass criteria, abort, evidence) and runs only with explicit authorization.
- Record decisions as ADRs when they set ownership, boundaries, messaging contracts, persistence formats or safety policy.
- Check commit titles and feature claims against the call graph: a feature is done only when it is wired and tested.
- Keep one source of truth per fact (configuration, tuning, limits). Never keep an untracked local configuration that differs from the repository.
