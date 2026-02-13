# Nextion HMI — Merge Plan

> Last updated: 2026-02-12
> Status: Phase 4 COMPLETE — All four merge phases finished

---

## Architecture Principle

**All communication within the HMI must be asynchronous.**
The HMI must talk exclusively through the **coordinator component** and **event_manager**.
The coordinator is the single source of flow — no direct function calls between components
for runtime data. The HMI sends commands via events and receives state updates via events.

---

## Finding 1 — Temperature Model (Two Modes)

### Current Problem
`program_get_current_temp_c()` returns a hardcoded static value of 23 °C.
The HMI never subscribes to `TEMP_PROCESSOR_EVENT`, so the main page always
shows 23 °C and 0 kW. The temperature pipeline is completely disconnected.

### Agreed Design — Two Distinct Modes

#### Mode A: Program Editing / Saving (Offline)
When a user creates or edits a program on the HMI, **we cannot and must not rely on
real thermocouple readings**. The program editor must work independently of the furnace
state:

- Stage 1 uses a **placeholder starting temperature** (e.g. Kconfig default or 0 °C)
- Each subsequent stage uses **the previous stage's target temperature** as its
  starting value
- All values are purely calculated from the draft data — no hardware dependency
- The graph preview on the edit page renders from ProgramDraft data only

#### Mode B: Program Running (Live)
When a user selects a program on the main page and runs it:

- The ESP reads **real thermocouple values** via the temperature pipeline
  (temperature_monitor → temperature_processor → `TEMP_PROCESSOR_EVENT`)
- The coordinator owns the execution state and sends real-time data back to the HMI
- The HMI subscribes to coordinator events and receives:
  - Current real temperature
  - Current target setpoint
  - Current power / heater status
  - Stage progress (which node, time elapsed)
- A **second waveform channel** on the Nextion display plots the real measured
  temperature graph alongside the programmed target curve
- The HMI does NOT poll — data arrives as events

### Hardware Note
The furnace heater is currently replaced by an LED for development. The architecture
must support swapping back to the real heating element without code changes in the HMI.
The HMI is agnostic to what the heater actually is — it only sees events from the
coordinator.

### Action Items
- [ ] Subscribe to `TEMP_PROCESSOR_EVENT` in `nextion_events.c` for live temp display
- [ ] Subscribe to coordinator status events for profile progress
- [ ] Implement two-channel waveform: channel 0 = target (from draft), channel 1 = real (from events)
- [ ] Ensure program editor graph uses only draft data (already the case in `program_graph.c`)
- [ ] Confirm starting temperature logic: stage N start = stage N-1 target

---

## Finding 2 — Coordinator Feedback Loop

### Current Problem
nextion_hmi posts 3 events to the coordinator (START_PROFILE, PAUSE_PROFILE,
STOP_PROFILE) but **never subscribes** to any feedback. The coordinator already emits:
- `COORDINATOR_EVENT_STATUS_STARTED`
- `COORDINATOR_EVENT_STATUS_PAUSED`
- `COORDINATOR_EVENT_STATUS_STOPPED`
- `COORDINATOR_EVENT_NODE_COMPLETED`
- `COORDINATOR_EVENT_ERROR_OCCURRED`

All are ignored. The HMI is fire-and-forget with no confirmation.

### Agreed Design
This ties directly into Finding 1, Mode B. The fix is the same: subscribe to
coordinator events and use them to drive the UI.

### Action Items
- [ ] Subscribe to `COORDINATOR_EVENT` base in `nextion_events.c`
- [ ] Handle STARTED → update main page status to "Running"
- [ ] Handle PAUSED → update main page status to "Paused"
- [ ] Handle STOPPED → update main page, return to idle
- [ ] Handle NODE_COMPLETED → advance stage indicator on display
- [ ] Handle ERROR_OCCURRED → show error popup via `nextion_show_error()`
- [ ] Confirm coordinator event data structs carry enough info (current temp, target, node index, etc.)
  If not, extend coordinator event payloads

---

## Finding 3 — Logging Unification

### Current Problem
Every component in the codebase uses `LOGGER_LOG_INFO/WARN/ERROR` from
`logger_component.h`. nextion_hmi is the only component that uses `ESP_LOGx()` directly.
This means:
- HMI logs bypass the project's async queued logger
- Cannot be disabled via `CONFIG_LOG_ENABLE`
- Inconsistent formatting and level gating

### Agreed Design
Switch all nextion_hmi logging to use the project's `logger_component`.

### Action Items
- [ ] Add `logger_component` to REQUIRES in CMakeLists.txt
- [ ] Replace all `ESP_LOGI(TAG, ...)` → `LOGGER_LOG_INFO(TAG, ...)`
- [ ] Replace all `ESP_LOGW(TAG, ...)` → `LOGGER_LOG_WARN(TAG, ...)`
- [ ] Replace all `ESP_LOGE(TAG, ...)` → `LOGGER_LOG_ERROR(TAG, ...)`
- [ ] Replace all `#include "esp_log.h"` → `#include "logger_component.h"`
- [ ] Files affected: nextion_hmi.c, nextion_transport.c, nextion_storage.c,
  nextion_file_reader.c, nextion_events.c, nextion_ui.c, program_models.c,
  program_graph.c, program_validation.c, program_profile_adapter.c
- [ ] Verify build after replacement

---

## Finding 4 — Error System (UI Popups Stay)

### Current Problem
nextion_hmi uses `nextion_show_error("string")` for user-facing error messages.
It does not integrate with the `error_manager` component or `furnace_error_t` structs.
The `furnace_error_source_t` enum does not include a `SOURCE_NEXTION_HMI` entry.

### Decision
**Current errors are user-facing validation messages** (e.g. "Missing program name",
"Target temp too high", "Read failed"). These are NOT crash reports or structured
system errors. They are meant to inform the user about mistakes they made.

**For now: keep as UI popups.** No integration with `error_manager` needed at this stage.

### Future Consideration
If the HMI later needs to report system-level errors (UART hardware failure, SD card
corruption, etc.), then:
- Add `SOURCE_NEXTION_HMI` to `furnace_error_source_t`
- Register an error descriptor
- Post structured `furnace_error_t` events for system-level failures only
- User-facing validation messages remain as UI popups

---

## Finding 5 — Health Monitoring

### Current Problem
The health monitor expects all major components to periodically call
`event_manager_post_health()`. nextion_hmi makes zero heartbeat calls. If the HMI
task hangs or crashes, the watchdog system won't know.

### Agreed Design
Implement health monitoring for nextion_hmi.

### Action Items
- [ ] Add a component ID for nextion_hmi in the health monitor's component enum
  (e.g. `COMPONENT_NEXTION_HMI`)
- [ ] Add `health_monitor` to REQUIRES in CMakeLists.txt
- [ ] Call `event_manager_post_health(COMPONENT_NEXTION_HMI)` periodically from the
  RX task loop (the always-running task in `nextion_hmi.c`)
- [ ] Decide on heartbeat interval (suggestion: same as other components, likely every
  few seconds)

---

## Finding 6 — Profile Ownership: Who is the Source of Truth?

### Current Problem
Two program models coexist:
1. **HMI model:** `ProgramDraft` / `ProgramStage` — rich data with name, stage times,
   target temps, delta rates, stored on SD card as text files
2. **Coordinator model:** `heating_profile_t` / `heating_node_t` — linked-list runtime
   model used by `temperature_profile_controller` for real-time interpolation

The `program_profile_adapter.c` converts ProgramDraft → heating_profile_t but:
- `delta_t_per_min_x10` is NOT mapped (lost)
- `t_delta_min` is NOT mapped (lost)
- `node->type` is hardcoded to `NODE_TYPE_LINEAR` always
- Uses static arrays that are shared via pointer with the coordinator (no sync)

### Open Question (NEEDS DECISION)
> **Should the HMI's ProgramDraft be the single source of truth for heating programs,
> replacing the colleague's heating_profile_t / heating_node_t entirely?**

#### Option A: HMI is Source of Truth (Replace heating_profile_t)
- ProgramDraft becomes THE program format, stored on SD card
- The coordinator and temperature_profile_controller are refactored to consume
  ProgramDraft directly (or a common intermediate format)
- No more lossy conversion in program_profile_adapter.c
- The linked-list model (heating_node_t) is removed
- **Pro:** Single model, no data loss, HMI programs are exactly what runs
- **Con:** Large refactor of coordinator + temperature_profile_controller

#### Option B: Keep Both, Fix the Adapter
- ProgramDraft remains the authoring/storage format
- heating_profile_t remains the runtime execution format
- Fix program_profile_adapter.c to properly map ALL fields:
  - Map delta_t_per_min_x10 to appropriate node fields
  - Map t_delta_min properly
  - Support NODE_TYPE_LINEAR, LOG, SQUARE, CUBE selection
- Add mutex synchronization for shared memory
- **Pro:** Smaller change, existing coordinator code untouched
- **Con:** Two models to maintain, adapter is an ongoing translation layer

#### Option C: New Common Model in `common` Component
- Define a new `program_model_t` in the common component that satisfies both sides
- Both HMI and coordinator use the same struct
- SD card serialization reads/writes this common model
- **Pro:** Clean shared contract
- **Con:** Significant refactor on both sides

**Current leaning (author's recommendation):** Option A makes the most sense long-term
if the HMI is the only place where programs are authored. The HMI owns program creation,
editing, storage, and selection — it should own the data model too. The coordinator
should receive programs in whatever format the HMI provides and execute them.
However, option B is faster if you want incremental progress.

### Impact Analysis — What Changes If Option A Is Chosen

Full component-by-component impact assessment (completed 2026-02-12):

| Component | Impact | Work Required |
|-----------|--------|---------------|
| `temperature_monitor_component` | **None** | Remove stale `#include "core_types.h"` (unused) |
| `temperature_processor_component` | **None** | Nothing — doesn't reference profiles at all |
| `heater_controller_component` | **None** | Nothing — only manages GPIO on/off from power float |
| `pid_component` | **None** | Nothing — pure (setpoint, measured, dt) → power |
| `temperature_profile_controller` | **HEAVY** | Rewrite: replace `heating_profile_t*` with `ProgramDraft*`, walk `stages[]` array instead of linked list. Simpler code. |
| `coordinator_component` | **HEAVY** | Replace all `heating_profile_t*` with `ProgramDraft*` in config, context, and heating_profile_task. |
| `common/include/core_types.h` | **REMOVE** | Delete `heating_profile_t`, `heating_node_t`, `node_type_t` (or keep `node_type_t` only if curves needed). |
| `nextion_hmi/program_profile_adapter` | **REMOVE** | Delete both `.h` and `.c` — the adapter layer is no longer needed. |
| `main.c` | **Moderate** | Change `hmi_get_profile_slots()` return type → `ProgramDraft*`, pass to coordinator. |

**Key insight:** Only 4 out of 10+ components actually reference `heating_profile_t`:
coordinator, temperature_profile_controller, the adapter (deleted), and main.c.
The remaining components (temp_monitor, temp_processor, heater_controller, pid,
logger, error_manager, health_monitor, etc.) are completely untouched.

**No other component outside nextion_hmi references `ProgramDraft` today.** This means
the types must be moved from `nextion_hmi/include/program_models.h` into a shared
location (e.g. `common/include/program_models.h`) so that the coordinator and
temperature_profile_controller can consume them without depending on nextion_hmi.

### Should ProgramDraft Move Out of nextion_hmi?

Yes — if ProgramDraft becomes the app-wide program format, it **must move to the
`common` component** (or a new `program_models` component). It cannot stay inside
nextion_hmi because:

1. The coordinator and temperature_profile_controller would need to `REQUIRES nextion_hmi`
   — creating a circular dependency (nextion_hmi already requires coordinator events)
2. Semantically, the program model is a shared data contract, not an HMI-specific type
3. The SD file format (serialize/deserialize) stays in nextion_hmi since only the HMI
   talks to the Nextion SD card

**Proposed migration:**
- [ ] Move `ProgramDraft`, `ProgramStage`, and related constants to
  `components/common/include/program_models.h`
- [ ] Move `program_validation.c` / `program_validation_internal.h` to
  `components/common/` (validation is domain logic, not HMI-specific)
- [ ] Keep in nextion_hmi: `program_models.c` (draft state management — UI-specific
  mutable draft), `program_graph.c` (Nextion waveform rendering), SD file I/O
- [ ] Delete `program_profile_adapter.c` and `program_profile_adapter.h`
- [ ] Delete `core_types.h` (or keep stripped down if `node_type_t` curves are needed)
- [ ] Refactor `temperature_profile_controller` to consume `ProgramStage[]` array
- [ ] Refactor `coordinator_component` config/context to store `ProgramDraft*`
- [ ] Update `main.c` accordingly

### Design Decision: Curve Types

`ProgramStage` currently has no curve type field — everything is implicitly linear.
The colleague's `heating_node_t` supports LOG, LINEAR, SQUARE, CUBE via `node_type_t`.

**If linear-only is sufficient for now:** `temperature_profile_core.c` becomes a trivial
linear interpolator over a `ProgramStage[]` array — much simpler code.

**If non-linear curves are needed later:** Add a `curve_type` field to `ProgramStage`,
add UI for selecting it, and update the interpolation logic. This can be done
incrementally after the initial migration.

---

## Finding 7 — Shared Memory Synchronization

### Current Problem
`main.c` passes `hmi_get_profile_slots()` pointer directly into `coordinator_config_t`.
When `hmi_build_profile_from_draft()` mutates the static arrays at run-start, the
coordinator sees changes via the same pointer — implicit shared-memory contract, no mutex.

### Relation to Finding 6
This problem **goes away** if Finding 6 is resolved with Option A (HMI as source of
truth), because the ownership model would be clear and the handoff mechanism redesigned.

If Finding 6 uses Option B (keep both models), then:
- [ ] Add a mutex in `program_profile_adapter.c` around `hmi_build_profile_from_draft()`
- [ ] Have the coordinator take the same mutex when reading profile data
- [ ] OR: use a staging buffer and swap the pointer atomically after conversion completes

---

## Finding 8a — Duplicated File Parsing Code

### What This Means
Inside `nextion_events.c`, the **exact same file parsing logic** exists in two places:

1. **`nextion_program_load_task()`** (lines ~97–190): A FreeRTOS task that reads a
   program file from SD card, parses it line-by-line into a ProgramDraft, then pushes
   stage values to the Nextion display for the editor page.

2. **`load_program_into_draft()`** (lines ~443–520): A static helper function that does
   the same file read + parse into ProgramDraft, used when loading a program for the
   programs list page.

Both functions do identically:
- Build `sd0/filename.ext` path
- `malloc` a file buffer
- Call `nextion_read_file(path, ...)`
- `strtok` line-by-line loop
- Parse `name=` lines with `program_draft_set_name()`
- Parse `stage=` lines with `sscanf()` into the same 5 fields
- Handle old vs new format (delta= vs delta_x10=)
- Call `program_draft_set_stage()` with the same arguments

This is copy-pasted code. If the file format ever changes, both locations must be
updated in sync — a bug risk.

### Action Items
- [ ] Extract a single function: `parse_program_file_to_draft(const char *filename,
  char *error_msg, size_t error_len)` into `nextion_storage.c` (since it's file I/O)
- [ ] Call this function from both `nextion_program_load_task()` and
  `load_program_into_draft()`
- [ ] Delete the duplicated parsing logic from both call sites

---

## Finding 8b — Duplicated Validation Logic (NEW)

### What This Means
`program_validation.c` is the official single-entry-point validator via
`program_validate_draft()`. However, **`handle_autofill()` in `nextion_events.c`
contains ~10 duplicated range/limit checks** using the exact same CONFIG constants.

### Detailed Audit Results

#### `handle_autofill()` in nextion_events.c — **HIGH duplication**

| Line | Duplicated Check | Same as in program_validation.c |
|------|------------------|---------------------------------|
| ~L1159 | `target_t > CONFIG_NEXTION_MAX_TEMPERATURE_C` | L195: `target_t_c <= MAX_TEMPERATURE_C` |
| ~L1190 | `delta_t_x10 > DELTA_T_MAX_PER_MIN_X10` | L212: `delta_t_per_min_x10 <= DELTA_T_MAX` |
| ~L1196 | `delta_t_x10 < DELTA_T_MIN_PER_MIN_X10` | L220: `delta_t_per_min_x10 >= DELTA_T_MIN` |
| ~L1211 | `calc_time > MAX_OPERATIONAL_TIME_MIN` | L189: `t_min <= MAX_OPERATIONAL_TIME` |
| ~L1229 | `t_min <= 0` | L184: `t_min > 0` |
| ~L1235 | `t_min > MAX_OPERATIONAL_TIME_MIN` | L189: `t_min <= MAX_OPERATIONAL_TIME` |
| ~L1246 | `calc_delta_x10 > DELTA_T_MAX` | L212: same |
| ~L1256 | `calc_delta_x10 < DELTA_T_MIN` | L220: same |

These exist to give immediate feedback during autofill calculation, but they use
identical constants and identical logic. If a limit changes, both files must be
updated — a guaranteed future bug.

#### `program_profile_adapter.c` L54 — **LOW** (goes away with Finding 6)
- `if (node_count == 0)` → duplicates the "at least one stage" check from L247

#### NOT duplicates (confirmed clean):
- `handle_save_prog()` — pre-merge completeness checks ("is target set?"), not range
  validation. These are input-layer UI guards, not domain validation.
- `program_graph.c` — display clamping for rendering, not domain validation.
- `program_models.c` — array bounds guards (`stage_number < 1`), not field validation.
- `nextion_storage.c` — serialization skip logic (`if (!stage->is_set)`), not validation.

### Agreed Design

**All domain validation must happen through `program_validation.c`.** Any time a
program is saved — whether temporarily to RAM or permanently to the SD card — it
must pass through the validation component.

Extract shared validation helpers:

```c
// In program_validation.c (or program_validation_internal.h):
bool validate_temp_in_range(int target_t_c, char *err, size_t err_len);
bool validate_delta_t_in_range(int delta_t_x10, char *err, size_t err_len);
bool validate_time_in_range(int t_min, char *err, size_t err_len);
```

Both `handle_autofill()` and `program_validate_draft()` call these shared helpers.
Limit constants are defined once in Kconfig and checked in one place.

### Action Items
- [ ] Extract `validate_temp_in_range()`, `validate_delta_t_in_range()`,
  `validate_time_in_range()` as public helpers in `program_validation.c`
- [ ] Expose them via `program_validation_internal.h`
- [ ] Refactor `handle_autofill()` to call the shared helpers instead of inline checks
- [ ] Refactor `program_validate_draft()` to call the same shared helpers internally
- [ ] Remove the `node_count == 0` check from `program_profile_adapter.c`
  (or delete the file entirely per Finding 6)
- [ ] Ensure every code path that writes to the draft or saves calls validation

---

## Finding 9 — HMI Internal Coordinator (NEW)

### Current Problem
The HMI manages work through **ad-hoc FreeRTOS task spawning**:

| Task | Spawned by | Purpose | Lifetime |
|------|------------|---------|----------|
| `nextion_rx_task` | `nextion_hmi_init()` | Permanent UART read loop | Forever |
| `nextion_init_task` | `nextion_hmi_init()` | One-shot boot UI push | Self-deletes |
| `nextion_program_load_task` | `handle_load_prog()` | Read file → parse → push to UI | Self-deletes |
| `nextion_save_task` | `handle_save_prog()` | Validate → serialize → write to SD | Self-deletes |
| `nextion_edit_task` | `handle_edit()` | Read file → load draft → navigate | Self-deletes |
| `nextion_sync_task` | `request_sync_program_buffer()` | Push draft fields to display | Self-deletes |

Problems:
1. **No ordering** — nothing prevents a user from triggering "save" while "load" is
   still running. Both would fight over the draft and the UART.
2. **Volatile-bool synchronization** (`s_storage_active`, `nextion_file_reader_active()`)
   is not real synchronization — no memory barriers, no atomic handoff.
3. **Task explosion** — every operation allocates a new FreeRTOS task stack from heap,
   runs briefly, then self-deletes. Wasteful and hard to reason about.
4. **No command queue** — `nextion_rx_task` parses a line and immediately calls
   `nextion_event_handle_line()`, which may spawn tasks. No backpressure or ordering.
5. **Adding event subscriptions (Findings 1+2)** would make this worse — incoming
   events from the temperature pipeline and system coordinator would need yet more
   uncoordinated task management.

### Agreed Design — Command Queue Pattern

```
[RX Task] ──parse──> [Command Queue] ──dequeue──> [HMI Coordinator Task]
[ESP Events] ───────────────────────────────────────────┘
```

**`nextion_rx_task`** stays as-is (permanent UART reader), but instead of calling
handlers directly, it **posts a command struct to a FreeRTOS queue**.

**Incoming `esp_event` subscriptions** (temperature data, system coordinator status)
also post to the **same command queue**.

**One `hmi_coordinator_task`** dequeues commands sequentially:
- Only one operation runs at a time (no concurrent save + load)
- UART write access is naturally serialized (single consumer)
- Draft mutations are naturally serialized (no mutex needed for draft)
- File I/O blocks the coordinator — that's fine, the user can't interact with the
  HMI during a file operation anyway
- Health monitor heartbeat can run in this task's idle loop

**No more ad-hoc task spawning.** The 4 spawned tasks (load, save, edit, sync) become
command handler functions called by the coordinator task.

### Command Queue Design

```c
typedef enum {
    HMI_CMD_HANDLE_LINE,        // RX task received a complete line
    HMI_CMD_SAVE_PROGRAM,       // User pressed save
    HMI_CMD_LOAD_PROGRAM,       // User selected a program to load
    HMI_CMD_EDIT_PROGRAM,       // User pressed edit on a program
    HMI_CMD_SYNC_DISPLAY,       // Push draft state to display
    HMI_CMD_TEMP_UPDATE,        // Temperature event from pipeline
    HMI_CMD_PROFILE_STATUS,     // Coordinator status event
    HMI_CMD_RUN_PROFILE,        // User pressed run
    HMI_CMD_PAUSE_PROFILE,      // User pressed pause
    HMI_CMD_STOP_PROFILE,       // User pressed stop
} hmi_cmd_type_t;

typedef struct {
    hmi_cmd_type_t type;
    union {
        char line[CONFIG_NEXTION_LINE_BUF_SIZE];  // for HANDLE_LINE
        char filename[64];                         // for LOAD/EDIT
        float temperature;                         // for TEMP_UPDATE
        // ... other payloads as needed
    } data;
} hmi_cmd_t;
```

### What Stays vs What Changes

| Current | After |
|---------|-------|
| `nextion_rx_task` calls `nextion_event_handle_line()` directly | Posts `HMI_CMD_HANDLE_LINE` to queue |
| `nextion_event_handle_line()` spawns tasks | Coordinator dequeues and calls handler functions |
| `s_storage_active` / `nextion_file_reader_active()` volatile bools | Not needed — single task = natural serialization |
| `s_uart_mutex` recursive mutex for UART writes | May still be needed if RX task sends commands to Nextion (e.g. error popups). Evaluate. |
| `s_program_mutex` for draft access | Not needed — only the coordinator task accesses the draft |
| 4 separate task stack sizes in Kconfig | 1 coordinator task stack size (larger, but only one) |

### Action Items
- [ ] Create `src/coordinator/hmi_coordinator.c` + `hmi_coordinator_internal.h`
- [ ] Define `hmi_cmd_t` struct and `hmi_cmd_type_t` enum
- [ ] Create FreeRTOS queue (`xQueueCreate`) in `nextion_hmi_init()`
- [ ] Create single `hmi_coordinator_task` that dequeues and dispatches
- [ ] Convert `nextion_program_load_task` → `hmi_handle_load()` function
- [ ] Convert `nextion_save_task` → `hmi_handle_save()` function
- [ ] Convert `nextion_edit_task` → `hmi_handle_edit()` function
- [ ] Convert `nextion_sync_task` → `hmi_handle_sync()` function
- [ ] Modify `nextion_rx_task` to post to queue instead of calling handlers
- [ ] Remove volatile-bool synchronization (`s_storage_active` pattern)
- [ ] Remove `s_program_mutex` (draft access is single-threaded now)
- [ ] Evaluate whether `s_uart_mutex` is still needed
- [ ] Add Kconfig entry for coordinator task stack size + queue depth
- [ ] Add health monitor heartbeat in coordinator idle/loop

---

## Implementation Order

The implementation is split into 4 phases. Each phase builds on the previous one.
Within a phase, items can generally be done in any order.

### Phase 1 — Code Cleanup (no architecture changes)

Clean up the existing code BEFORE restructuring. Less code to move = fewer conflicts.

| Step | Finding | Effort | Notes |
|------|---------|--------|-------|
| 1.1 | **#3 Logging** | Small | Replace ESP_LOG → LOGGER_LOG across all files |
| 1.2 | **#8a Dedup parsing** | Small | Extract `parse_program_file_to_draft()` |
| 1.3 | **#8b Dedup validation** | Medium | Extract shared validation helpers |

No open questions. Can start immediately.

### Phase 2 — HMI Coordinator (new foundation)

Establish the command-queue architecture BEFORE adding event subscriptions.
If we added events first without the coordinator, we'd have to re-refactor immediately.

| Step | Finding | Effort | Notes |
|------|---------|--------|-------|
| 2.1 | **#9 HMI Coordinator** | Large | Command queue, single worker task, convert 4 ad-hoc tasks |
| 2.2 | **#5 Health monitor** | Small | Add heartbeat in coordinator task loop |

Phase 2 depends on Phase 1 being complete (cleaner code to refactor).

### Phase 3 — Data Model Migration

Settle the data model BEFORE wiring up event pipes. The events need to carry
the right data types.

| Step | Finding | Effort | Notes |
|------|---------|--------|-------|
| 3.1 | **#6 Move ProgramDraft to common** | Medium | Move types + validation to common component |
| 3.2 | **#6 Refactor coordinator + temp_profile_controller** | Large | Replace heating_profile_t with ProgramDraft |
| 3.3 | **#6 + #7 Delete adapter + old types** | Small | Remove program_profile_adapter, core_types.h linked-list types |

Phase 3 depends on Phase 2 being complete (coordinator handles program dispatch).

### Phase 4 — Wire the Event Pipes

Connect the HMI to the rest of the system through events. Requires both the
HMI coordinator (to receive events) and the data model (to know what flows).

| Step | Finding | Effort | Notes |
|------|---------|--------|-------|
| 4.1 | **#1 Temperature pipeline** | Medium | Subscribe to TEMP_PROCESSOR_EVENT, feed to display |
| 4.2 | **#2 Coordinator feedback** | Medium | Subscribe to COORDINATOR_EVENT, update UI status |
| 4.3 | **#1 Two-channel waveform** | Large | Real-time graph: target curve + measured temp |
| 4.4 | **#4 Error system** | TBD | Revisit — may want FURNACE_ERROR_EVENT subscription |

Phase 4 depends on Phases 2 + 3 being complete.

### Dependency Diagram

```
Phase 1: Cleanup ──────> Phase 2: HMI Coordinator ──────> Phase 3: Data Model ──────> Phase 4: Events
  #3 Logging                #9 Coordinator                  #6 ProgramDraft move       #1 Temp pipeline
  #8a Parsing dedup          #5 Health monitor               #6 Refactor coord          #2 Coord feedback
  #8b Validation dedup                                       #6+#7 Delete old types     #1 Waveform
                                                                                        #4 Error system
```

---

## Phase 1 Changelog — Code Cleanup (COMPLETED)

> Implemented: 2026-02-12
> Build verified: YES — all 14 nextion_hmi source files compile cleanly

### Step 1.1 — Logging Unification (Finding #3)

Replaced all `esp_log.h` / `ESP_LOG*` usage with `logger_component.h` / `LOGGER_LOG_*`
across the nextion_hmi component. This aligns with the rest of the firmware's logging
infrastructure (async queued logging gated on Kconfig).

**Files modified:**
| File | Changes |
|------|---------|
| `CMakeLists.txt` | Added `logger_component` to REQUIRES |
| `src/transport/nextion_transport.c` | 1 ESP_LOGI → LOGGER_LOG_INFO |
| `src/nextion_hmi.c` | 9 calls (7 INFO, 1 WARN, 1 ERROR) |
| `src/storage/nextion_file_reader.c` | 10 calls (5 INFO, 5 WARN) |
| `src/storage/nextion_storage.c` | 11 calls (6 INFO, 5 WARN) |
| `src/events/nextion_events.c` | 17 calls (15 INFO, 1 WARN, 1 ERROR) |

**Total: 48 ESP_LOG → LOGGER_LOG replacements + include/dependency changes**

**What to tell colleague:** The nextion_hmi component now depends on `logger_component`
instead of ESP-IDF's built-in `esp_log`. All logging calls use `LOGGER_LOG_INFO/WARN/ERROR`
macros which route through the project's async logging queue. The behavior is identical
but logging is now consistent with every other component.

### Step 1.2 — File Parsing Deduplication (Finding #8a)

Extracted `nextion_storage_parse_file_to_draft()` — a shared function that reads an SD
card program file and parses it into `g_program_draft`. Previously this logic was
duplicated in two places: `nextion_program_load_task()` (background load) and
`load_program_into_draft()` (immediate load).

**Files modified:**
| File | Changes |
|------|---------|
| `src/storage/nextion_storage.c` | Added `nextion_storage_parse_file_to_draft()` (~80 lines) |
| `src/storage/nextion_storage_internal.h` | Added declaration for the new function |
| `src/events/nextion_events.c` | `nextion_program_load_task()` — replaced ~70 lines of file read+parse with a call to the shared function. UI code (graph render, display push) remains. |
| `src/events/nextion_events.c` | `load_program_into_draft()` — entire ~60 line body replaced with single delegation |

**What to tell colleague:** The file-to-draft parsing logic that was copy-pasted in two
event handlers now lives in one place (`nextion_storage.c`). Both callers delegate to
`nextion_storage_parse_file_to_draft(filename, error_msg, error_len)` which returns
`true` on success. The UI-specific post-load operations (graph rendering, pushing values
to display) remain in the event handlers.

### Step 1.3 — Validation Deduplication (Finding #8b)

Created 3 shared validation helpers in `program_validation.c` and refactored both
`program_validate_draft()` and `handle_autofill()` to use them instead of duplicating
identical range checks.

**Shared helpers added to `program_validation.c`:**
- `validate_temp_in_range(target_t_c, stage_num, err, err_len)` — checks against `CONFIG_NEXTION_MAX_TEMPERATURE_C` and negative
- `validate_time_in_range(t_min, stage_num, err, err_len)` — checks `<= 0` and against `CONFIG_NEXTION_MAX_OPERATIONAL_TIME_MIN`
- `validate_delta_t_in_range(delta_t_x10, stage_num, err, err_len)` — checks against `CONFIG_NEXTION_DELTA_T_MAX/MIN_PER_MIN_X10`

Also renamed `format_x10()` → `format_x10_value()` and made it non-static (public),
since both validation and event formatting need it.

**Files modified:**
| File | Changes |
|------|---------|
| `src/program/program_validation.c` | Added 3 helpers (~50 lines), renamed `format_x10` → `format_x10_value`, refactored `program_validate_draft()` to use helpers |
| `src/program/program_validation_internal.h` | Added declarations for 3 helpers + `format_x10_value()` |
| `src/events/nextion_events.c` | `format_delta_x10()` now delegates to `format_x10_value()` |
| `src/events/nextion_events.c` | `handle_autofill()` — replaced ~10 inline range checks (5 blocks) with shared helper calls |

**What to tell colleague:** The validation rules for temperature, time, and delta_T
range were duplicated between the "Validate" button handler (`program_validate_draft`)
and the "Autofill" button handler (`handle_autofill`). Now both use the same 3 helper
functions. If you change a validation limit in Kconfig, it takes effect everywhere
automatically. The `format_x10` utility was also renamed to `format_x10_value` and
made accessible to both modules (validation and events).

**Error message wording change:** The autofill handler previously showed custom messages
like "Need >= X min at max dT" for calculated delta_t out-of-range. It now shows
the same message as validation: "Delta T exceeds max 5.0" (etc). This is slightly
less specific but ensures consistency and eliminates the duplicate code.

---

## Phase 2 Changelog — HMI Coordinator (COMPLETED)

> Implemented: 2026-02-12
> Build verified: YES — full rebuild (1053 compilation units), clean

### What Changed — Architecture

**Before:** The RX task called `nextion_event_handle_line()` directly, which then spawned
4 separate FreeRTOS tasks for any operation involving SD I/O (load, save, edit, sync).
Each task self-deleted after completion. Coordination relied on volatile bools
(`s_sync_pending`, `s_sync_in_progress`). Multiple tasks could theoretically run
concurrently, fighting over the draft and UART.

**After:** A single `hmi_coordinator_task` processes all commands sequentially from a
FreeRTOS queue. The RX task posts lines to the queue instead of calling handlers directly.
All SD I/O handlers now execute inline on the coordinator task — no more task spawning,
no more volatile-bool synchronization, no more concurrent mutations.

```
[RX Task] ──post──> [Command Queue (depth 8)] ──dequeue──> [HMI Coordinator Task]
```

### New Files

| File | Purpose |
|------|---------|
| `src/coordinator/hmi_coordinator.c` | Queue creation, coordinator task loop, post functions |
| `src/coordinator/hmi_coordinator_internal.h` | `hmi_cmd_t` struct, enum, API declarations |
| `src/coordinator/Kconfig` | Coordinator stack size (8192), priority (5), queue depth (8) |

### Modified Files

| File | Changes |
|------|---------|
| `CMakeLists.txt` | Added `src/coordinator/hmi_coordinator.c` to SRCS, `src/coordinator` to PRIV_INCLUDE_DIRS |
| `Kconfig` | Added `rsource "src/coordinator/Kconfig"`, removed `NEXTION_INIT_TASK_*` configs |
| `src/events/Kconfig` | Removed all 8 per-task configs (sync/save/edit/load × stack+priority) — replaced by single coordinator config |
| `src/events/nextion_events_internal.h` | Added `nextion_event_handle_init()` declaration |
| `src/events/nextion_events.c` | **Major refactor** — see below |
| `src/nextion_hmi.c` | **Major refactor** — see below |

### nextion_hmi.c Changes

- **Include change:** `nextion_events_internal.h` → `hmi_coordinator_internal.h`
- **Removed `nextion_init_task`** — its body moved to `nextion_event_handle_init()` in nextion_events.c
- **RX task:** Both `nextion_event_handle_line(line_buf)` calls replaced with
  `hmi_coordinator_post_line(line_buf)` — lines are posted to the queue instead of
  processed synchronously on the RX thread
- **`nextion_hmi_init()`:**
  - Added `hmi_coordinator_init()` call (creates queue + coordinator task)
  - Removed `nextion_init_task` creation
  - Added `hmi_coordinator_post_cmd(HMI_CMD_INIT_DISPLAY)` to queue initial UI setup

### nextion_events.c Changes

**Removed (dead code):**
- `nextion_program_load_task()` — body merged into `handle_program_select()`
- `nextion_save_task()` — body inlined into `handle_save_prog()`
- `nextion_edit_task()` — body merged into `handle_edit_prog()`
- `nextion_sync_task()` — eliminated (no longer needed)
- `request_sync_program_buffer()` — eliminated
- `SaveTaskArgs` / `EditTaskArgs` typedefs — no longer needed
- `s_sync_pending` / `s_sync_in_progress` volatile bools — no longer needed
- All `xTaskCreate()` calls (4 total)
- All `vTaskDelete(NULL)` calls (11 total)
- All `free()` calls for heap-allocated task args

**Added:**
- `nextion_event_handle_init()` — handles initial display setup (was `nextion_init_task`)

**Modified handlers:**
- `handle_program_select()`: Now does the full load inline — parse file to draft,
  send display name/time, build graph, send graph points, sync buffer. Previously
  just validated + spawned `nextion_program_load_task`.
- `handle_save_prog()`: Bottom section changed from heap-alloc-draft + spawn-task to
  direct call: `nextion_storage_save_program(program_draft_get(), ...)`. No more
  heap copy of the draft — safe because the coordinator serializes all access.
- `handle_edit_prog()`: Full edit logic inline — file exists check, load into draft,
  navigate to programs page, push fields to display, sync buffer. Previously just
  spawned `nextion_edit_task`.
- 4 `request_sync_program_buffer()` calls → `sync_program_buffer()` direct calls

### RAM & Task Impact

| Metric | Before | After |
|--------|--------|-------|
| Permanent tasks | 1 (RX) | 2 (RX + Coordinator) |
| Dynamic tasks (peak) | 1-2 (load/save/edit/sync) | 0 |
| Permanent stack RAM | 4096 (RX) + 2048 (init, brief) | 4096 (RX) + 8192 (coordinator) |
| Dynamic stack allocs | 2048-8192 per operation | 0 |
| Heap allocs per operation | 1-3 (task args, draft copy) | 0 |
| Volatile-bool sync vars | 2 | 0 |
| Kconfig task configs | 10 | 4 (RX stack+pri, coord stack+pri, queue depth) |

Net permanent RAM increase: ~6KB (coordinator stack replaces init task + is always
residing instead of growing/shrinking). But eliminates unpredictable heap fragmentation
from repeated task alloc/free cycles.

### What to Tell Colleague

1. **Architecture change:** The nextion_hmi component now has a command queue pattern.
   The RX task no longer calls event handlers directly — it posts lines to a FreeRTOS
   queue. A single coordinator task dequeues and processes commands one at a time.

2. **No more task spawning:** The 4 dynamically-created tasks (prog_load, prog_save,
   prog_edit, prog_sync) are gone. Their logic is now inline in the handler functions,
   which run on the coordinator task.

3. **Why this matters:** Operations that touch the draft or SD card are now naturally
   serialized — no race conditions, no volatile bools, no mutex needed for draft access.
   If a user presses save while a load is running, the save waits in the queue.

4. **New Kconfig:** 8 old task stack/priority configs replaced by 3 new ones:
   `CONFIG_NEXTION_COORDINATOR_TASK_STACK_SIZE` (8192),
   `CONFIG_NEXTION_COORDINATOR_TASK_PRIORITY` (5),
   `CONFIG_NEXTION_COORDINATOR_QUEUE_DEPTH` (8).
   The init task config is also removed.

5. **The RX task still yields** during file I/O (`nextion_file_reader_active()` /
   `nextion_storage_active()`). This is a hardware constraint — the file reader uses
   the same UART for Nextion SD card protocol. The coordinator pattern doesn't change
   this, but it does ensure only one file operation runs at a time.

---

## Phase 3 Changelog — Data Model Migration (COMPLETED)

> Implemented: 2026-02-12
> Build verified: YES — full clean rebuild (1074 compilation units), zero errors

### What Changed — Architecture

**Before:** Two competing data models coexisted:
1. `ProgramDraft` / `ProgramStage` in `nextion_hmi` — rich flat array with name, stages,
   target temps, delta rates, stored on SD card
2. `heating_profile_t` / `heating_node_t` in `common/core_types.h` — doubly-linked list
   used by `temperature_profile_controller` for real-time interpolation

A lossy adapter (`program_profile_adapter.c`) converted ProgramDraft → heating_profile_t
at run-start, dropping `delta_t_per_min_x10` and `t_delta_min`, hardcoding
`NODE_TYPE_LINEAR`, and using shared static memory with no synchronization.

**After:** `ProgramDraft` is the **single source of truth**. The linked-list model
(`heating_node_t`, `heating_profile_t`, `node_type_t`) has been removed entirely. Both
the coordinator and temperature_profile_controller consume `ProgramDraft` directly.
No more adapter, no more data loss, no more lossy conversion.

```
ProgramDraft ──[run slot copy]──> coordinator_component ──> temperature_profile_controller
    (common/program_types.h)          (ProgramDraft*)            (linear interpolation
                                                                  over stages[] array)
```

### Step 3.1 — Move ProgramDraft / ProgramStage to `common` Component

**Why:** `ProgramDraft` was defined in `nextion_hmi/include/program_models.h`. If the
coordinator or temp_profile_controller wanted to use it, they'd need to depend on
`nextion_hmi` — creating a circular dependency. The types are a shared data contract,
not HMI-specific.

**New files:**
| File | Purpose |
|------|---------|
| `components/common/include/program_types.h` | Canonical `ProgramDraft`, `ProgramStage`, and `PROGRAMS_*` constants |

**Modified files:**
| File | Changes |
|------|---------|
| `nextion_hmi/include/program_models.h` | Now a 2-line shim: `#include "program_types.h"` |

**What to tell colleague:** The `ProgramDraft` and `ProgramStage` types now live in
`components/common/include/program_types.h` instead of the HMI's header. This makes
them available to any component without depending on `nextion_hmi`. All existing code
that includes `program_models.h` continues to work — it's a compatibility shim that
re-exports the common header.

### Step 3.2 — Move Validation to `common` Component

**Why:** The validation logic (`program_validation.c`) checks domain constraints using
Kconfig limits. It's domain logic, not HMI logic. Moving it to `common` lets the
coordinator validate programs before execution if needed.

**New files:**
| File | Purpose |
|------|---------|
| `components/common/include/program_validation.h` | Validation API: `program_validate_draft()`, `program_validate_draft_with_temp()`, helpers |
| `components/common/src/program_validation.c` | Full validation implementation (~280 lines) |

**Modified files:**
| File | Changes |
|------|---------|
| `components/common/CMakeLists.txt` | Added `src/*.c` glob for source files |
| `nextion_hmi/CMakeLists.txt` | Removed `program_validation.c` from SRCS |
| `nextion_hmi/src/program/program_validation_internal.h` | Now a shim: `#include "program_validation.h"` |

**API change:** Added `program_validate_draft_with_temp(draft, start_temp_c, ...)` which
accepts the current furnace temperature explicitly. The old `program_validate_draft()`
still works as a convenience wrapper (passes 0 as start temp). HMI callers were updated
to use `_with_temp` with `program_get_current_temp_c()`.

**What to tell colleague:** The validation code moved from `nextion_hmi` to `common` so
any component can validate programs. The function signature gained a `_with_temp` variant
that takes the starting temperature as a parameter instead of internally calling
`program_get_current_temp_c()`. This removes the validation code's dependency on HMI
state. The old function name still works (defaults to 0°C start temp).

### Step 3.3 — Refactor `temperature_profile_controller` (COLLEAGUE'S CODE)

**Why:** The controller's core job is: given a program and an elapsed time, return the
target temperature. Previously it walked a doubly-linked list of `heating_node_t` with
LOG/LINEAR/SQUARE/CUBE interpolation. Now it walks the `ProgramStage[]` array with
simple linear interpolation. Much simpler code.

**Modified files:**
| File | Changes |
|------|---------|
| `include/temperature_profile_types.h` | `#include "core_types.h"` → `#include "program_types.h"`. `temp_profile_config_t` changed: `heating_profile_t *heating_profile` → `const ProgramDraft *program` |
| `src/temperature_profile_core.c` | **Rewritten.** Context stores `const ProgramDraft *program`. `load_heating_profile()` stores program pointer. `get_target_temperature_at_time()` iterates `stages[]` array, skips unset stages, does linear interpolation within each stage. `shutdown_profile_controller()` unchanged. |

**Key behavior changes:**
- **Linked-list traversal → array iteration:** `for (i = 0..TOTAL_STAGE_COUNT)` instead
  of `while (node != NULL)`
- **Curve types removed:** LOG/SQUARE/CUBE interpolation removed. All stages use linear
  interpolation. If non-linear curves are needed later, add a `curve_type` field to
  `ProgramStage` and restore the switch statement.
- **Duration source:** `stage->t_min * 60 * 1000` (minutes to ms) instead of
  `node->duration_ms` (was already in ms)
- **Temperature source:** `(float)stage->target_t_c` instead of `node->set_temp`
  (was already float)
- **No more `<math.h>` dependency** (no log10f needed for LOG curve)

**What to tell colleague:** Your `temperature_profile_core.c` has been rewritten to
consume `ProgramDraft` directly instead of walking a linked list. The interpolation
logic is simpler — just linear over the `stages[]` array. The `temp_profile_config_t`
struct now has a `const ProgramDraft *program` field instead of `heating_profile_t
*heating_profile`. The error enum and `profile_controller_error_t` are unchanged. The
LOG/SQUARE/CUBE curve types are removed for now (they were never actually used — the
adapter hardcoded LINEAR). They can be restored later by adding a `curve_type` field
to `ProgramStage`.

### Step 3.4 — Refactor `coordinator_component` (COLLEAGUE'S CODE)

**Why:** The coordinator stored `heating_profile_t *heating_profiles` and indexed into
it by profile_index. Now it stores `const ProgramDraft *programs` instead.

**Modified files:**
| File | Changes |
|------|---------|
| `include/coordinator_component_types.h` | `#include "core_types.h"` → `#include "program_types.h"`. `coordinator_config_t` changed: `heating_profile_t *profiles` + `num_profiles` → `const ProgramDraft *programs` + `num_programs` |
| `include/coordinator_component.h` | Removed `#include "core_types.h"` |
| `src/coordinator_component_internal.h` | `#include "core_types.h"` → `#include "program_types.h"`. Context changed: `heating_profile_t *heating_profiles` + `num_profiles` → `const ProgramDraft *programs` + `num_programs` |
| `src/coordinator_core.c` | Init stores `config->programs` / `config->num_programs`. `coordinator_list_heating_profiles()` iterates `programs[]` and logs `prog->name`. |
| `src/heating_profile_task.c` | `start_heating_profile()`: gets `&ctx->programs[profile_index]`, calculates `total_time_ms` by summing all set stages' durations, creates `temp_profile_config_t` with `.program = prog`. Old code accessed `ctx->heating_profiles[profile_index].first_node->duration_ms` (only got first node's duration — this was a bug). |

**Bug fix:** The old code set `total_time_ms = ctx->heating_profiles[profile_index]
.first_node->duration_ms` which only captured the **first node's** duration, not the
total program duration. The new code sums all set stages: `total_ms += stage->t_min *
60 * 1000`. This was a latent bug in the original coordinator code.

**What to tell colleague:** Your coordinator now stores `const ProgramDraft *programs`
instead of `heating_profile_t *profiles`. The config struct fields are renamed to
`programs` / `num_programs`. In `start_heating_profile()`, the total time calculation
now correctly sums ALL stages instead of just taking the first node's duration (this
was a bug in the original). The `temp_profile_config_t` is created with `.program = prog`
instead of `.heating_profile = &profiles[index]`.

### Step 3.5 — Update `main.c` Wiring

**Modified files:**
| File | Changes |
|------|---------|
| `main/main.c` | Removed `#include "program_profile_adapter.h"`. Changed `hmi_get_profile_slots(&profile_count)` → `hmi_get_run_program(&program_count)`. Changed config fields: `.profiles = profiles, .num_profiles = profile_count` → `.programs = programs, .num_programs = program_count` |

**What to tell colleague:** `main.c` no longer includes the adapter header. It gets a
`const ProgramDraft*` from `hmi_get_run_program()` and passes it to the coordinator
config. The function returns a pointer to a static "run slot" — a `ProgramDraft` that
the HMI copies the validated draft into at run-start time.

### Step 3.6 — Delete Adapter + Linked-List Types

**Removed from build:**
| File | Status |
|------|--------|
| `nextion_hmi/src/program/program_profile_adapter.c` | Removed from CMakeLists.txt SRCS |
| `nextion_hmi/include/program_profile_adapter.h` | No longer included anywhere |

**Note:** The `.c` and `.h` files still exist on disk but are no longer compiled. They
can be git-deleted at leisure.

**Modified files:**
| File | Changes |
|------|---------|
| `common/include/core_types.h` | Stripped to a deprecation shim: just `#include "program_types.h"`. The `heating_profile_t`, `heating_node_t`, `node_type_t` types are gone. |
| `temperature_monitor_component/src/temperature_monitor_core.c` | Removed stale `#include "core_types.h"` (was unused) |

### Step 3.7 — HMI Run Slot + Draft-to-Run Copy

**New "run slot" pattern:** When the user presses "Run", the HMI:
1. Validates the draft with `program_validate_draft_with_temp()`
2. Copies the draft into a static "run slot" via `program_copy_draft_to_run_slot()`
3. Posts `COORDINATOR_EVENT_START_PROFILE` with `profile_index = 0`

This replaces the old pattern where `hmi_build_profile_from_draft()` converted the
draft into linked-list nodes in static arrays.

**Modified files:**
| File | Changes |
|------|---------|
| `nextion_hmi/include/nextion_hmi.h` | Added `#include "program_types.h"`, added `hmi_get_run_program()` declaration |
| `nextion_hmi/src/program/program_models.c` | Added static `s_run_program` (run slot). Added `hmi_get_run_program()` and `program_copy_draft_to_run_slot()` |
| `nextion_hmi/src/program/program_models_internal.h` | Added `program_copy_draft_to_run_slot()` declaration |
| `nextion_hmi/src/events/nextion_events.c` | `handle_run_start()`: removed `hmi_build_profile_from_draft()` call, replaced with `program_copy_draft_to_run_slot()`. Removed `#include "program_profile_adapter.h"`. Both `program_validate_draft()` calls updated to `program_validate_draft_with_temp()`. |

### Impact Summary

| Component | Impact | Type |
|-----------|--------|------|
| `common` | New files + CMake update | Types + validation moved here |
| `temperature_profile_controller` | **Rewritten** | Linked list → array, curves removed |
| `coordinator_component` | **Modified** | New config/context types, bug fix |
| `nextion_hmi` | Adapter removed, validation moved out | Simpler, less code |
| `main.c` | New API call | 3-line change |
| `temperature_monitor_component` | Stale include removed | 1-line cleanup |

### Data Flow — Before vs After

**Before:**
```
ProgramDraft ──adapter──> heating_node_t linked list ──pointer──> coordinator
                (lossy: drops delta_t,                  ──pointer──> temp_profile_ctrl
                 hardcodes LINEAR,                       (walks linked list,
                 no mutex)                                LOG/LINEAR/SQUARE/CUBE)
```

**After:**
```
ProgramDraft ──copy──> run slot (static ProgramDraft) ──pointer──> coordinator
               (full copy,                               ──pointer──> temp_profile_ctrl
                mutex-protected,                          (walks stages[] array,
                no data loss)                              linear interpolation)
```

---

## Open Questions

1. ~~**Finding 6 — Curve types:** Resolved — linear-only for now. The old LOG/SQUARE/CUBE
   interpolation has been removed. If curves are needed later, add a `curve_type` field
   to `ProgramStage` and update `temperature_profile_core.c`.~~
2. **Finding 1/2:** What data should the coordinator event payloads carry for real-time
   HMI updates? Need to define the event data struct (current temp, target temp,
   current node index, elapsed time, power level, etc.)
3. **Finding 5:** What component ID value to assign to nextion_hmi in the health monitor
   enum? Need to check existing values to avoid collision.
4. ~~**Finding 6 — Validation location:** Resolved — `program_validation.c` moved to
   `components/common/`. Both HMI and coordinator can validate programs. The HMI callers
   use `program_validate_draft_with_temp()` passing the current furnace temperature.~~
5. **Finding 9 — UART mutex:** Once the HMI coordinator is the single writer, is
   `s_uart_mutex` still needed? The RX task only reads; the coordinator only writes.
   But error popups triggered from RX context might still need the mutex.
6. ~~**Finding 9 — Queue depth:** Resolved — set to 8 items via
   `CONFIG_NEXTION_COORDINATOR_QUEUE_DEPTH`.~~

---

## Phase 4 Changelog — Wire Event Pipes (COMPLETED)

> Implemented: 2026-02-12
> Build verified: YES — incremental build (11 steps), zero errors

### What Changed — Architecture

**Before:** The HMI coordinator queue only handled two command types:
- `HMI_CMD_INIT_DISPLAY` → push initial UI state after boot
- `HMI_CMD_HANDLE_LINE` → process a Nextion line from the RX task

The display had **no way** to receive system events. Temperature display was
updated only when the HMI polled or the user navigated pages. Profile status
(started/paused/stopped) was not reflected on the display at all. The waveform
graph only showed the target curve (channel 0) — no live measured temperature.

**After:** The HMI coordinator subscribes to ESP event system events and bridges
them into the command queue using lightweight callbacks. Five new command types
route system events to dedicated handler functions, all serialized on the same
coordinator task:

```
  ┌───────────────────────────────────────────────────────────────────┐
  │ ESP Event Loop                                                    │
  │                                                                   │
  │  TEMP_PROCESSOR_EVENT ──┐                                         │
  │  COORDINATOR_EVENT ─────┼─ event bridge callbacks (lightweight)   │
  │                         │   → pack hmi_cmd_t                      │
  │                         │   → xQueueSend (non-blocking, drop ok)  │
  └─────────────────────────┼─────────────────────────────────────────┘
                            │
                            ▼
  ┌───────────────────────────────────────────────────────────────────┐
  │ HMI Command Queue (depth 8)                                       │
  │                                                                   │
  │   HMI_CMD_HANDLE_LINE ──────> nextion_event_handle_line()         │
  │   HMI_CMD_INIT_DISPLAY ─────> nextion_event_handle_init()        │
  │   HMI_CMD_TEMP_UPDATE ──────> nextion_event_handle_temp_update() │
  │   HMI_CMD_PROFILE_STARTED ──> nextion_event_handle_profile_*()   │
  │   HMI_CMD_PROFILE_PAUSED                                         │
  │   HMI_CMD_PROFILE_RESUMED                                        │
  │   HMI_CMD_PROFILE_STOPPED                                        │
  │   HMI_CMD_PROFILE_ERROR ────> nextion_event_handle_profile_error()│
  └───────────────────────────────────────────────────────────────────┘
```

### Files Changed

#### `src/coordinator/hmi_coordinator_internal.h` — Command types + union payload

**What:** Expanded `hmi_cmd_type_t` from 2 to 8 values. Converted `hmi_cmd_t`
from a flat struct with just a `line[]` field to a **union-based** envelope:

| Union member | Used by | Fields |
|---|---|---|
| `line[BUF_SIZE]` | `HMI_CMD_HANDLE_LINE` | Raw Nextion line |
| `temp{average_temperature, valid}` | `HMI_CMD_TEMP_UPDATE` | Live furnace temperature |
| `profile_event{current_temperature, target_temperature, elapsed_ms, total_ms}` | `HMI_CMD_PROFILE_*` | Profile status (reserved for future use) |
| `error{error_code, esp_error}` | `HMI_CMD_PROFILE_ERROR` | Coordinator error details |

**Why:** The union keeps `sizeof(hmi_cmd_t)` stable — it's dominated by the
`line[]` field anyway — while giving each event type its own typed payload.
Added `#include "event_registry.h"` for `coordinator_error_code_t`.

#### `src/coordinator/hmi_coordinator.c` — Event subscription + dispatch

**What:** Added two **static bridge functions**:

1. `temp_processor_event_bridge()` — receives `temp_processor_data_t` from the
   event loop, packs `hmi_cmd_t{.type=HMI_CMD_TEMP_UPDATE, .temp={...}}`, posts
   to queue with `xQueueSend(..., 0)` (non-blocking, best-effort).

2. `coordinator_event_bridge()` — receives all `COORDINATOR_EVENT` IDs, maps
   TX events (PROFILE_STARTED/PAUSED/RESUMED/STOPPED/ERROR_OCCURRED) to the
   matching `HMI_CMD_*` type, ignores RX events (we sent those ourselves).

In `hmi_coordinator_init()`: two `event_manager_subscribe()` calls register
these bridges. Added `#include "event_manager.h"` and `#include "event_registry.h"`.

Extended the task loop `switch` from 2 cases to 8, each calling the corresponding
`nextion_event_handle_*()` function declared in `nextion_events_internal.h`.

**Why this is safe:** Bridge callbacks are called on the event_manager's event
loop task. They do zero I/O — just memset + field writes + `xQueueSend`. If the
queue is full the event is silently dropped (UI updates are best-effort). All
actual display work happens on the coordinator task.

#### `src/events/nextion_events_internal.h` — New handler declarations

**What:** Added 6 new function declarations:
- `nextion_event_handle_temp_update(float temperature, bool valid)`
- `nextion_event_handle_profile_started(void)`
- `nextion_event_handle_profile_paused(void)`
- `nextion_event_handle_profile_resumed(void)`
- `nextion_event_handle_profile_stopped(void)`
- `nextion_event_handle_profile_error(coordinator_error_code_t, esp_err_t)`

Added `#include <stdbool.h>` and `#include "event_registry.h"`.

#### `src/events/nextion_events.c` — Event handler implementations + two-channel waveform

**What:**

1. **Three new static variables** for waveform tracking:
   - `s_waveform_active` — true while a profile is running
   - `s_waveform_total_ms` — total duration of loaded profile
   - `s_waveform_x` — current pixel column on channel 1

2. **`nextion_event_handle_temp_update()`:**
   - Rounds temperature to integer, calls `program_set_current_temp_c()`
   - Sends `currentTemp.txt="N"` to the display
   - If waveform is active and x < graph width: scales temp to Y pixel
     `(temp_c × HEIGHT / MAX_TEMP)`, sends `add <id>,1,<y>` on **channel 1**

3. **`nextion_event_handle_profile_started()`:**
   - Sets display to "Running", clears error overlay
   - Computes `total_ms` from all set stages in the draft
   - Resets waveform state (`s_waveform_x = 0`, `s_waveform_active = true`)
   - Clears channel 1 (`cle <id>,1`) — channel 0 (target curve) is preserved

4. **`nextion_event_handle_profile_paused/resumed()`:**
   - Updates `progNameDisp.txt` to "Paused" / "Running"

5. **`nextion_event_handle_profile_stopped()`:**
   - Updates display to "Stopped"
   - Sets `s_waveform_active = false` to stop adding points

6. **`nextion_event_handle_profile_error()`:**
   - Maps `coordinator_error_code_t` to human-readable string via
     `coordinator_error_to_str()`
   - Combines with `esp_err_to_name()` output and shows via `nextion_show_error()`

### Two-Channel Waveform Design

| Channel | Source | Rendering |
|---|---|---|
| 0 (target) | `program_build_graph()` | Pre-rendered when program is selected — all points pushed at once |
| 1 (measured) | `TEMP_PROCESSOR_EVENT` | One point per `TEMP_UPDATE` event, pushed in real time |

The pixel mapping for channel 1 is:
`y = (temp_c × NEXTION_MAIN_GRAPH_HEIGHT) / NEXTION_MAX_TEMPERATURE_C`

Channel 1 is cleared when a profile starts (`cle <id>,1`). The
`s_waveform_x` counter prevents overflow past graph width.

### What Is NOT Yet Done (Future Work)

1. **Elapsed/remaining time display:** The `profile_event` union member has
   `elapsed_ms` and `total_ms` fields ready, but the coordinator doesn't
   currently post periodic status updates. When `COORDINATOR_EVENT_NODE_STARTED`
   or a periodic status mechanism is added, these fields can drive
   `timeElapsed.txt` and `timeRamaining.txt` updates.

2. **Power (kW) display:** `currentKw.txt` is still updated only by the
   manual `update_main_status()` call. To make it event-driven, the heater
   controller component would need to publish power events.

3. **Waveform time scaling:** Currently each `TEMP_UPDATE` event adds one
   pixel. For accurate time alignment, the x-axis should advance based on
   `elapsed_ms / total_ms × width`. This requires periodic coordinator
   status data (see item 1).

4. **Health monitor integration (Finding 5):** `nextion_hmi` still doesn't
   register with the health monitor. Needs a component ID assignment.
