# Risk Log (debug-first)

Purpose: Track vulnerabilities, reliability risks, and "will break if" issues as we walk the code.

How to use:
- Each item has an ID so we can reference it while debugging.
- "Trigger/Symptoms" tells you what you’ll likely observe.
- "Root cause" is the concrete mechanism.
- "Blast radius" is what features fail.
- "Mitigation" is the safest fix direction.

---

## R-001 — UART stream contention (two readers)
**Area:** UART RX architecture

- **Location:** `main.c` (`nextion_rx_task`) coordinating with `nextion_file_reader_*` / `nextion_storage_*`
- **Trigger/Symptoms:**
  - File read/write sometimes corrupts or times out.
  - Nextion events appear as garbage / partial strings.
  - Random "it worked once" behavior when saving/loading.
- **Root cause:** UART is a single byte stream. If more than one task/function calls `uart_read_bytes()` concurrently, bytes are split nondeterministically.
- **Blast radius:** Anything that depends on serialized request/response or bulk data transfer (file ops, replies, event lines).
- **Current mitigation in code:** `nextion_rx_task` pauses when `nextion_file_reader_active()` or `nextion_storage_active()`.
- **Residual risk:**
  - If any other code reads UART without setting these “active” flags, contention returns.
  - If “active” stays true due to a bug, RX stops and UI appears dead.
- **Mitigation (stronger):**
  - Centralize UART reads into a single “UART RX router” task + queues.
  - Or add a mutex/lock around UART reads and enforce ownership consistently.

## R-002 — Nextion line buffer overflow drops events
**Area:** UART event framing

- **Location:** `main.c` (`NEXTION_LINE_BUF_SIZE` = 128, overflow handler)
- **Trigger/Symptoms:**
  - ESP logs: `Nextion line buffer overflow, dropping line`.
  - Buttons/events work except for "big" ones (save program, graph data, autofill) which fail intermittently.
- **Root cause:** Incoming event lines longer than 127 chars are discarded when buffer fills before `\n`.
- **Blast radius:** Any Nextion event that transmits CSV payloads (e.g., program stage data).
- **Mitigation (short-term):**
  - Increase buffer size (e.g., 256/512) after measuring max line length.
- **Mitigation (robust):**
  - Switch to length-delimited framing (prefix with length) or chunking protocol.
  - Or parse streaming tokens (scan for separators) instead of buffering the whole line.

## R-003 — Protocol dependency: newline-terminated ASCII events
**Area:** Nextion → ESP protocol contract

- **Location:** `main.c` RX parser expects `\n` to terminate messages; ignores `\r`.
- **Trigger/Symptoms:**
  - No events processed; UI seems unresponsive.
  - Line buffer fills and overflows repeatedly.
- **Root cause:** If Nextion script doesn’t emit `\n` (CRLF is OK) after each event, RX task never dispatches.
- **Blast radius:** All event handling.
- **Mitigation:** Ensure every event ends with `printh 0d 0a` or equivalent `\r\n`.

## R-004 — Silent failure mode: `config_init()` logs error then continues
**Area:** startup robustness

- **Location:** `main/config.c` (`config_init`)
- **Trigger/Symptoms:**
  - NVS init fails (flash issues or partition issues), later features relying on NVS behave oddly.
- **Root cause:** On NVS init error, function logs and returns, but `app_main` continues boot.
- **Blast radius:** Anything that later depends on NVS/config persistence.
- **Mitigation:** Decide desired behavior:
  - Fail-fast (reboot/panic) if NVS is required.
  - Or keep running but explicitly mark “config storage unavailable” and disable features.

## R-005 — Startup timing sensitivity (Nextion boot readiness)
**Area:** boot UI sync

- **Location:** `main.c` (`nextion_init_task` delays 500ms/30ms)
- **Trigger/Symptoms:**
  - Sometimes starts on wrong page or misses initial status updates.
- **Root cause:** Fixed delays assume Nextion is ready; real readiness can vary.
- **Blast radius:** Initial UI correctness.
- **Mitigation:** Add a handshake (e.g., send `get`/`connect` and wait for expected reply) before sending page/status commands.

## R-006 — TX interleaving: multiple tasks can send Nextion commands concurrently
**Area:** Nextion command transport usage

- **Location:** `main/nextion_events.c` spawns tasks (`prog_load`, `prog_edit`, `prog_save`, `prog_sync`) that all call `nextion_send_cmd()`.
- **Trigger/Symptoms:**
  - Random command failures (page changes missed, corrupted `add` waveform commands).
  - Display behaves inconsistently under load (save + sync + graph render).
- **Root cause:** If multiple FreeRTOS tasks call UART write functions concurrently, bytes from different commands can interleave on the wire unless the transport layer serializes TX.
- **Blast radius:** Any UI updates during concurrent operations; graph rendering is most sensitive (many commands).
- **Mitigation:** Add a single TX mutex/queue so only one task writes to Nextion at a time.

## R-007 — Event parsing uses substring matching (`strstr`) instead of prefix matching
**Area:** event dispatch correctness

- **Location:** `main/nextion_events.c` (`nextion_event_handle_line`)
- **Trigger/Symptoms:**
  - Wrong handler fires for a line.
  - A program name containing e.g. `nav:` or `save_prog:` triggers unexpected navigation/save.
- **Root cause:** `strstr(clean, "nav:")` matches anywhere in the line, not just at the start.
- **Blast radius:** Navigation, saving, editing, deletion—potentially destructive flows.
- **Mitigation:** Require prefix match (`strncmp(clean, "nav:", 4) == 0`) or a strict `key:value` parser.

## R-008 — Truncation risks: fixed buffers may cut commands/payloads mid-string
**Area:** robustness of UI updates

- **Location:** multiple fixed buffers: `clean[512]`, `buffer[512]`, many `cmd[64/96/128]`.
- **Trigger/Symptoms:**
  - Nextion rejects commands silently.
  - Text fields show partial content; subsequent parsing fails.
- **Root cause:** `snprintf` truncation can remove closing quotes or commas; `strncpy` truncation can drop tokens.
- **Blast radius:** Program names, confirm dialogs, programBuffer syncing, page-data events.
- **Mitigation:**
  - Keep an explicit max-length contract for each event type.
  - Check `snprintf` return values where correctness depends on full strings.

## R-009 — Task creation failure not always handled (can leak memory and hide errors)
**Area:** reliability under low memory

- **Location:** `handle_save_prog()` calls `xTaskCreate(nextion_save_task, ...)` without checking return.
- **Trigger/Symptoms:**
  - On low heap: save does nothing; sometimes later crashes/odd behavior.
- **Root cause:** If `xTaskCreate` fails, allocated `draft_copy`/`args` are never freed and user is not told.
- **Blast radius:** Save flow; heap fragmentation over time.
- **Mitigation:** Always check `xTaskCreate` result and free allocations + show error.

## R-010 — Data races on `volatile` flags (not atomic)
**Area:** cross-task state coordination

- **Location:** `s_sync_pending` / `s_sync_in_progress` used across tasks.
- **Trigger/Symptoms:**
  - Rare: multiple sync tasks, or sync stops unexpectedly.
- **Root cause:** `volatile` prevents compiler optimization but does not make updates atomic or thread-safe.
- **Blast radius:** programBuffer sync reliability.
- **Mitigation:** Use a mutex, critical section, or FreeRTOS atomic/notification primitives.

## R-011 — User text is not fully escaped before sending to Nextion
**Area:** command correctness / injection-like behavior

- **Location:** commands like `progNameDisp.txt="%s"`, `progNameInput.txt="%s"`.
- **Trigger/Symptoms:**
  - If program name includes `"` or `\\`, Nextion command breaks.
  - Worst case: injected extra Nextion commands if quotes/terminators are not handled.
- **Root cause:** Nextion’s string syntax requires escaping; raw `%s` may contain special characters.
- **Blast radius:** program display/edit flows.
- **Mitigation:** Implement a small "escape for Nextion string" helper (handles quotes, backslashes, CR/LF) and use it consistently.

## R-012 — NVS erase return value ignored
**Area:** config/NVS resilience

- **Location:** `main/config.c` (`config_init`) calls `nvs_flash_erase()` without checking return code.
- **Trigger/Symptoms:**
  - Logs "NVS partition needs erase" but subsequent init still fails.
  - Later modules that expect NVS to work may behave unpredictably.
- **Root cause:** If erase fails (flash wear, partition issue), the code proceeds as if it succeeded.
- **Blast radius:** Any future feature that persists settings/state to NVS.
- **Mitigation:** Check erase return, log it, and decide a policy (retry, reboot, disable persistence).

## R-013 — Config values exist in two worlds (compile-time macros vs runtime struct)
**Area:** configuration as single source of truth

- **Location:** `main/app_config.h` defines `CONFIG_*` macros; `main/config.c` copies them into `AppConfig`.
- **Trigger/Symptoms:**
  - Some code uses macros directly while other code uses `AppConfig`, causing mismatched limits.
- **Root cause:** There are two representations of the same values.
- **Blast radius:** Validation, autofill, UI limit display.
- **Mitigation:** Prefer `config_get_effective()` everywhere and limit direct `CONFIG_*` use to one module.

## R-014 — `uart_flush_input()` drops asynchronous Nextion events
**Area:** UART RX/data loss during file operations

- **Location:** `main/nextion_file_reader.c` (`nextion_read_file`, `nextion_file_exists`)
- **Trigger/Symptoms:**
  - During/after file operations, some button events are “missed”.
  - UI feels unresponsive while loading/saving.
- **Root cause:** `uart_flush_input()` discards any bytes already received. If Nextion sends an event line while a file op starts, it can be thrown away.
- **Blast radius:** Navigation/events near the time of file reads; can look like random UI glitches.
- **Mitigation:**
  - Prefer a single RX router that demultiplexes file-transfer replies vs line events.
  - If keeping flush: only flush immediately before a request when you’re certain no valuable events are expected, and consider notifying UI to disable inputs during file ops.

## R-015 — Assumed response framing for `rdfile` may desynchronize UART stream
**Area:** Nextion protocol robustness

- **Location:** `main/nextion_file_reader.c` expects exactly 4 bytes for size, and raw bytes for chunks.
- **Trigger/Symptoms:**
  - Reads hang/time out or return wrong size.
  - Subsequent UART parsing breaks (garbage lines or missing responses).
- **Root cause:** If Nextion returns error/status bytes (e.g., 0x05/0x06/0x1A depending on model/firmware) or leftover bytes from prior operations remain in the UART buffer, the code may treat them as file size/data.
- **Blast radius:** All file reads and any UART consumer after a desync.
- **Mitigation:**
  - Implement explicit response parsing (detect error codes, resync rules).
  - Drain any trailing bytes after each request until UART idle for a short window.

## R-016 — “Active flag” can deadlock RX if not cleared
**Area:** cross-module coordination

- **Location:** `s_file_read_active` is used to pause the RX task.
- **Trigger/Symptoms:**
  - RX task stops dispatching events forever; UI appears dead.
- **Root cause:** If a code path returns/crashes before clearing `s_file_read_active`, the RX task will keep yielding.
- **Blast radius:** All Nextion events.
- **Mitigation:**
  - Use a structured cleanup pattern (single exit path) or a guard helper.
  - Add a watchdog timeout: if active is true for >N seconds, log and clear/reset.

## R-017 — Storage uses `uart_flush_input()` and can drop user events
**Area:** UART RX/data loss during save/delete

- **Location:** `main/nextion_storage.c` (`nextion_storage_save_program`, `nextion_storage_delete_program`)
- **Trigger/Symptoms:** Button presses/events are missed while saving/deleting.
- **Root cause:** `uart_flush_input()` discards queued bytes; Nextion may emit events asynchronously.
- **Blast radius:** UI responsiveness around save/delete operations.
- **Mitigation:** Same direction as R-014 (single RX router / disable UI inputs during transfer).

## R-018 — Infinite retry on NAK can hang saving forever
**Area:** file transfer robustness

- **Location:** `main/nextion_storage.c` in the TWFILE packet loop: on `0x04` (NAK) it `continue;` with no retry limit.
- **Trigger/Symptoms:** Save never finishes; storage active remains true; UI appears dead.
- **Root cause:** If the Nextion keeps NAKing (bad path, buffer overflow, baud issues), the loop can spin forever.
- **Blast radius:** Save flow; potentially blocks all events due to `s_storage_active`.
- **Mitigation:** Add a per-packet retry counter and a global transfer timeout; on failure, abort and clear active flag.

## R-019 — TX serialization not guaranteed during TWFILE transfer
**Area:** UART TX concurrency

- **Location:** `main/nextion_storage.c` sends `nextion_send_cmd` + `nextion_send_raw` while other tasks may also send.
- **Trigger/Symptoms:** Corrupted transfer; random NAKs; graph commands interleaving; Nextion receives malformed packets.
- **Root cause:** Without a TX mutex/queue, multiple tasks can interleave bytes on the UART TX line.
- **Blast radius:** Saving programs; also any other UI update during a save.
- **Mitigation:** Single TX lock/queue in transport layer (ties to R-006).

## R-020 — Filename sanitization can cause collisions and overwrite surprises
**Area:** storage correctness

- **Location:** `sanitize_filename()` keeps only alnum and turns space into `_`.
- **Trigger/Symptoms:** Two different names map to the same filename (e.g., "A-B" and "AB"), causing “already exists” or accidental overwrite behavior.
- **Root cause:** Sanitization is many-to-one mapping.
- **Blast radius:** Program storage integrity.
- **Mitigation:**
  - Show the sanitized filename to the user, or
  - incorporate a stable suffix (hash/ID), or
  - expand allowed characters safely.

## R-021 — Overwrite check compares unsanitized names, not filenames
**Area:** storage authorization

- **Location:** overwrite gate uses `strcmp(draft->name, original_name) == 0`.
- **Trigger/Symptoms:**
  - User can be blocked from overwriting the same underlying file if name formatting differs (spaces/case), or
  - edge cases where different display names sanitize to same filename.
- **Root cause:** Storage is keyed by sanitized filename, but overwrite permission is keyed by raw name string.
- **Blast radius:** Editing/saving existing programs.
- **Mitigation:** Compare sanitized filenames for overwrite decisions.

## R-022 — Static payload buffer is not re-entrant
**Area:** concurrency / future-proofing

- **Location:** `nextion_storage_save_program()` uses `static char payload[PROGRAM_FILE_SIZE];`.
- **Trigger/Symptoms:** If save is ever called concurrently (or re-entered), payload contents corrupt.
- **Root cause:** Shared static buffer.
- **Blast radius:** Saving programs.
- **Mitigation:** Allocate payload on heap per save (or protect with a mutex).

## R-023 — Delete/save does not verify Nextion command success
**Area:** silent failure

- **Location:** `delfile` commands are sent with delays but no parsed response/ACK.
- **Trigger/Symptoms:** File still exists after delete; save behaves oddly.
- **Root cause:** Nextion may reject commands but firmware assumes success.
- **Blast radius:** Storage consistency and UI browser refresh.
- **Mitigation:** Parse and validate responses for critical commands (create/delete).

## R-024 — Transport layer does not serialize UART TX
**Area:** UART TX integrity

- **Location:** `main/nextion_transport.c` (`nextion_send_cmd`, `nextion_send_raw`) are callable from multiple tasks.
- **Trigger/Symptoms:**
  - Random corrupted commands (especially during graph render + save/file ops).
  - TWFILE transfers fail with NAKs for “no reason”.
- **Root cause:** `uart_write_bytes()` calls from different FreeRTOS tasks can interleave bytes unless protected.
- **Blast radius:** Everything that talks to Nextion.
- **Mitigation:** Add a TX mutex (or single TX queue/task) inside `nextion_transport.c` and lock around every write.

## R-025 — `uart_write_bytes()` return value ignored (partial writes not detected)
**Area:** reliability under load

- **Location:** `main/nextion_transport.c`.
- **Trigger/Symptoms:**
  - Commands sometimes have missing bytes or terminator; Nextion ignores them.
- **Root cause:** `uart_write_bytes()` returns number of bytes queued/written; code does not check it.
- **Blast radius:** Any command, but long bursts are most sensitive (graph `add`, TWFILE header+data).
- **Mitigation:** Check return values and log/retry or fail fast for critical transfers.

## R-026 — Error message text is not escaped and can break Nextion commands
**Area:** UI correctness / robustness

- **Location:** `main/nextion_ui.c` (`nextion_show_error`) builds `errText.txt="%s"` from raw `message`.
- **Trigger/Symptoms:**
  - Error popup sometimes doesn’t show.
  - After certain errors, subsequent UI commands misbehave.
- **Root cause:** If `message` contains `"`, `\\`, or newline characters, the generated Nextion command becomes syntactically invalid.
- **Blast radius:** Error display; can also desynchronize command parsing if the display interprets broken strings.
- **Mitigation:** Escape strings before sending or use a chunked/escaped setter (ties to R-011).

## R-027 — Error command buffer can truncate mid-quote
**Area:** silent UI failure

- **Location:** `main/nextion_ui.c` uses `char cmd[96]; snprintf(cmd, ..., "errText.txt=\"%s\"", message)`.
- **Trigger/Symptoms:** Long errors produce no text or garbled text.
- **Root cause:** Truncation can drop the closing quote; Nextion ignores malformed commands.
- **Blast radius:** Error visibility during debugging (high pain factor).
- **Mitigation:** Use a chunked setter (like the one in `nextion_events.c`) or increase buffer + check `snprintf` return.

## R-028 — Graph downsampling overwrites buckets (last sample wins)
**Area:** graph fidelity

- **Location:** `main/program_graph.c` (`program_build_graph`) maps many logical points into fewer pixel buckets when `total_points > width_px`.
- **Trigger/Symptoms:** Graph looks jagged or “wrong” for fast ramps; fine details disappear.
- **Root cause:** When multiple points fall into the same `bucket`, the code overwrites `out[bucket]` each time; only the last point is preserved (nearest-neighbor style downsample).
- **Blast radius:** UI graph correctness only (not core control logic).
- **Mitigation:** Use min/max aggregation per bucket (envelope), or average, or render at full width.

## R-029 — Caller can pass inconsistent `width_px` vs `max_len`
**Area:** graph correctness / partial rendering

- **Location:** `main/program_graph.c` uses `width_px` for bucket limits and `max_len` for output buffer bounds.
- **Trigger/Symptoms:** Graph partially drawn or clipped unexpectedly.
- **Root cause:** If `width_px > max_len`, buckets can exceed buffer and the function returns early; if `width_px < max_len`, output may only use first `width_px` entries.
- **Blast radius:** Graph display only.
- **Mitigation:** Define a single contract: `max_len` should be >= `width_px`, or drop one parameter.

## R-030 — Likely wrong graph dimension passed at call site
**Area:** integration bug suspicion

- **Location:** `main/nextion_events.c` calls `program_build_graph(..., width_px=PROGRAMS_GRAPH_HEIGHT, ...)` in one place.
- **Trigger/Symptoms:** Programs-page graph appears compressed/low-resolution horizontally.
- **Root cause:** Passing height where width is expected changes bucket calculation.
- **Blast radius:** Programs page graph only.
- **Mitigation:** Audit all `program_build_graph` call sites; ensure `width_px` matches the waveform’s horizontal resolution.

## R-031 — Global mutable model state is not thread-safe
**Area:** program model integrity

- **Location:** `main/program_models.c` uses file-static globals `s_program_draft`, `s_current_temp_c`, `s_current_kw`.
- **Trigger/Symptoms:**
  - Rare/Heisenbug: graph renders with partially updated stages.
  - Validation passes/fails inconsistently under heavy UI activity.
- **Root cause:** Multiple FreeRTOS tasks can read/write the same globals with no mutex/critical section. `program_draft_get()` exposes a pointer to shared mutable state.
- **Blast radius:** Programs page UI, graph building, save/validation flows.
- **Mitigation:**
  - Protect writes/reads with a mutex or critical section, or
  - snapshot-copy the draft before using it in long operations (graph build/save), or
  - run all draft mutations on one task and communicate via queues.

## R-032 — Draft name truncation can cause identity mismatches
**Area:** program identity / storage

- **Location:** `ProgramDraft.name[32]` in `main/program_models.h` and `program_draft_set_name()` truncates.
- **Trigger/Symptoms:**
  - Two different long names become the same 31-char prefix.
  - Save/edit/delete checks behave unexpectedly.
- **Root cause:** Truncation is many-to-one mapping; other code (storage sanitization) may apply additional transformations.
- **Blast radius:** Program selection/edit/save/delete.
- **Mitigation:** Define a single max-length and enforce it consistently in UI + storage; consider storing a separate stable ID.

## R-033 — No validation in setter APIs (invalid values can enter draft)
**Area:** correctness boundaries

- **Location:** `program_draft_set_stage()` accepts any integers and just stores them.
- **Trigger/Symptoms:**
  - Negative times, unrealistic temperatures, etc. can exist in the draft until later validation.
- **Root cause:** This module is a plain data store; validation is handled elsewhere.
- **Blast radius:** Downstream modules that assume values are sane (graph building may behave oddly).
- **Mitigation:**
  - Keep it as a pure store (OK), but ensure every “use” path validates or clamps as needed.
  - Or add optional defensive checks (but avoid duplicating domain validation rules).

## R-034 — Validation depends on global current temperature (non-deterministic)
**Area:** domain validation determinism

- **Location:** `main/program_validation.c` uses `program_get_current_temp_c()` as the starting temperature.
- **Trigger/Symptoms:** Same program draft sometimes validates and sometimes fails if current temperature changes between edits/saves.
- **Root cause:** A program’s mathematical consistency is evaluated relative to a mutable global state (current temp).
- **Blast radius:** Save/validate flow and graph correctness.
- **Mitigation:** Make the start temperature an explicit input to validation (snapshot it at the time user validates/saves).

## R-035 — Program name rules differ across validation vs storage
**Area:** user-facing inconsistency

- **Location:**
  - `main/program_validation.c` allows only letters/numbers and space, and rejects commas.
  - `main/nextion_storage.c` sanitizes names by stripping non-alnum and converting space to `_`.
- **Trigger/Symptoms:**
  - User sees name accepted but resulting filename differs unexpectedly, or
  - user sees “name already exists” due to sanitization collisions.
- **Root cause:** Two different “name policy” implementations.
- **Blast radius:** Save/edit/delete UX and storage integrity.
- **Mitigation:** Define one policy and reuse it for (1) UI acceptance, (2) filename mapping, and (3) overwrite checks.

## R-036 — Integer math truncation can reject near-valid inputs
**Area:** validation strictness / false negatives

- **Location:** `validate_stage_math()` computes `expected_temp = expected_temp_x10 / 10` (integer division truncation).
- **Trigger/Symptoms:**
  - Inputs that are mathematically close can fail validation unless tolerances are generous.
- **Root cause:** Fixed-point math is correct, but conversion to whole degrees truncates, which can bias results.
- **Blast radius:** Validation results shown to user.
- **Mitigation:** Compare using x10 consistently (compare `expected_temp_x10` to `target_temp_c * 10` with x10 tolerance).

## R-037 — Potential overflow if values grow beyond expected limits
**Area:** math safety

- **Location:** `validate_stage_math()` uses expressions like `(delta_t_x10 * t_min)` and `(start_temp_c * 10)` in `int`.
- **Trigger/Symptoms:** If configuration limits increase greatly, validation can behave incorrectly.
- **Root cause:** 32-bit `int` overflow.
- **Blast radius:** Validation correctness.
- **Mitigation:** Use `int64_t` for intermediate calculations in validation.

---

## Notes / connections to watch
- R-001 + R-002 often appear together: contention can cause partial lines; partial lines increase odds of overflow; overflow hides the original root cause.
- When debugging event issues, first confirm: (1) no contention (R-001), (2) no overflow logs (R-002), (3) `\r\n` termination (R-003).
