# Production component instructions

These instructions apply under `components/` in addition to the repository root guide.

- Public APIs belong in `include/`; private state and helpers belong in `src/`.
- Keep explicit component source lists; do not add broad source globs.
- Check every ESP-IDF/FreeRTOS creation call and unwind partial initialization in reverse order.
- For any task, callback, timer, queue, semaphore, mutex, event, or shared variable change, document execution context, owner, synchronization, blocking, and shutdown behavior.
- For any temperature, profile, PID, fan, relay, SSR, GPIO, watchdog, or fault change, invoke the appropriate safety/concurrency/control reviewer before handoff.
- Update `docs/codex/REPOSITORY_MAP.md` and `docs/codex/repository-map.yaml` only when the architecture or runtime map changes.
