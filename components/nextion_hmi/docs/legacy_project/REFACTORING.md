# Refactoring Log (DRY/SOLID)

Purpose: Track refactoring opportunities (DRY + single-responsibility) as we review the code, with an explicit extraction/merge plan.

Guidelines for this file:
- Keep refactors incremental (small steps, compile/test between steps).
- Prefer refactors that reduce bugs first (shared parsing, shared formatting, shared validation entrypoints).
- Embedded constraint: reducing duplicate code is good, but avoid adding heavy abstractions that make timing/UART behavior harder to reason about.

---

## Terms (quick)
- **DRY**: “Don’t Repeat Yourself” — avoid duplicated logic that must be fixed in multiple places.
- **SRP (Single Responsibility Principle)**: one function/module should have one primary reason to change.
- **SOLID**: a set of OO design principles. In C, we mainly apply SRP + clear module boundaries + explicit dependencies.
- **Domain validation vs UI validation**:
  - *Domain validation* = is the program mathematically/safely valid?
  - *UI validation* = are the user inputs parseable/present so we can proceed?

---

## RF-001 — Program load/edit flow duplicates parsing + path building
**Observed in:** [main/nextion_events.c](main/nextion_events.c)

### Evidence
- `nextion_program_load_task()` builds `sd0/...` path, reads file, parses to `ProgramDraft`, updates UI, renders graph, syncs programBuffer.
- `load_program_into_draft()` builds the same `sd0/...` path, reads file, parses to `ProgramDraft`.
- `nextion_edit_task()` again builds the path, checks existence, loads into draft, then updates UI and syncs.

### Why this is a DRY/SRP smell
- Same “file → parse → draft” logic exists in multiple places.
- If the file format changes, you must update multiple functions.

### Extraction / merge plan
1. **Extract path builder**
   - New helper: `static bool build_sd0_program_path(const char *name, char *out, size_t out_len)`
   - Replace repeated `snprintf("sd0/%s..."` blocks.

2. **Single file-to-draft function (one source of truth)**
   - Keep `load_program_into_draft()` as the canonical: it already does read+parse.
   - Update `nextion_program_load_task()` to call `load_program_into_draft(filename, ...)` instead of repeating parsing.
   - Ensure `load_program_into_draft()` returns the parsed draft already stored in the global `ProgramDraft` (it does).

3. **Extract “apply loaded draft to UI”**
   - New helper: `static void programs_apply_loaded_draft_to_ui(void)`
   - Contents: set name fields, reset time fields, set page 1, call `programs_page_apply(1)`, optionally `sync_program_buffer()`.

4. **Extract “render graph from current draft”**
   - New helper: `static bool programs_render_graph_from_draft(int graph_id, int width, int height, char *err, size_t err_len)`
   - Used by both load and show-graph.

Expected benefit:
- Less duplication.
- Easier to debug: file format issues are localized.

---

## RF-002 — CSV tokenization is duplicated (save/show_graph/autofill/prog_page_data)
**Observed in:** [main/nextion_events.c](main/nextion_events.c)

### Evidence
- `handle_save_prog()` copies payload into `buffer[512]`, splits by comma into an array of tokens.
- `handle_show_graph()` repeats the same.
- `handle_autofill()` repeats the same.
- `handle_prog_page_data()` repeats similar logic, plus direction parsing.

### Why this is a DRY bug risk
- If you change the event format (add a field, change quoting), you must edit 4 places.
- Small inconsistencies already exist (some functions check parse return values, some ignore them).

### Extraction / merge plan
1. **Extract a general “split CSV by comma” helper (simple, no quoting)**
   - New helper: `static size_t split_csv_in_place(char *buf, char **tokens, size_t max_tokens)`
   - This matches your current protocol (commas only, no quoted commas).

2. **Extract “parse page rows into draft” helper**
   - New helper: `static bool parse_page_rows_into_draft(uint8_t page, char **tokens, size_t token_count, bool require_time_and_delta, char *err, size_t err_len)`
   - Used by:
     - save (require both time and delta)
     - page_data sync (allow partial)
     - show_graph (allow partial)

3. **Extract “compute starting temperature for page” helper**
   - New helper: `static int compute_start_temp_for_page(uint8_t page, int fallback_current_temp)`

Expected benefit:
- You’ll be able to state: “All page parsing goes through one function.”

---

## RF-003 — Validation appears duplicated (UI checks vs domain checks)
**Observed in:** [main/nextion_events.c](main/nextion_events.c) + [main/program_validation.c](main/program_validation.c)

### Your question: “Why validate again in autofill/save?”
You’re right to question it, but it depends on what we mean by validation.

- In `handle_save_prog()`, the code does **UI validation** (are fields present? are numbers parseable?), then calls **domain validation**:
  - `program_validate_draft(program_draft_get(), &effective, ...)`
  This is good separation: UI validation prevents nonsense inputs; domain validation enforces the real rules.

- In `handle_autofill()`, the code does **limit checks** (max temp, delta range) before calculating.
  That is also reasonable because it allows immediate user feedback and prevents generating values that the domain validator would reject.

### Where it *is* a refactoring opportunity
Right now, some numeric checks and “limits” logic are embedded in `handle_autofill()`.

### Extraction plan (single source of truth)
1. Add a small validation helper in the domain layer for per-stage constraints used by autofill
   - Candidate: `program_validate_stage_inputs(...)` or `program_compute_autofill(...)`
   - Goal: Nextion events code calls domain logic to compute/validate, and only formats UI errors.

2. Define a clear contract:
   - Nextion layer: parsing, trim, UI feedback, sending commands.
   - Domain layer: calculations and all safety/consistency constraints.

Expected benefit:
- Changing constraints happens in one place.

---

## RF-004 — Nextion command string escaping is inconsistent
**Observed in:** [main/nextion_events.c](main/nextion_events.c)

### Evidence
- Some code uses manual escaping (confirm dialog uses `\"`).
- Many places do `...txt="%s"` with raw names.

### Plan
- Add helper: `static void nextion_escape_string(const char *in, char *out, size_t out_len)`
- Use it for every `txt="..."` command that includes user-controlled text (program names).

---

## RF-005 — Too many responsibilities in task functions (SRP)
This is where your intuition is correct.

Example: `nextion_program_load_task()` currently does:
1) file read, 2) parse, 3) UI update, 4) graph render, 5) sync.

In embedded C, it’s not "wrong" to do that in one task function, but it becomes hard to test and hard to debug.

### Plan
Keep the task function as an orchestrator:
- `nextion_program_load_task()` should become “call helpers in order + handle errors + free memory”.
- The real work should live in helpers (as listed in RF-001/RF-002/RF-004).

---

## RF-006 — Config source-of-truth cleanup (macros vs `AppConfig`)
**Observed in:** [main/config.c](main/config.c), [main/app_config.h](main/app_config.h)

### Smell
The same constraints exist as compile-time `CONFIG_*` macros and as runtime `AppConfig` fields.

### Plan
1. Treat `config_get_effective()` as the only way other modules obtain limits.
2. Over time, reduce direct use of `CONFIG_*` outside config/UI init code.
3. Decide the NVS policy explicitly (fail-fast vs degrade) and encode it in `config_init()`.

---

## RF-007 — Consolidate UART “transaction” pattern (pause RX + flush + request + read)
**Observed in:** [main/nextion_file_reader.c](main/nextion_file_reader.c) (and similar patterns likely in storage)

### Smell
`nextion_read_file()` and `nextion_file_exists()` both:
- set an “active” flag,
- delay,
- flush UART,
- send a request,
- perform blocking reads with timeouts,
- then clear the flag.

### Plan
1. Extract a small helper to centralize the safe pattern and guarantee cleanup:
   - Candidate: `static bool nextion_uart_begin_exclusive(int settle_ms)` / `static void nextion_uart_end_exclusive(void)`
   - Or a single helper: `static bool nextion_uart_transaction(const char *cmd, uint8_t *rx, size_t rx_len, int timeout_ms, char *err, size_t err_len)`
2. Make the helper responsible for clearing the active flag on all exits.
3. Long-term: replace active-flag + flush with a single RX router task + queues (ties into R-001/R-014).

---

## RF-008 — Single program serialization function
**Observed in:** [main/nextion_events.c](main/nextion_events.c) and [main/nextion_storage.c](main/nextion_storage.c)

### Smell
There are two very similar serializers:
- `serialize_program_to_buffer()` in events
- `serialize_program()` in storage

### Risk
If the on-disk format changes (fields added/renamed), save and sync can diverge.

### Plan
1. Move serialization into the domain layer (best): e.g., `program_serialize_draft(const ProgramDraft*, char*, size_t)`.
2. Use it from both modules (storage save + programBuffer sync).

---

## RF-009 — Transport-layer TX locking
**Observed in:** heavy multi-task callers (`nextion_events`, `nextion_storage`) using `nextion_send_cmd()` / `nextion_send_raw()`.

### Smell
The transport layer does not appear to enforce that UART writes are serialized.

### Plan
1. Add a TX mutex inside `nextion_transport.c` (lock around every UART write).
2. Optionally add a queued TX task later (more robust for bursty traffic like graph render).

### Practical incremental step
- Add `nextion_transport_init_lock()` (called from `nextion_uart_init`) or lazily initialize a static mutex.
- Wrap both `nextion_send_raw()` and `nextion_send_cmd()` with the same lock so mixed raw/command traffic cannot interleave.
- If you later implement a TX queue, keep the current APIs and route them through the queue to avoid touching all callers.

---

## RF-010 — One safe “set text” helper (escape + chunking)
**Observed in:** [main/nextion_ui.c](main/nextion_ui.c) and `nextion_set_text_chunked()` in [main/nextion_events.c](main/nextion_events.c)

### Smell
There are multiple ad-hoc ways of sending text to Nextion fields:
- raw `snprintf(...txt=\"%s\"...)` (breaks on quotes/newlines)
- chunked appends (safer, but currently `static` to events file)

### Plan
1. Promote a reusable helper into a shared module (good place: `nextion_ui.c` or a new `nextion_text.c`).
2. API idea:
   - `void nextion_set_text(const char *obj, const char *text);` (internally escapes and chunks)
3. Replace all direct `...txt="%s"` formatting that uses user-controlled strings (program names, error messages).

---

## RF-012 — Clarify graph sampling contract (width vs buffer length)
**Observed in:** [main/program_graph.c](main/program_graph.c) + call sites in [main/nextion_events.c](main/nextion_events.c)

### Smell
`program_build_graph()` takes both `max_len` and `width_px`. Callers can (and appear to) pass inconsistent values.

### Plan
1. Decide a single definition:
   - Option A: output length is exactly `width_px` (so caller allocates `width_px` bytes and passes that).
   - Option B: output length is `max_len` and width is derived from it (drop `width_px`).
2. Add a lightweight assertion/guard in the function to enforce the chosen contract.
3. Optional: replace float math with integer math to reduce CPU and improve determinism.

---

## RF-013 — Model ownership and snapshot API
**Observed in:** [main/program_models.c](main/program_models.c)

### Smell
The program draft and telemetry are stored as global mutable state and exposed via `program_draft_get()`.

### Plan
1. Add a snapshot helper:
   - `void program_draft_copy(ProgramDraft *out)` (copies the current draft atomically under a lock/critical section).
2. Update long-running consumers (save/graph build) to operate on a local snapshot.
3. Optionally introduce a mutex around draft mutations to make cross-task access explicit.

Expected benefit:
- Removes race conditions without rewriting all callers.

---

## RF-014 — Make validation inputs explicit (remove hidden dependency on current temp)
**Observed in:** [main/program_validation.c](main/program_validation.c)

### Smell
`program_validate_draft()` pulls starting temperature from global state (`program_get_current_temp_c()`).

### Plan
1. Change API (or add a new one):
   - `bool program_validate_draft_from_temp(const ProgramDraft *draft, const AppConfig *config, int start_temp_c, char *error, size_t error_len)`
2. Have UI/event layer snapshot the start temperature at the moment of validation/save.
3. Keep the old API as a wrapper (optional) to avoid touching all callers at once.

---

## RF-015 — Unify program naming rules (UI validation + storage filename)
**Observed in:** [main/program_validation.c](main/program_validation.c), [main/nextion_storage.c](main/nextion_storage.c)

### Smell
Validation and storage implement different name constraints and transformations.

### Plan
1. Create one shared name policy helper in the domain layer:
   - `bool program_name_is_valid(const char *name, char *err, size_t err_len)`
   - `bool program_name_to_filename(const char *name, char *filename, size_t filename_len)`
2. Use the same mapping for overwrite checks (compare filenames, not raw names).

---

## RF-011: UI access layering (avoid both duplication and a "god UI module")

**Problem**
- UI commands (`nextion_send_cmd("...")`) are scattered across event handlers and modules.
- Some sequences are repeated (set text, visibility toggles, browser refresh, programBuffer sync patterns).
- Naively moving *all* UI calls into `nextion_ui.c` risks creating a "god module" (harder debugging, poor cohesion).

**Plan**
1. Keep `nextion_transport.*` as transport only.
2. Add "UI primitives" layer (can live in `nextion_ui.c` or a new `nextion_ui_helpers.c`):
   - `nextion_ui_set_text_safe(obj, text)` (escape + chunk)
   - `nextion_ui_set_visible(obj, on)`
   - `nextion_ui_set_value(obj, int)`
3. Add "screen controller" functions (by page) for reusable sequences:
   - Programs page: apply page fields, refresh browser, render graph
   - Main page: update status
   - Settings page: populate config fields
4. Do **not** wrap one-off commands that are only used once; extract only repeated/error-prone sequences.

**Expected payoff**
- Less duplication where it matters (safe text setting, repeated sequences)
- Improved readability (handlers call named UI actions)
- Debugging remains concrete (you can still see major commands in one place per screen)

---

## Refactoring order (recommended)
1. RF-002 (shared CSV tokenization + page parsing) — biggest DRY win, reduces bugs.
2. RF-001 (reuse `load_program_into_draft` + shared path builder) — removes duplicated parsing.
3. RF-004 (string escaping helper) — prevents broken commands.
4. RF-003 (move more validation/calc into domain layer) — long-term correctness.

We’ll append new RF items here as we review more modules.
