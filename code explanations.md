# Code Explanations (Furnace Firmware)

Goal: provide a readable, function-by-function explanation of the codebase so it’s easy to merge additional work safely.

## Walkthrough Order (Plan)

We’ll go in dependency order (bottom-up), so each later module builds on concepts we already covered:

1. **Top-level entrypoints & build wiring**
   - `main/` (app_main init flow)
   - Component `CMakeLists.txt` dependencies (who depends on whom)
   - Kconfig highlights (runtime configuration knobs)

2. **common/** (shared types + helpers)
   - Error/type conventions and utility helpers used everywhere

3. **logger_component/** (logging primitives)
   - Init, threading model, log macros usage

4. **error_manager/** + core error patterns
   - How errors are represented and propagated

5. **event_manager/** (event bus)
   - Event loop(s), registry, post/subscribe patterns

6. **Low-level drivers / HAL-ish components**
   - `gpio_master_driver/`
   - `spi_master_component/`

7. **Temperature pipeline: acquire → publish**
   - `temperature_monitor_component/` (sensor IO, sampling tasks, ring buffer)

8. **Temperature pipeline: consume → process**
   - `temperature_processor_component/` (filtering/aggregation + events)

9. **Control pipeline: decide → actuate**
   - `temperature_profile_controller/` (setpoint scheduling / interpolation)
   - `pid_component/` (controller math)
   - `heater_controller_component/` (GPIO/PWM output + safety)

10. **System orchestration & safety**

- `coordinator_component/` (ties everything together; owns main state machine)
- `health_monitor/` (heartbeats, fault detection, self-reporting)

---

## Notes/Conventions We’ll Use

- For each file we cover:
  - **Purpose**: what this module/file is responsible for
  - **Key data types**: structs/enums and what they represent
  - **Key functions**: each function’s inputs/outputs, side effects, and how it’s used
  - **Runtime behavior**: tasks, event loops, queues, timers
  - **Dependencies**: what it requires and what requires it

---

## Status

- Document created. Next step: start with `main/main.c` and the init flow.

---

## main/

### Purpose

`main/` is the ESP-IDF application entrypoint component. It contains:

- The firmware entry function `app_main()`.
- Build wiring that tells ESP-IDF which source files belong to the “main” component and which other components it depends on.
- A small project-level Kconfig menu (currently only an “Alarm Controller” GPIO setting).

### main/CMakeLists.txt

ESP-IDF treats each folder with an `idf_component_register(...)` as a “component”. This file registers the `main` component.

Key parts:

- `SRCS "main.c"`: only `main.c` is compiled for this component.
- `INCLUDE_DIRS "."`: headers in `main/` are available to this component.
- `REQUIRES ...`: declares compile/link dependencies on other components.

The dependency list here is important because it’s effectively the “who we rely on” list for `app_main()`:

- `common` (shared helpers like `CHECK_ERR_LOG`)
- `logger_component` (logging macros and `logger_init()`)
- `event_manager` (global event loop and publish/subscribe API)
- `heater_controller_component`, `coordinator_component`, `temperature_monitor_component`, `temperature_processor_component`, `health_monitor`

### main/Kconfig.projbuild

Adds a config menu to `menuconfig`:

- `CONFIG_ALARM_CONTROLLER_GPIO` (int, default 26)

Notes:

- This setting is _defined_ here but not referenced in `main.c` yet.
- The intended use is likely in an “alarm controller” component or within a safety subsystem (e.g., drive a buzzer/LED on fault).

### main/main.c

#### File-level elements

- `static const char* TAG = "main";`
  - A logging “tag” used by the logger macros. Many components define their own `TAG` string so logs are easy to filter.

Includes:

- `logger_component.h`: provides `logger_init()` and `LOGGER_LOG_*` macros.
- `event_manager.h`: provides `event_manager_init()`.
- `utils.h` (from `components/common/include/utils.h`): provides the `CHECK_ERR_LOG*` macros.
- Other component headers provide init functions + config types used below.

#### Function: app_main(void)

This is ESP-IDF’s application entrypoint. It runs after ESP-IDF bootstraps the RTOS.

High-level behavior:

1. Initialize foundational infrastructure (logging, event manager).
2. Initialize functional components (temperature monitoring, processing, coordinator, health monitor).
3. Log “System initialized successfully”.
4. Go idle forever (10-second delay loop) while the real work happens in component tasks/event handlers.

Step-by-step:

1. `logger_init();`

- Initializes the logging subsystem.
- In this codebase, the logging macros (`LOGGER_LOG_INFO`, etc.) ultimately call `logger_send(...)` only if `CONFIG_LOG_ENABLE` is on and `CONFIG_LOG_LEVEL` allows it.
- Note: `logger_init()` does not return an error code here. If logger init can fail, it’s not being checked in `main.c`.

2. `CHECK_ERR_LOG(event_manager_init(), "Failed to initialize event manager");`

- `event_manager_init()` returns `esp_err_t`.
- `CHECK_ERR_LOG(expr, msg)` evaluates `expr`, and if it’s not `ESP_OK`, logs an error using `LOGGER_LOG_ERROR(TAG, ...)`.
- Important nuance: `CHECK_ERR_LOG` **does not stop boot**. It logs and continues.
  - That means if `event_manager_init()` fails, later components may still attempt to register handlers/post events and behave unpredictably.
  - If the event manager is truly required, `CHECK_ERR_LOG_RET` or a “fail fast” path may be safer.

3. Temperature monitor setup:

```c
temp_monitor_config_t temp_monitor_config = {
      .number_of_attached_sensors = 5
};
CHECK_ERR_LOG(init_temp_monitor(&temp_monitor_config), ...);
```

- Creates a local configuration struct for the temperature monitor and sets `number_of_attached_sensors` to 5.
- Calls `init_temp_monitor(&temp_monitor_config)` to bring up the sensor acquisition subsystem.
- Again: failure is only logged; boot continues.

4. Temperature processor init:
   `CHECK_ERR_LOG(init_temp_processor(), "Failed to initialize temperature processor");`

- Brings up the component that consumes raw temperature readings and produces processed values (averages/filters/etc.).
- Called after `init_temp_monitor()` because it likely depends on monitor events/data.

5. Coordinator init:

```c
const coordinator_config_t coordinator_config = {
      .profiles = NULL,
      .num_profiles = 0
};
CHECK_ERR_LOG(init_coordinator(&coordinator_config), ...);
```

- Initializes the system orchestrator.
- Configuration currently supplies **no heating profiles** (`profiles = NULL`, `num_profiles = 0`).
  - This is a placeholder configuration; in a real furnace run you’d likely provide at least one profile or a manual mode.

6. Health monitor init:
   `CHECK_ERR_LOG(init_health_monitor(), "Failed to initialize health monitor");`

- Brings up health monitoring (heartbeats, fault detection, etc.).
- Called late in the sequence; depending on design, you might want it earlier (so it can observe init failures).

7. Success log:
   `LOGGER_LOG_INFO(TAG, "System initialized successfully");`

- Logged if logging is enabled and log level is at least INFO.

8. Idle loop:

```c
while (1) {
      vTaskDelay(pdMS_TO_TICKS(10000));
}
```

- `app_main()` never returns; it sleeps forever.
- This is typical in ESP-IDF when the real work happens in other FreeRTOS tasks created by components.

#### Cross-cutting detail: CHECK_ERR_LOG macro

From `components/common/include/utils.h`:

- It relies on a `TAG` symbol being in scope (so each C file that uses it should define `static const char* TAG = ...`).
- It converts ESP-IDF error codes into readable text using `esp_err_to_name(_ret)`.
- Variants exist for “log + return”, “log + action”, and formatted messages.

#### Practical implications for merging your code later

- If you add required components, consider whether failures should abort boot (switch to `CHECK_ERR_LOG_RET` or a structured init state machine).
- If you add profile support, `coordinator_config_t` is the obvious integration point: populate `profiles` and `num_profiles`.
- If you add new Kconfig options, `main/Kconfig.projbuild` is one place to expose project-level knobs (but per-component Kconfig is often cleaner).

---

## components/common/

### Purpose

`components/common/` is a “shared toolbox” component: small, cross-cutting type definitions and macros that many other components include.

This folder currently contains three headers:

- `include/core_types.h`: domain model types for heating profiles.
- `include/furnace_error_types.h`: a common, component-agnostic error struct.
- `include/utils.h`: error-checking/logging convenience macros.

### components/common/CMakeLists.txt

This registers the component and declares dependencies:

- `INCLUDE_DIRS "include"`: exports `components/common/include` as an include path.
- `PRIV_REQUIRES esp_common logger_component`: internal dependencies used by `common` itself.
- `REQUIRES esp_event`: public dependency — anything that depends on `common` will also get `esp_event` available.

Notes:

- Today, `core_types.h` includes `esp_event.h` but doesn’t use it directly; that might be a leftover include or for future extension.

### include/core_types.h

#### Purpose

Defines the core data types representing a heating profile as a linked list of “nodes”. A profile is a named sequence of nodes, where each node describes a target temperature and how long to hold/transition.

#### Type: node_type_t

```c
typedef enum {
   NODE_TYPE_LOG,
   NODE_TYPE_LINEAR,
   NODE_TYPE_SQUARE,
   NODE_TYPE_CUBE
} node_type_t;
```

Meaning:

- This enum encodes the _shape_ of the transition between nodes (or a curve used within a node), e.g. linear ramp vs logarithmic/squared/cubed curve.
- The exact math is not implemented here; other modules (e.g., `temperature_profile_controller`) are expected to interpret these.

#### Type: heating_node_t

```c
typedef struct heating_node {
   node_type_t type;
   struct heating_node *next_node;
   struct heating_node *previous_node;
   float set_temp;
   uint32_t duration_ms;
   char *expression;
} heating_node_t;
```

Meaning of fields:

- `type`: how this node’s setpoint should be interpreted (curve type).
- `next_node` / `previous_node`: **doubly-linked list** pointers.
  - This allows forward and backward traversal, which can help for profile editing or interpolations that need context.
- `set_temp`: setpoint temperature for this node (float; units are implied to be °C but not stated here).
- `duration_ms`: how long this node lasts.
- `expression`: optional string expression.
  - This suggests a future/advanced mode where a node may describe a setpoint curve via a parsed expression.
  - As a plain `char*`, ownership rules matter (who allocates/frees it). That is not defined here.

Important implications:

- Because this is a linked structure with pointers, profiles are likely allocated dynamically or assembled at runtime.
- Any code that copies a `heating_node_t` by value risks shallow-copy pointer bugs.
- Thread-safety is not implied; if profiles can be modified while in use, access must be synchronized.

#### Type: heating_profile_t

```c
typedef struct {
   char *name;
   heating_node_t *first_node;
} heating_profile_t;
```

Meaning:

- `name`: profile name (string pointer; ownership not specified).
- `first_node`: head of the linked list of nodes.

How it’s used from `main.c`:

- `coordinator_config_t` in `main.c` currently passes `profiles = NULL` and `num_profiles = 0`, so profiles are “not configured yet” at the top-level.

### include/furnace_error_types.h

#### Purpose

Defines a standard error payload (`furnace_error_t`) that can be passed around (likely via events) regardless of which component detected the issue.

#### Type: furnace_error_severity_t

```c
typedef enum {
   SEVERITY_INFO,
   SEVERITY_WARNING,
   SEVERITY_ERROR,
   SEVERITY_CRITICAL
} furnace_error_severity_t;
```

Meaning:

- A coarse severity classification.
- The expectation is that severity influences what the system does (log only vs alarm vs emergency shutdown).

#### Type: furnace_error_source_t

```c
typedef enum {
   SOURCE_TEMP_PROCESSOR,
   SOURCE_TEMP_MONITOR,
   SOURCE_HEATER_CONTROLLER,
   SOURCE_COORDINATOR,
   SOURCE_PID_CONTROLLER,
   SOURCE_SPI_MASTER,
   SOURCE_LOGGER,
   SOURCE_UNKNOWN_COMPONENT
} furnace_error_source_t;
```

Meaning:

- Identifies _which subsystem_ raised the error.
- This is useful for dashboards/logging and for routing decisions (e.g., heater fault → immediate disable).

#### Type: furnace_error_t

```c
typedef struct {
   furnace_error_severity_t severity;
   furnace_error_source_t source;
   uint32_t error_code;
} furnace_error_t;
```

Meaning:

- `severity`: how bad.
- `source`: where it came from.
- `error_code`: component-defined numeric code.

Notes/implications:

- This intentionally does _not_ use `esp_err_t` directly. That allows components to define richer, domain-specific codes while still being able to carry ESP-IDF codes when needed.
- There’s no text/message field; human-readable details are expected to be logged separately (or looked up elsewhere).

### include/utils.h

#### Purpose

Defines convenience macros to reduce boilerplate around ESP-IDF error handling (check `esp_err_t`, log if not OK, optionally return).

All macros share these traits:

- They evaluate an expression that returns `esp_err_t`.
- On failure, they log `msg` (or formatted text) plus the ESP-IDF error name (`esp_err_to_name(_ret)`).
- They rely on a `TAG` identifier in the current translation unit.

#### Macro: CHECK_ERR_LOG(expr, msg)

Behavior:

- If `expr != ESP_OK`, logs an error and continues.

When to use:

- Non-fatal failures or best-effort initialization.

#### Macro: CHECK_ERR_LOG_RET(expr, msg)

Behavior:

- If `expr != ESP_OK`, logs an error and returns `_ret` from the current function.

When to use:

- Fail-fast situations where the current function cannot proceed safely.

#### Macro: CHECK_ERR_LOG_CALL / CHECK_ERR_LOG_CALL_RET

Behavior:

- Same as the non-CALL versions, but executes an `action` statement on failure.

Typical patterns:

- Cleanup: `action` could free memory, close a handle, or reset state.
- Safety: `action` could disable outputs before returning.

#### Formatted variants

- `CHECK_ERR_LOG_FMT`, `CHECK_ERR_LOG_RET_FMT`, `CHECK_ERR_LOG_CALL_RET_FMT`

These allow formatting additional context, e.g., including an index or parameter value.

Important caution:

- The `*_FMT` macros require you to supply `fmt` and matching `__VA_ARGS__`. If you pass a format string with no additional args, it will not compile.

---

## components/logger_component/

### Purpose

Provides a lightweight, thread-safe logging abstraction over ESP-IDF’s `ESP_LOGx` macros.

Core idea:

- Call sites (any task) format a log message and enqueue it.
- A dedicated logger task dequeues messages and prints them using `ESP_LOGI/W/E/D`.

This avoids multiple tasks printing simultaneously and gives a single choke point for log throughput.

### components/logger_component/CMakeLists.txt

- Uses `file(GLOB ...)` to compile all `src/*.c` files.
- Exports `include/` so other components can include the public header.

### components/logger_component/Kconfig

Defines menuconfig options that become `sdkconfig` macros:

- `CONFIG_LOG_ENABLE` (bool): enables/disables the logging macros.
- `CONFIG_LOG_LEVEL` (int): compile-time threshold checked by the wrapper macros.
- `CONFIG_LOG_QUEUE_SIZE` (int): FreeRTOS queue length for pending log messages.
- `CONFIG_LOG_MAX_MESSAGE_LENGTH` (int): size of the fixed buffer in each queued message.

Important nuance:

- `CONFIG_LOG_MAX_MESSAGE_LENGTH` controls **queue item size**. Large values increase RAM use because each queue entry stores a full `log_message_t`.
  - Rough memory estimate: `CONFIG_LOG_QUEUE_SIZE * sizeof(log_message_t)`.

### include/logger_component.h

#### Type: log_level_t

```c
typedef enum {
      LOG_LEVEL_NONE = 0,
      LOG_LEVEL_ERROR,
      LOG_LEVEL_WARN,
      LOG_LEVEL_INFO,
      LOG_LEVEL_DEBUG
} log_level_t;
```

Meaning:

- This project’s internal log levels.

Note: Kconfig’s help text says “0=ERROR ... 4=VERBOSE”, but the enum here does **not** include VERBOSE and `0` is `LOG_LEVEL_NONE`. In practice:

- `CONFIG_LOG_LEVEL` is compared against `LOG_LEVEL_*` values in the wrapper macros.
- So `CONFIG_LOG_LEVEL=0` will suppress everything (because it’s not >= ERROR).

#### Macros: LOGGER_LOG_INFO/WARN/ERROR/DEBUG

Behavior:

- If `CONFIG_LOG_ENABLE` is off, they compile to no-ops.
- If enabled, they check `CONFIG_LOG_LEVEL` and only then call `logger_send(level, tag, fmt, ...)`.

This means:

- The log-level filtering happens **at the call site**.
- Even filtered-out logs do not pay the cost of formatting/enqueueing.

#### Type: log_message_t

```c
typedef struct {
      const char *tag;
      char message[CONFIG_LOG_MAX_MESSAGE_LENGTH];
      log_level_t level;
} log_message_t;
```

Meaning:

- Each queued item stores the `tag` pointer (not copied), a fixed-size formatted message buffer, and the severity.

Implications:

- `tag` should point to a string with static lifetime (typical usage: `static const char* TAG = "...";`).
- The message is copied into the queue item, so callers can pass stack/local formatting arguments safely.

#### Public API

- `void logger_init(void);`
  - One-time init; creates queue and spawns logger task.
- `void logger_send(log_level_t log_level, const char *tag, const char *message, ...);`
  - Formats and enqueues a message.

### src/logger_component.c

#### File-level state

- `static QueueHandle_t logger_queue;`
  - Global queue used by all call sites.
- `static bool logger_initialized = false;`
  - Ensures `logger_init()` is idempotent.

#### Internal type: LoggerConfig_t

```c
typedef struct {
      const char *task_name;
      uint32_t stack_size;
      UBaseType_t task_priority;
} LoggerConfig_t;
```

And a static config instance:

- Task name: `LOGGER_TASK`
- Stack size: 4096 bytes
- Priority: 4

This is the runtime configuration of the logger task (not exposed via Kconfig).

#### Function: logger_task(void \*args)

Role:

- Blocks forever on `xQueueReceive(logger_queue, &msg, portMAX_DELAY)`.
- For each received message, prints via the corresponding `ESP_LOGx` call.

Level mapping:

- `LOG_LEVEL_INFO` → `ESP_LOGI`
- `LOG_LEVEL_WARN` → `ESP_LOGW`
- `LOG_LEVEL_ERROR` → `ESP_LOGE`
- `LOG_LEVEL_DEBUG` → `ESP_LOGD`
- default → `ESP_LOGI`

Notes:

- This uses ESP-IDF logging as the final sink, so it will still be affected by ESP-IDF’s own log configuration (UART, timestamps, etc.).

#### Function: logger_init(void)

Role:

- If already initialized, returns immediately.
- Creates a queue: `xQueueCreate(CONFIG_LOG_QUEUE_SIZE, sizeof(log_message_t))`.
  - On failure: logs via `ESP_LOGE(TAG, ...)` and returns (logger remains uninitialized).
- Creates the logger task via `xTaskCreate(...)`.
- Marks `logger_initialized = true`.

Important nuance:

- There’s no error return, so callers can’t directly react to init failures.
- If task creation fails, the code currently still sets `logger_initialized = true` (because it doesn’t check the return value of `xTaskCreate`).
  - That would lead to a “stuck” state where the queue exists but nothing consumes it.

#### Function: logger_send(log_level_t log_level, const char *tag, const char *fmt, ...)

Role:

- If `logger_queue == NULL`, logs a warning (`ESP_LOGW`) and returns.
- Builds a `log_message_t` on the stack.
- Formats text into `msg.message` using `vsnprintf`.
- Tries to enqueue with a short timeout (`pdMS_TO_TICKS(10)`).
  - If the queue is full, logs a warning and drops the message.

Why the queue can fill:

- Burst logging from multiple tasks.
- The logger task being starved (priority too low or CPU pinned elsewhere).
- The sink (UART) being slow.

Practical implications for merging your code later:

- If you add high-rate telemetry logs, tune `CONFIG_LOG_QUEUE_SIZE` and message size, or you’ll see “queue full” drops.
- Prefer static tags (file-level `TAG`) so the pointer stays valid.

---

## components/event_manager/

### Purpose

Provides a single, project-wide event bus built on ESP-IDF’s `esp_event` library.

Core idea:

- The project creates **one global event loop**.
- Components subscribe handlers to event bases + event IDs.
- Components post events into the loop (with optional payload).

This creates loose coupling: publishers don’t need to know who consumes an event.

### components/event_manager/CMakeLists.txt

- Compiles all `src/*.c`.
- `REQUIRES esp_event`: public dependency on ESP-IDF event system.
- `PRIV_REQUIRES esp_common logger_component common`: internal dependencies for logging and helper macros.

### components/event_manager/Kconfig

Defines event-loop tuning knobs:

- `CONFIG_EVENT_MANAGER_QUEUE_SIZE` (default 32): queue depth for the event loop.
- `CONFIG_EVENT_MANAGER_TASK_STACK_SIZE` (default 4096)
- `CONFIG_EVENT_MANAGER_TASK_PRIORITY` (default 5)
- `CONFIG_EVENT_MANAGER_TASK_NAME` (default "event_manager_task")

Important nuance:

- In the current implementation, only `queue_size` and `task_name` are actually used when creating the loop.
- Stack size and priority config exist but are not wired into `esp_event_loop_args_t` (see below).

### include/event_registry.h

#### Purpose

Defines the “schema” of the event bus:

- Event bases (top-level categories)
- Event IDs for each base
- Payload structs/enums for events that carry data

This is effectively the shared contract between publishers and subscribers.

#### Event base concept

In ESP-IDF, an event is identified by:

- `esp_event_base_t event_base` (a unique base pointer)
- `int32_t event_id` within that base

This file declares bases using `ESP_EVENT_DECLARE_BASE(...)`.

#### COORDINATOR_EVENT

- Base: `COORDINATOR_EVENT`
- IDs: `coordinator_event_id_t`
  - Includes both “RX” (commands to coordinator) and “TX” (status/notifications from coordinator).

Notable payload types:

- `coordinator_start_profile_data_t` (contains `profile_index`)
- `coordinator_error_data_t` (contains a coordinator error code + an `esp_err_t`)
- `coordinator_status_data_t` (status snapshot)

#### HEATER_CONTROLLER_EVENT

- Base: `HEATER_CONTROLLER_EVENT`
- IDs: `heater_controller_event_t`
  - Includes set power, heater toggled, status report request/response, error.

#### HEALTH_MONITOR_EVENT

- Base: `HEALTH_MONITOR_EVENT`
- IDs: `health_monitor_event_id_t` currently defines `HEALTH_MONITOR_EVENT_HEARTBEAT`.

Also defines `health_monitor_component_id_t`:

```c
typedef enum {
      TEMP_MONITOR_EVENT_HEARTBEAT = 0,
      HEATER_CONTROLLER_EVENT_HEARTBEAT,
      COORDINATOR_EVENT_HEARTBEAT,
      TEMP_PROCESSOR_EVENT_HEARTBEAT,
} health_monitor_component_id_t;
```

Meaning:

- These values are used as “component IDs” when posting health/heartbeat events.
- Slight naming mismatch: enum values are called `*_EVENT_HEARTBEAT` but they function as component identifiers.

#### TEMP_PROCESSOR_EVENT

- Base declared: `TEMP_PROCESSOR_EVENT`
- Payload: `temp_processor_data_t` with `average_temperature` + `valid`

Important nuance:

- In the current `event_registry.c`, `TEMP_PROCESSOR_EVENT` is **declared** but not **defined** (see below). That will break linking if any code uses `TEMP_PROCESSOR_EVENT`.

#### FURNACE_ERROR_EVENT

- Base: `FURNACE_ERROR_EVENT`
- ID: `FURNACE_ERROR_EVENT_ID` (0)

This looks like a project-wide error publication channel.

#### Function: event_registry_init(void)

Comment says:

- “Must be called after `event_manager_init()` but before components use events”.

In the current implementation, it mostly logs and returns `ESP_OK`.

### src/event_registry.c

#### Purpose

Defines the declared event bases using `ESP_EVENT_DEFINE_BASE(...)`.

Current definitions:

- `COORDINATOR_EVENT`
- `HEATER_CONTROLLER_EVENT`
- `HEALTH_MONITOR_EVENT`
- `FURNACE_ERROR_EVENT`

Missing definition:

- `TEMP_PROCESSOR_EVENT` is not defined here, even though it’s declared in the registry header.

Function: `event_registry_init()`

- Logs “Event registry initialized” and a few debug lines.
- Returns `ESP_OK`.

### include/event_manager.h (public API)

This is the wrapper API components are expected to call.

Key functions:

---

## components/health_monitor/

### Purpose

Provides a **system health / liveness monitor** based on component heartbeats.

Core idea:

- Each component periodically posts a heartbeat into the event bus (via `event_manager_post_health(...)`).
- The health monitor subscribes to those heartbeats and tracks when each component was last seen.
- If a required component stops sending heartbeats for “too long” (or too many times), the health monitor marks the system unhealthy.
- When the system is healthy, the health monitor “feeds” an ESP-IDF **task watchdog**.
- If the system is unhealthy, it stops feeding the watchdog, which triggers a watchdog panic/reset (because `trigger_panic = true`).

This is a classic “fail fast if the system stops making progress” strategy.

### components/health_monitor/CMakeLists.txt

- Compiles all `src/*.c`.
- Depends on:
  - `event_manager` (subscribe to health events)
  - `common` (CHECK_ERR_LOG macros)
  - `logger_component` (logs)

### components/health_monitor/Kconfig

Key knobs:

- `CONFIG_HEARTH_BEAT_COUNT`: number of heartbeat entries tracked.
  - Typo note: this is spelled “HEARTH” in Kconfig.
- Task config: stack/priority/name
- `CONFIG_HEALTH_MONITOR_CHECK_INTERVAL_MS`: how often the monitor evaluates liveness.

Per-component thresholds (each has both):

- `*_MAX_SILENCE_MS`: maximum time since last heartbeat.
- `*_MAX_MISSES`: how many consecutive silence checks are tolerated.

### include/health_monitor.h (public API)

- `init_health_monitor()`
- `shutdown_health_monitor()`

### src/health_monitor_internal.h

#### Heartbeat state model

Each monitored component uses a `heartbeat_entry_t`:

- `last_seen_tick`: last tick when heartbeat arrived
- `max_silence_ticks`: threshold in ticks
- `miss_count`, `max_misses`
- `required`: if true, exceeding misses fails the system
- `state`: one of:
  - `HB_STATE_OK`
  - `HB_STATE_MISSED`
  - `HB_STATE_FAILED`
  - (`HB_STATE_LATE` exists but isn’t used in the current task logic)

The main context (`health_monitor_ctx_t`) contains:

- `heartbeat[CONFIG_HEARTH_BEAT_COUNT]`: fixed array indexed by `health_monitor_component_id_t`.
- `is_running`, `initialized`, and init flags for events/tasks.
- a FreeRTOS `task_handle`.

### src/health_monitor_core.c

#### Function: init_health_monitor(void)

Boot flow:

1. Allocates a singleton context (`calloc`).
2. Subscribes to health events (`init_health_monitor_events`).
3. Starts the monitor task (`init_health_monitor_task`).
4. Initializes heartbeat entries (`init_heartbeats`).

Important nuance:

- If `init_health_monitor()` is called more than once, it returns `ESP_OK` once initialized.
- Allocation is unconditional when ctx is NULL; it doesn’t reuse an existing non-initialized ctx.

#### Function: init_heartbeats(health_monitor_ctx_t\* ctx)

Initializes “last seen” timestamps and per-component thresholds.

It uses the `health_monitor_component_id_t` enum values (from `event_registry.h`) as array indices:

- `TEMP_MONITOR_EVENT_HEARTBEAT`
- `TEMP_PROCESSOR_EVENT_HEARTBEAT`
- `HEATER_CONTROLLER_EVENT_HEARTBEAT`
- `COORDINATOR_EVENT_HEARTBEAT`

Each is currently marked `required = true`.

Important nuance:

- `CONFIG_HEARTH_BEAT_COUNT` must be large enough to include the maximum component ID you index.
- The current Kconfig default is 5 while the enum currently lists 4 components (0..3), so it’s OK as-is.

#### Function: shutdown_health_monitor(void)

Stops events and task, then frees the context.

### src/health_monitor_events.c

#### Subscription model

Subscribes to `HEALTH_MONITOR_EVENT` (any ID). In practice it handles:

- `HEALTH_MONITOR_EVENT_HEARTBEAT`

#### Function: health_monitor_event_handler(...)

For `HEALTH_MONITOR_EVENT_HEARTBEAT`:

- Treats `event_data` as a `health_monitor_component_id_t*`.
- Validates the ID is in range.
- Updates `ctx->heartbeat[id].last_seen_tick = xTaskGetTickCount()`.

Important nuance / bug risk:

- There is no NULL check on `event_data` before dereferencing.
- The check `*component_id < 0` is meaningless for an enum stored as an unsigned/integer type; the upper bound check is the meaningful one.

### src/health_monitor_task.c

#### Watchdog strategy

The task uses the ESP-IDF task watchdog (`esp_task_wdt_*`).

Initialization:

- Calls `init_health_watchdog()`.
- The watchdog config uses:
  - `timeout_ms = 5000`
  - `trigger_panic = true`
  - `idle_core_mask` configured for all cores

Then the task registers itself:

- `esp_task_wdt_add(NULL)`

Runtime logic (every `CONFIG_HEALTH_MONITOR_CHECK_INTERVAL_MS`):

1. For each heartbeat entry, compute:
   $$silence = now - last\_seen$$
2. If `silence > max_silence_ticks`:
   - increment `miss_count` (up to `max_misses`)
   - if required and miss_count reaches max → `HB_STATE_FAILED` and mark system unhealthy
   - else → `HB_STATE_MISSED`
3. If within silence threshold:
   - reset miss_count to 0 and mark `HB_STATE_OK`
4. If system is healthy:
   - `esp_task_wdt_reset()` (feed watchdog)
     Else:
   - do not feed watchdog, allowing watchdog panic/reset.

Sleep behavior:

- It computes a periodic wait using `last_wake + period` and blocks using `ulTaskNotifyTake(...)` so shutdown can wake it.

Shutdown:

- `shutdown_health_monitor_task` sets `is_running = false` and notifies the task.
- Task unregisters from the watchdog and exits.

### Practical implications for merging your code later

- Health monitoring currently assumes “heartbeat missing → system reset” (hard-fail) for required components.
- Any component you add that should be monitored needs:
  - a new `health_monitor_component_id_t` value
  - `init_heartbeats(...)` entry + Kconfig thresholds
  - a periodic call to `event_manager_post_health(component_id)`
- If you want graceful degradation (e.g., heater off + error event instead of reset), this is where you’d change the policy (don’t rely solely on watchdog panic).
- `event_manager_init()` / `event_manager_shutdown()`
- `event_manager_subscribe()` / `event_manager_unsubscribe()`
- `event_manager_post()` and convenience wrappers:
  - `event_manager_post_immediate()` (timeout 0)
  - `event_manager_post_blocking()` (timeout `portMAX_DELAY`)
- `event_manager_post_health(health_monitor_component_id_t component_id)`

There is also a declared `event_manager_get_loop()` but it is not implemented in `src/event_manager.c`.

### src/event_manager.c

#### Internal state: g_event_manager_ctx

```c
typedef struct {
      esp_event_loop_handle_t event_loop_handle;
      bool is_initialized;
} event_manager_context_t;
```

Meaning:

- Holds the global event loop handle.
- Tracks init status.

#### Function: event_manager_init(void)

What it does:

- If already initialized: returns `ESP_OK`.
- Creates an event loop with `esp_event_loop_create(&loop_args, &handle)`.
- Sets `is_initialized = true`.
- Logs success.

Important nuance about loop args:

```c
esp_event_loop_args_t loop_args = {
      .queue_size = CONFIG_EVENT_MANAGER_QUEUE_SIZE,
      .task_name = event_manager_config.task_name
};
```

- Only queue size and task name are set.
- Task stack size and priority are not passed (even though Kconfig exposes them).
- Depending on ESP-IDF defaults and the struct definition, leaving other fields zero may mean:
  - default stack/priority, or
  - invalid config, depending on IDF version.

#### Function: event_manager_shutdown(void)

- If not initialized: returns `ESP_OK`.
- Calls `esp_event_loop_delete(handle)`.
- Clears handle and `is_initialized` flag.

#### Function: event_manager_subscribe(...)

- Wraps `esp_event_handler_register_with()` for the global loop.
- Logs subscription.

Note:

- This function assumes `event_manager_init()` has been called. If not, `event_loop_handle` will be NULL.

#### Function: event_manager_unsubscribe(...)

- Wraps `esp_event_handler_unregister_with()`.

#### Function: event_manager_post(...)

- Wraps `esp_event_post_to()`.
- Lets caller choose a timeout (`ticks_to_wait`) in case the event queue is full.

#### Function: event_manager_post_immediate / event_manager_post_blocking

- Convenience wrappers around `event_manager_post()`.

#### Function: event_manager_post_health(health_monitor_component_id_t component_id)

```c
return event_manager_post(
      HEALTH_MONITOR_EVENT,
      component_id,
      NULL,
      0,
      portMAX_DELAY);
```

Meaning:

- Posts into the `HEALTH_MONITOR_EVENT` base.
- Uses the enum value as the `event_id`.
- No payload.

### Key integration points / pitfalls to remember

- `event_registry_init()` is described as required by comments, but `main.c` currently does not call it.
- `event_manager_get_loop()` is declared but not implemented.
- `TEMP_PROCESSOR_EVENT` is declared but not defined in `event_registry.c`.
- Subscription/post calls don’t guard against an uninitialized loop handle; correct init ordering matters.

---

## components/spi_master_component/

### Purpose

Provides a simple SPI “bus + N chip-select devices” abstraction.

Core idea:

- Initialize the SPI bus once.
- Register up to N SPI slave devices (each with its own CS pin).
- Offer a single `spi_transfer()` API that is guarded by a mutex so multiple tasks can share the bus safely.

This is used as a hardware abstraction layer for components that need SPI (e.g., temperature sensors).

### components/spi_master_component/CMakeLists.txt

- Compiles all `src/*.c`.
- `PRIV_REQUIRES driver logger_component esp_common common`
  - Uses ESP-IDF `driver/spi_master.h`.
  - Uses `CHECK_ERR_LOG_*` from `common` and logging macros.

### components/spi_master_component/Kconfig

SPI configuration is exposed to menuconfig and becomes `sdkconfig` macros.

Bus settings:

- `CONFIG_SPI_BUS_MISO`, `CONFIG_SPI_BUS_MOSI`, `CONFIG_SPI_BUS_SCK`: bus pin mapping.
- `CONFIG_SPI_BUS_MODE` (0–3): clock polarity/phase.
- `CONFIG_SPI_CLOCK_SPEED_HZ`: per-device clock speed used when adding devices.
- `CONFIG_SPI_TRANSACTION_TIMEOUT_MS`: configured, but not currently used in code.
- `CONFIG_SPI_MAX_NUM_SLAVES` (1–9): compile-time maximum number of devices.
- `CONFIG_SPI_MAX_TRANSFER_SIZE`: configured, but not currently used in code.

CS pins:

- `CONFIG_SPI_SLAVE1_CS` … `CONFIG_SPI_SLAVE9_CS`: chip select pin per slave index.

Important nuance:

- The implementation uses `CONFIG_SPI_MAX_NUM_SLAVES` to size arrays and compile-time conditionally include CS pins.
- The runtime `init_spi(number_of_slaves)` tells it how many of those entries are actually active.

### include/spi_master_component.h

#### Function: init_spi(uint8_t number_of_slaves)

- Initializes the SPI bus and registers `number_of_slaves` devices.
- Returns `ESP_ERR_INVALID_ARG` if `number_of_slaves > CONFIG_SPI_MAX_NUM_SLAVES`.

#### Function: spi_transfer(int slave_index, const uint8_t *tx, uint8_t *rx, size_t len)

- Performs a blocking SPI transaction to the given slave index.
- `tx`/`rx` are the transmit/receive buffers.
- `len` is in bytes.

#### Function: shutdown_spi(void)

- Removes registered devices and frees the SPI bus.
- Deletes the mutex.

### src/spi_master_component.c

#### File-level state

- `spi_device_handle_t spi_slaves[CONFIG_SPI_MAX_NUM_SLAVES];`
  - Per-slave device handle returned by `spi_bus_add_device`.
- `static SemaphoreHandle_t spi_mutex = NULL;`
  - Ensures only one task uses the bus at a time.
- `static uint8_t _number_of_slaves;`
  - The active number of slaves passed to `init_spi()`.
- `static int cs_pins[CONFIG_SPI_MAX_NUM_SLAVES] = {...}`
  - CS pin lookup table built from Kconfig.
- `static bool spi_initialized = false;`

#### Internal config: SpiConfig_t

```c
typedef struct {
      int miso_io;
      int mosi_io;
      int sclk_io;
      int max_transfer_size;
      int clock_speed_hz;
      int mode;
      int queue_size;
} SpiConfig_t;
```

And a static instance:

- Uses Kconfig pins and mode/speed.
- Hard-codes `.max_transfer_size = 32` and `.queue_size = 1`.

Important nuance:

- Kconfig exposes `CONFIG_SPI_MAX_TRANSFER_SIZE` but the code uses a hard-coded 32 for both the bus config (`max_transfer_sz`) and `spi_config.max_transfer_size`.

#### Helper: add_spi_slave(int index)

Creates a `spi_device_interface_config_t` using:

- `clock_speed_hz` from Kconfig
- `mode` from Kconfig
- `spics_io_num = cs_pins[index]`
- `queue_size = 1`
  Then calls `spi_bus_add_device(HSPI_HOST, ...)`.

#### Function: init_spi(uint8_t number_of_slaves)

Step-by-step:

1. Validates requested slave count.
2. Stores `_number_of_slaves`.
3. If already initialized, returns `ESP_OK` (idempotent).
4. Creates a mutex if needed.
5. Builds `spi_bus_config_t`:
   - Uses configured MISO/MOSI/SCK.
   - Sets `max_transfer_sz = 32`.
6. Calls `spi_bus_initialize(HSPI_HOST, &buscfg, SPI_DMA_CH_AUTO)`.
7. For each slave `i` in `[0, number_of_slaves)`, calls `add_spi_slave(i)`.
8. Marks initialized.

Observations:

- Uses `HSPI_HOST` (SPI2 on ESP32). That’s a design choice; other systems might use `VSPI_HOST`.

#### Function: spi_transfer(int slave_index, const uint8_t* tx, uint8_t* rx, size_t len)

Step-by-step:

1. Validates `slave_index < _number_of_slaves`.
2. Takes `spi_mutex` with `portMAX_DELAY`.
3. Builds a `spi_transaction_t`:
   - `.length = len * 8` (ESP-IDF expects bits)
   - `.tx_buffer = tx`
   - `.rx_buffer = rx`
4. Calls `spi_device_transmit(spi_slaves[slave_index], &t)`.
5. Releases the mutex.

Important nuance:

- On transmit error, it returns early via `CHECK_ERR_LOG_RET(...)` and **does not release the mutex**.
  - That can deadlock all future SPI usage.
  - (This is documentation only; we can fix it later if you want.)

#### Function: shutdown_spi(void)

Step-by-step:

1. If not initialized, returns `ESP_OK`.
2. Loops over `CONFIG_SPI_MAX_NUM_SLAVES` and removes any non-NULL device handles.
3. Calls `spi_bus_free(HSPI_HOST)`.
4. Deletes the mutex.
5. Clears initialized flag.

Practical implications for merging your code later:

- Decide who “owns” calling `init_spi()` (temperature monitor likely) to avoid double-init ambiguity.
- If you add more SPI peripherals, bump `CONFIG_SPI_MAX_NUM_SLAVES` and set CS pins in menuconfig.
- Consider fixing the mutex-release-on-error behavior before relying on SPI for safety-critical control.

---

## components/gpio_master_driver/

### Purpose

Provides a simple, thread-safe wrapper around ESP-IDF’s GPIO driver.

Core idea:

- Centralize GPIO configuration and access behind a single API.
- Use a mutex so multiple tasks/components can safely configure pins and set/read levels.

This is especially relevant in a modular firmware where different components might touch GPIOs.

### components/gpio_master_driver/CMakeLists.txt

- Compiles all `src/*.c`.
- `REQUIRES driver`: uses ESP-IDF’s `driver/gpio.h` API.
- `PRIV_REQUIRES logger_component esp_common common`: uses logging macros and common utilities.

### include/gpio_master_driver.h

Public API functions:

- `esp_err_t gpio_master_driver_init(void);`
  - Creates the internal mutex.
- `esp_err_t gpio_master_set_pin_mode(int gpio_num, int mode, int pull_up, int pull_down);`
  - Configures a pin’s direction/mode and pull resistors.
- `esp_err_t gpio_master_set_level(int gpio_num, int level);`
  - Sets output level.
- `esp_err_t gpio_master_get_level(int gpio_num, int *level);`
  - Reads input level into `*level`.
- `esp_err_t gpio_master_deinit(void);`
  - Deletes the mutex.

Note:

- The API uses `int mode` rather than `gpio_mode_t`, so callers must pass ESP-IDF mode constants (e.g., `GPIO_MODE_OUTPUT`).

### src/gpio_master_driver.c

#### File-level state

- `static SemaphoreHandle_t gpio_mutex = NULL;`
  - A global mutex that protects calls into the GPIO driver.

#### Function: gpio_master_driver_init(void)

Behavior:

- If the mutex doesn’t exist, creates it with `xSemaphoreCreateMutex()`.
- If creation fails, logs and returns `ESP_FAIL`.
- Otherwise logs “initialized” and returns `ESP_OK`.

Design note:

- This init is idempotent-ish: calling it multiple times is safe (it only creates the mutex once).

#### Function: gpio_master_set_pin_mode(int gpio_num, int mode, int pull_up, int pull_down)

Behavior:

- If mutex is NULL → returns `ESP_ERR_INVALID_STATE` (driver not initialized).
- Builds a `gpio_config_t`:
  - `pin_bit_mask = (1ULL << gpio_num)`
  - `mode = mode`
  - pull-ups/downs enabled/disabled based on the int flags
  - interrupts disabled
- Takes the mutex, calls `gpio_config(&io_conf)`, releases the mutex.
- Returns whatever `gpio_config()` returns.

Implications:

- Configuration is serialized across tasks.
- This wrapper does not validate `gpio_num` range.

#### Function: gpio_master_set_level(int gpio_num, int level)

Behavior:

- Requires init (mutex exists).
- Takes mutex, calls `gpio_set_level(gpio_num, level)`, releases mutex.
- Returns the underlying `esp_err_t`.

#### Function: gpio_master_get_level(int gpio_num, int \*level)

Behavior:

- Requires init.
- Takes mutex, reads `gpio_get_level(gpio_num)`, releases mutex.
- If the read value is negative, returns `ESP_FAIL`.
- Otherwise writes to `*level` and returns `ESP_OK`.

Nuance:

- In ESP-IDF, `gpio_get_level()` typically returns 0 or 1; negative handling here is defensive.
- This function assumes `level` is non-NULL; it does not check for `NULL`.

#### Function: gpio_master_deinit(void)

Behavior:

- Deletes the mutex if it exists.
- Logs “deinitialized”.

Practical implications for merging your code later:

- If you build new components that touch GPIO, use this wrapper to avoid concurrent `gpio_config()` / `gpio_set_level()` calls.
- If you need pin interrupt support, this wrapper currently forces `GPIO_INTR_DISABLE`; you’d extend it (or add a separate API) to enable interrupts safely.

---

## components/temperature_monitor_component/

### Purpose

This component is responsible for **reading temperatures from RTD sensors** (via MAX31865 over SPI), packaging readings into samples, buffering them, and producing signals/events for downstream processing.

High-level pipeline:

1. `init_temp_monitor()` initializes SPI and the MAX31865 chips.
2. A FreeRTOS task (`TEMP_MONITOR_TASK`) runs periodically at `CONFIG_TEMP_SENSORS_SAMPLING_FREQ_HZ`.
3. Each tick, it reads all sensors, validates the sample, pushes it into a ring buffer, and maintains per-second “batch” stats.
4. Once enough samples are collected for the second, it sets an event-group bit (`TEMP_READY_EVENT_BIT`) so consumers can process a batch.
5. It also posts health/heartbeat events and publishes any errors as `FURNACE_ERROR_EVENT`.

### components/temperature_monitor_component/CMakeLists.txt

- Compiles all `src/*.c`.
- `PRIV_REQUIRES ... spi_master_component ... event_manager error_manager`
  - SPI is required to talk to MAX31865.
  - Event manager is used to publish `FURNACE_ERROR_EVENT` and health heartbeats.
  - Error manager is included; this component also builds error codes (see notes below).

### Kconfig (Temperature Sensors)

Key knobs (in `components/temperature_monitor_component/Kconfig`):

- `CONFIG_TEMP_SENSORS_MAX_SENSORS` (default 9): array sizing for per-sensor storage.
- `CONFIG_TEMP_SENSORS_SAMPLING_FREQ_HZ` (default 20): sample rate.
- `CONFIG_TEMP_SENSORS_MAXIMUM_BAD_SAMPLES_PER_BATCH_PERCENT` (default 30): per-second quality threshold.
- `CONFIG_TEMP_SENSORS_RING_BUFFER_SIZE` (default 32): sample history buffer depth.
- `CONFIG_TEMP_SENSORS_MAX_SENSOR_FAILURES` (default 5): escalation threshold for HW errors.

Read behavior:

- `CONFIG_TEMP_SENSOR_MAX_READ_RETRIES` / `CONFIG_TEMP_SENSOR_RETRY_DELAY_MS`: retry loop for failed reads.
- `CONFIG_TEMP_SENSOR_MAX_TEMPERATURE_C` (default 220): over-temperature threshold.
- `CONFIG_TEMP_DELTA_THRESHOLD` and `CONFIG_TEMP_SENSORS_HAVE_OUTLIERS_REJECTION`: configured, but not used in the code shown (likely planned feature).

### include/temperature_monitor_types.h

#### Type: max31865_fault_flags_t

Parsed view of the MAX31865 fault status byte.

#### Type: temp_sensor_t

Represents one sensor reading:

- `index`: sensor index.
- `temperature_c`: computed temperature.
- `valid`: whether the sensor data is considered valid.
- `raw_fault_byte`: raw MAX31865 fault status.
- `error`: `esp_err_t` describing SPI/read errors.

#### Type: temp_sample_t

Represents a “snapshot” for all sensors at a given time:

- `timestamp_ms`: present in struct, but not set in the shown code.
- `sensors[CONFIG_TEMP_SENSORS_MAX_SENSORS]`: per-sensor results.
- `number_of_attached_sensors`: how many entries are meaningful.
- `valid`: overall validity.
- `empty`: whether any sensors provided data.

### include/temperature_monitor_component.h (public API)

#### Event group model

- Defines `TEMP_READY_EVENT_BIT`.
- Exposes `EventGroupHandle_t temp_monitor_get_event_group(void);`
  - Downstream components can wait on `TEMP_READY_EVENT_BIT` to know when a “batch” (1 second) of samples is ready.

#### Config: temp_monitor_config_t

- Currently only `number_of_attached_sensors`.

#### Function: init_temp_monitor(temp_monitor_config_t \*config)

Initializes the temperature monitor subsystem.

#### Function: shutdown_temp_monitor(void)

Stops the task, deletes the event group, frees context, and shuts down SPI.

#### Function: temp_ring_buffer_pop_all(temp_sample_t \*out_dest, size_t max_out)

Public API to extract buffered samples (up to `max_out`) into a caller-provided array.

### src/temperature_monitor_internal.h (internal design)

#### Type: temp_ring_buffer_t

Ring buffer state:

- `buffer[]`: fixed array of `temp_sample_t`.
- `write_index`, `read_index`, `count`: typical ring buffer indices.
- `mutex`: protects buffer access.

#### Error enums / flags

This header defines:

- High-level error class (`TEMP_MONITOR_HW_ERROR` vs `TEMP_MONITOR_DATA_ERROR`).
- More specific error IDs (sensor read, sensor fault, SPI comms, too many bad samples, over-temp, etc.).
- Flags for consolidating a summary error.

The implementation combines these into a single `uint32_t error_code` using an `ERROR_CODE(...)` macro (defined elsewhere, likely in `error_manager`).

#### Type: temp_monitor_context_t

This is the central state container for the component:

- Configuration: `number_of_attached_sensors`.
- Task: `monitor_running`, `task_handle`.
- Sync: `processor_event_group`.
- Ring buffer: `ring_buffer`.
- Working sample: `current_sample`.
- Per-second counters: `samples_collected`, `bad_samples_collected`.
- Error aggregation counters and buffer: `error_buffer`, `num_errors`, `num_hw_errors`, `num_data_errors`, `num_over_temp_errors`, `highest_error_severity`.

There is a single global pointer:

- `temp_monitor_context_t* g_temp_monitor_ctx`.

### src/temperature_monitor_core.c

#### Global state

`g_temp_monitor_ctx` is allocated on first init via `calloc(1, sizeof(...))`.

#### Function: init_temp_monitor(temp_monitor_config_t\* config)

Step-by-step:

1. If already running, returns `ESP_OK`.
2. Allocates the global context if needed.
3. Stores `number_of_attached_sensors`. 
4. Calls `init_spi(number_of_attached_sensors)`.
5. Creates an event group `processor_event_group`.
6. Calls `init_temp_sensors(ctx)` to configure MAX31865 devices.
7. Starts the monitor task via `start_temperature_monitor_task(ctx)`.

Notes:

- On certain failures it frees `g_temp_monitor_ctx` and calls `shutdown_spi()`, but cleanup handling is not perfectly consistent across all failure branches.

#### Function: temp_monitor_get_event_group(void)

Returns the event group handle for consumers.

#### Function: shutdown_temp_monitor(void)

Stops the task and cleans up:

- `stop_temperature_monitor_task(ctx)`
- deletes the event group
- frees the context
- calls `shutdown_spi()`

### src/ring_buffer.c

#### Function: temp_ring_buffer_init(temp_ring_buffer_t \*rb)

- Initializes indices/count.
- Creates a mutex.

#### Function: temp_ring_buffer_push(temp_ring_buffer_t *rb, const temp_sample_t *sample)

- Copies the whole `temp_sample_t` by value into the ring.
- If full, drops the oldest item (advances `read_index`).

#### Function: temp_ring_buffer_pop_all(...)

- Public wrapper that operates on `g_temp_monitor_ctx->ring_buffer`.
- Pops up to `max_out` samples and advances `read_index`.

Design note:

- The ring buffer stores entire `temp_sample_t` structs, which can be large (array sized by `CONFIG_TEMP_SENSORS_MAX_SENSORS`). RAM usage is worth keeping an eye on.

### src/max31865_registers.h

Defines:

- `max31865_registers_t`: addresses for config/RTD/fault registers.
- `max31865_fault_t`: bitmask values for the fault status byte.

The actual address constants live in `temperature_sensors.c` as a global `max31865_registers` instance.

### src/temperature_sensors.c (MAX31865 + SPI)

#### Function: init_temp_sensors(temp_monitor_context_t\* ctx)

- Builds a MAX31865 config byte (VBias, 3-wire RTD, auto conversion, filter settings).
- Calls `init_temp_sensor(i, config_value)` for each sensor index.

#### Function: init_temp_sensor(uint8_t sensor_index, uint8_t sensor_config)

- Writes the config register via SPI.

#### Function: read_temp_sensors_data(const temp_monitor_context_t* ctx, temp_sample_t* temp_sample_to_fill)

- Iterates all attached sensors and calls `read_temp_sensor(i, &sample->sensors[i])`.
- Sets:
  - `empty = true` initially, cleared if any read succeeds.
  - `valid = true` initially, cleared if any sensor read fails or sensor marks itself invalid.

#### Function: read_temp_sensor(uint8_t sensor_index, temp_sensor_t\* data)

- Sends an RTD read command and reads back 2 bytes.
- Interprets the lowest bit as a “fault present” bit.
  - If fault bit set: reads fault status, clears the fault bit in config, marks `data->valid = false`, returns `ESP_OK`.
- Otherwise converts the RTD code to a temperature via `process_temperature_data()`.

#### Function: process_temperature_data(uint16_t sensor_data)

- Converts the raw code into resistance and then temperature using a Callendar–Van Dusen style equation.

Notes:

- Several constants are hard-coded (R0, A, B, scale factors). For accurate RTD conversion, these constants and the resistance calculation should match the actual MAX31865 circuit (reference resistor, wiring, etc.).

#### Fault handling

- `handle_max31865_fault(...)` reads fault status and clears fault bits.
- `parse_max31865_faults(...)` exists to decode fault flags but is not used by the current code.

### src/temperature_monitor_task.c (sampling + errors + signaling)

#### Task configuration

`TEMP_MONITOR_TASK` is created with:

- stack size 8192
- priority 5
- period derived from `CONFIG_TEMP_SENSORS_SAMPLING_FREQ_HZ`

#### Function: temp_monitor_task(void \*args)

Loop behavior each tick:

1. Read sensors with retry (`read_sensors_with_retry`).
2. Validate each sensor and classify errors (`check_sensor_sample`).
3. Push the sample into the ring buffer.
4. Update per-second counters and potentially set `TEMP_READY_EVENT_BIT` (`process_sample`).
5. If any errors accumulated, post a consolidated summary error (`post_temp_monitor_error_summary`).
6. Post a health heartbeat event: `event_manager_post_health(TEMP_MONITOR_EVENT_HEARTBEAT)`.
7. Wait until the next tick using a task-notify-based delay.

#### How batching works (per second)

- `samples_per_second = CONFIG_TEMP_SENSORS_SAMPLING_FREQ_HZ`.
- After `samples_collected >= samples_per_second`:
  - if too many bad samples → records a “too many samples” data error.
  - else → sets `TEMP_READY_EVENT_BIT` in `processor_event_group`.
- Then resets counters.

#### Error publishing model

There are two layers:

- Immediate: if reads time out after retries, it posts a critical error.
- Summary: after each loop it may post a consolidated `furnace_error_t` built from counters.

Errors are posted to the global bus via:

- `event_manager_post_blocking(FURNACE_ERROR_EVENT, FURNACE_ERROR_EVENT_ID, &error, sizeof(error))`

Important nuance:

- The function `build_temp_monitor_error_code()` creates a packed 32-bit code using `ERROR_CODE(...)`.
- Severity escalates to CRITICAL if HW failures exceed `CONFIG_TEMP_SENSORS_MAX_SENSOR_FAILURES`.

### Practical implications for merging your code later

- Downstream consumer pattern: wait on `temp_monitor_get_event_group()` + `TEMP_READY_EVENT_BIT`, then call `temp_ring_buffer_pop_all()`.
- Make sure the global `event_registry` includes and defines `FURNACE_ERROR_EVENT` (it does) and that the event manager is initialized before this task runs.
- If you add outlier rejection (`CONFIG_TEMP_SENSORS_HAVE_OUTLIERS_REJECTION`), it likely belongs in `check_sensor_sample()` or `process_sample()`.

---

## components/temperature_processor_component/

### Purpose

Consumes batches of raw temperature samples from `temperature_monitor_component`, computes processed temperatures (mainly averages + anomaly checks), and publishes results/events for the rest of the system.

This component is the “second stage” of the temperature pipeline:

- Monitor: reads hardware → ring buffer → signals “batch ready”.
- Processor: waits for “batch ready” → drains ring buffer → calculates → posts processed output.

### components/temperature_processor_component/CMakeLists.txt

- Compiles all `src/*.c`.
- `PRIV_REQUIRES ... temperature_monitor_component ... event_manager`
  - Depends directly on the monitor API (`temp_monitor_get_event_group`, `temp_ring_buffer_pop_all`).
  - Uses the global event manager to publish output + health.

Note:

- The `PRIV_REQUIRES` list contains `common` twice; harmless but redundant.

### include/temperature_processor_component.h

Public lifecycle API:

- `init_temp_processor()` allocates context and starts the processor task.
- `shutdown_temp_processor()` stops the task and frees context.

### include/temperature_processor_types.h

Currently empty (placeholder). The actual event payload type `temp_processor_data_t` lives in `components/event_manager/include/event_registry.h`.

### src/temperature_processor_internal.h

#### Error model

`process_temperature_error_type_t` enumerates what can go wrong while processing:

- invalid input
- invalid/no valid samples
- threshold exceeded

#### Anomaly reporting structures

- `temp_sensor_pair_t`: describes a pair of sensors and their temperature delta.
- `temp_anomaly_result_t`: array of sensor pairs that looked suspicious.

#### Processing result structures

- `process_temp_result_t`: anomaly result + error type for a single input sample.
- `process_temp_samples_result_t`: a collection of per-sample error results + overall error type.

#### Context: temp_processor_context_t

Holds:

- `temperatures_buffer[CONFIG_TEMP_SENSORS_RING_BUFFER_SIZE]`: per-sample processed values used for computing the final average.
- `processor_running`: run flag.
- `task_handle`.

### src/temperature_processor_core.c

#### Global state

- `temp_processor_context_t* g_temp_processor_ctx` allocated by `calloc`.

#### Function: init_temp_processor(void)

Step-by-step:

1. If already running, returns `ESP_OK`.
2. Allocates context if NULL.
3. Sets `g_temp_processor_ctx->processor_running = true`.
4. Calls `start_temp_processor_task(ctx)`.

#### Function: shutdown_temp_processor(void)

Step-by-step:

1. If not running, returns `ESP_OK`.
2. Calls `stop_temp_processor_task(ctx)`.
3. Clears `processor_running`.
4. Frees context.

Important nuance:

- Stopping is cooperative; see `stop_temp_processor_task` below.

### src/temperature_processor_task.c

This file confirms the hypothesis: the processor waits on the temperature monitor’s event group and drains the ring buffer.

#### Function: temp_process_task(void \*args)

High-level loop:

1. Obtains the event group handle from the monitor:
   - `EventGroupHandle_t event_group = temp_monitor_get_event_group();`
2. If the event group is unavailable, logs an error and exits.
3. While running:
   - Waits on `TEMP_READY_EVENT_BIT`:
     - `xEventGroupWaitBits(event_group, TEMP_READY_EVENT_BIT, pdTRUE, pdFALSE, portMAX_DELAY);`
     - Uses `clearOnExit = pdTRUE` so the bit is cleared when consumed.
   - Drains available samples:
     - `temp_ring_buffer_pop_all(samples_buffer, CONFIG_TEMP_SENSORS_RING_BUFFER_SIZE)`.
   - Calls `process_temperature_samples(...)` to compute an overall average.
   - Publishes the results (see events notes below).
   - Posts a health heartbeat: `event_manager_post_health(TEMP_PROCESSOR_EVENT_HEARTBEAT)`.

Timing model:

- Unlike the monitor task, this task does not run on a fixed period.
- It is event-driven: it wakes up when the monitor signals a batch.

Important nuance / potential issue:

- `stop_temp_processor_task()` only sets `processor_running = false` and does not notify/unblock the task.
  - If the task is blocked in `xEventGroupWaitBits(...)`, it may not exit promptly until the next batch-ready bit is set.

### src/temperature_processor.c (core processing logic)

#### Function: process_temperature_samples(...)

Inputs:

- `input_samples`: array of `temp_sample_t` (drained from ring buffer)
- `number_of_samples`: count
- `output_temperature`: single aggregated temperature output

Behavior:

1. Validates inputs.
2. For each input sample:
   - Calls `process_temperature_data(&input_samples[i], &temperatures_buffer[i])`.
   - Tracks min/max across the per-sample temperatures.
   - Collects error results if any.
3. If overall range `max - min` exceeds `CONFIG_TEMP_DELTA_THRESHOLD`, sets error type `PROCESS_TEMPERATURE_ERROR_THRESHOLD_EXCEEDED`.
4. Computes final average across the per-sample temperatures using `average_float_array(...)`.

#### Function: process_temperature_data(temp_sample_t* input_temperatures, float* output_temperature)

Per-sample behavior:

- If `number_of_attached_sensors == 0`, marks invalid.
- Calls `check_temperature_anomalies(...)`.
  - If anomalies exist, flags `PROCESS_TEMPERATURE_ERROR_THRESHOLD_EXCEEDED`.
- Calculates average temperature across sensors in the sample using `calculate_average_temperature(...)`.

#### Function: check_temperature_anomalies(temp_sample_t\* temperatures)

Anomaly model:

- Compares adjacent sensors `i-1` and `i`.
- If `delta > CONFIG_TEMP_DELTA_THRESHOLD`, records a `temp_sensor_pair_t` entry.

Important nuance:

- It compares sensors in array order and does not check `sensor.valid`.
  - If invalid sensor readings exist (e.g., 0°C default), anomalies may be false positives.

#### Function: average_float_array / calculate_average_temperature

Both functions support optional outlier rejection controlled by:

- `CONFIG_TEMP_SENSORS_HAVE_OUTLIERS_REJECTION`

If enabled and enough points exist (>= 3), they drop min and max and average the remaining values.

### src/temperature_processor_events.c

#### Function: post_temp_processor_event(temp_processor_data_t data)

Posts a `temp_processor_data_t` payload to:

- base: `TEMP_PROCESSOR_EVENT`
- id: `PROCESS_TEMPERATURE_EVENT_DATA` (0)

Important integration issue:

- In `components/event_manager/include/event_registry.h`, `TEMP_PROCESSOR_EVENT` is declared but (as noted earlier) not defined in `event_registry.c`.
  - If this code is linked in, it will require adding `ESP_EVENT_DEFINE_BASE(TEMP_PROCESSOR_EVENT);`.

Also note:

- There is a helper `post_processing_error(...)` that posts to `FURNACE_ERROR_EVENT`, but it is currently unused.

### Publishing behavior vs. current code issues

There are two different “shapes” visible in the code:

1. The events file suggests a simple model:

- publish `temp_processor_data_t` (average + valid)

2. The task file currently calls `post_temp_processor_event(...)` as if it were a generic “post any processor event” function (event id + payload + size).

This mismatch is important:

- `temperature_processor_internal.h` declares `post_temp_processor_event(temp_processor_data_t data)`.
- `temperature_processor_events.c` implements `post_temp_processor_event(temp_processor_data_t data)`.
- `temperature_processor_task.c` calls `post_temp_processor_event(PROCESS_TEMPERATURE_EVENT_ERROR, &result, sizeof(result))` and also calls it again with `(PROCESS_TEMPERATURE_EVENT_DATA, &average_temperature, sizeof(float))`.

So the _intended_ behavior is clear (post processed data + maybe post processing errors), but the current function calls/signatures do not align.

### Practical implications for merging your code later

- Yes: this component does exactly what we expected — it blocks on `TEMP_READY_EVENT_BIT` and drains the monitor ring buffer.
- If you plan to consume processed temps via events, you’ll need the event-base definition for `TEMP_PROCESSOR_EVENT` and consistent `post_temp_processor_event` API usage.
- If you depend on clean shutdown, consider adding a wake-up mechanism so the task can exit even when blocked on the event group.

---

## components/temperature_profile_controller/

### Purpose

Turns a **heating profile** (a linked list of nodes) into a **time-based target temperature**.

This is the “setpoint generator” side of temperature control:

- Input: a `heating_profile_t` (from `components/common/include/core_types.h`) plus an initial temperature.
- Output: for a given elapsed time $t$ (ms), compute the target setpoint temperature.

It does not read sensors, does not run tasks, and does not post events—it's a pure compute/helper component with a small internal context.

### components/temperature_profile_controller/CMakeLists.txt

- Compiles all `src/*.c`.
- `PRIV_REQUIRES logger_component common`
  - Uses logger macros.
  - Uses `core_types.h` (via the types header).

### include/temperature_profile_types.h

#### Type: profile_controller_error_t

Error codes returned by the controller:

- `PROFILE_CONTROLLER_ERROR_NONE`: success
- `PROFILE_CONTROLLER_ERROR_INVALID_ARG`: bad inputs or unknown node type
- `PROFILE_CONTROLLER_ERROR_NO_PROFILE_LOADED`: controller not initialized with a profile
- `PROFILE_CONTROLLER_ERROR_COMPUTATION_FAILED`: reserved (not used in current implementation)
- `PROFILE_CONTROLLER_ERROR_TIME_EXCEEDS_PROFILE_DURATION`: requested time is past the end of the profile
- `PROFILE_CONTROLLER_ERROR_UNKNOWN`: allocation or unexpected failures

#### Type: temp_profile_config_t

```c
typedef struct {
      float initial_temperature;
      heating_profile_t *heating_profile;
} temp_profile_config_t;
```

Meaning:

- `initial_temperature`: the starting temperature at $t=0$.
- `heating_profile`: pointer to the profile head (linked nodes).

Ownership note:

- The controller stores the pointer; it does not copy or manage the profile memory.

### include/temperature_profile_controller.h (public API)

#### Function: load_heating_profile(const temp_profile_config_t config)

Loads profile pointer + initial temperature into an internal static context.

#### Function: get_target_temperature_at_time(uint32_t time_ms, float \*temperature)

Computes the setpoint at an elapsed time.

#### Function: shutdown_profile_controller(void)

Frees internal controller context (but not the profile itself).

### src/temperature_profile_core.c

#### Internal context

```c
typedef struct {
      float initial_temperature;
      heating_profile_t *heating_profile;
} temperature_profile_controller_context_t;

static temperature_profile_controller_context_t *g_temp_profile_controller_ctx = NULL;
```

Meaning:

- This component is effectively a singleton (one active profile at a time).

#### Function: load_heating_profile(const temp_profile_config_t config)

Behavior:

1. Validates `config.heating_profile != NULL`.
2. Allocates the controller context with `calloc` if not already allocated.
3. Stores:
   - `heating_profile`
   - `initial_temperature`

Notes:

- If you call `load_heating_profile()` multiple times, it overwrites the stored pointer and initial temp (no explicit “unload”).

#### Function: get_target_temperature_at_time(uint32_t time_ms, float \*temperature)

This walks the node list and figures out which node contains the requested time.

Algorithm:

1. Verify a profile is loaded.
2. Start at the first node.
3. Keep a running `elapsed_time` (sum of node durations).
4. Track `start_temp` which is:
   - `initial_temperature` at the first node
   - then updated to the previous node’s `set_temp` as you advance
5. For the node that contains `time_ms`, compute a normalized fraction:
   $$\tau = \frac{time\_ms - elapsed\_time}{duration\_ms} \in [0,1]$$
6. Use the node’s `type` to interpolate between `start_temp` and `node->set_temp`.

Interpolation per node type:

- Linear:
  $$T(\tau) = T_0 + (T_1 - T_0)\tau$$
- Square:
  $$T(\tau) = T_0 + (T_1 - T_0)\tau^2$$
- Cube:
  $$T(\tau) = T_0 + (T_1 - T_0)\tau^3$$
- Log:
  $$T(\tau) = T_0 + (T_1 - T_0)\log_{10}(1 + 9\tau)$$
  - The `1 + 9τ` scaling makes it map $\tau\in[0,1]$ to a log input of $[1,10]$.

Outcomes:

- Returns `PROFILE_CONTROLLER_ERROR_NONE` once it computes a temperature.
- Returns `PROFILE_CONTROLLER_ERROR_TIME_EXCEEDS_PROFILE_DURATION` if time is beyond the last node.
- Returns `PROFILE_CONTROLLER_ERROR_INVALID_ARG` if a node type is unknown.

Important nuances / edge cases:

- `temperature` (output pointer) is not checked for NULL.
- If a node has `duration_ms == 0`, this will divide by zero.
- The `expression` field in `heating_node_t` is not used here.

#### Function: shutdown_profile_controller(void)

- Frees the singleton context.
- Does not free profile nodes or profile name.

### Practical implications for merging your code later

- This is the natural place to integrate “profile mode” into your coordinator: the coordinator can call `get_target_temperature_at_time(...)` to generate a setpoint for PID.
- Ensure your profiles are well-formed:
  - non-NULL linked list
  - durations > 0
  - nodes chained correctly (`next_node`)

---

## components/pid_component/

### Purpose

Implements the **PID controller math** that converts a setpoint (target temperature) and a measured value (current temperature) into a bounded control output.

This component is intentionally small:

- No tasks
- No event posting
- No timers

It’s just a pair of functions:

- `pid_controller_compute(...)`: compute next output
- `pid_controller_reset()`: reset controller state

In the overall architecture, the expected chain is:

1. Profile controller generates setpoint vs time
2. Temperature processor produces measured temperature
3. PID computes a control output
4. Heater controller consumes that output and drives GPIO/PWM

### components/pid_component/CMakeLists.txt

- Compiles all `src/*.c`.
- `INCLUDE_DIRS "include"` exports `pid_component.h`.
- `PRIV_REQUIRES logger_component` because the implementation logs PID outputs.

### components/pid_component/Kconfig

Exposes PID tuning values as **percent-based integers** (0–100):

- `CONFIG_PID_KP`, `CONFIG_PID_KI`, `CONFIG_PID_KD`
- `CONFIG_PID_OUTPUT_MIN`, `CONFIG_PID_OUTPUT_MAX`

These are converted to floats in the implementation by dividing by 100.0f.

Implication:

- The PID output range is effectively normalized to $[0,1]$ by default (0%–100%), which maps nicely to “heater power fraction” if the heater controller expects a 0..1 duty/power value.

### include/pid_component.h (public API)

#### Function: pid_controller_compute(float setpoint, float measured_value, float dt)

Computes the PID output for the current timestep.

Parameters:

- `setpoint`: desired value
- `measured_value`: current value
- `dt`: timestep since last compute call

Return:

- Control output clamped to `[output_min, output_max]`.

#### Function: pid_controller_reset(void)

Resets internal controller state (integral accumulator and previous error).

### src/pid_component.c

#### Internal constants and state

There are two internal structs:

1. Tunable parameters (constant after build):

```c
typedef struct {
    float kp;
    float ki;
    float kd;
    float output_min;
    float output_max;
} pid_controller_params_t;
```

`pid_params` is built from Kconfig:

- `kp = CONFIG_PID_KP / 100.0f`
- `ki = CONFIG_PID_KI / 100.0f`
- `kd = CONFIG_PID_KD / 100.0f`
- `output_min/max` also scaled the same way.

2. Runtime state (persists across calls):

```c
typedef struct {
    float integral;
    float previous_error;
} pid_controller_state_t;
```

This is held in a single `static pid_state`, so there is one global controller instance.

#### Function: pid_controller_compute(const float setpoint, const float measured_value, const float dt)

Core math:

1. Error:
   $$e = setpoint - measured\_value$$
2. Integrator update:
   $$I \leftarrow I + e\cdot dt$$
3. Derivative term:
   $$D = \frac{e - e_{prev}}{dt}$$
4. Output:
   $$u = K_p e + K_i I + K_d D$$
5. Clamp output to `[output_min, output_max]`.
6. Store `previous_error = error`.
7. Log a debug line with setpoint/measured/output.

Important runtime assumptions / edge cases:

- `dt` must be $> 0$. If `dt == 0`, this divides by zero when computing the derivative.
- There is no explicit anti-windup beyond output clamping.
  - The integrator continues to accumulate even if output saturates.
  - If you hold saturation for a long time, you can get integral windup (slow recovery when the system comes back in range).
- This implementation is not thread-safe.
  - Because `pid_state` is global, two tasks calling `pid_controller_compute` concurrently would corrupt the state.
  - The intended use is “one control loop task” owns PID updates.

#### Function: pid_controller_reset(void)

Sets:

- `pid_state.integral = 0.0f`
- `pid_state.previous_error = 0.0f`
  And logs `"PID controller reset"` at INFO level.

### Practical implications for merging your code later

- Decide what `dt` unit is for your control loop and use it consistently (seconds vs milliseconds); the derivative and integrator scaling depends on it.
- If you want robust heater control, consider adding anti-windup (integral clamp or back-calculation) once we see how `heater_controller_component` consumes the PID output.
- If you need multiple independent PID controllers (e.g., multiple zones), this component would need to be refactored to hold per-instance state instead of a single global `pid_state`.

---

## components/heater_controller_component/

### Purpose

Responsible for **actually driving the heater output pin**, based on a commanded “power level” (0.0 to 1.0).

This component is the “actuation” end of the control chain:

- Inputs: commands (primarily “set power level”) delivered via the event bus.
- Outputs:
  - GPIO writes to turn the heater ON/OFF.
  - Events indicating heater toggles.
  - A `FURNACE_ERROR_EVENT` on GPIO failures.

The control method is a common “time-proportional window” approach:

- Choose a fixed window size $W$ (ms).
- For a desired power fraction $p \in [0,1]$, turn ON for $pW$ then OFF for $(1-p)W$.

This is often used for relays/SSR control where you don’t have (or don’t want) high-frequency PWM.

### components/heater_controller_component/CMakeLists.txt

- Compiles all `src/*.c`.
- Depends on:
  - `gpio_master_driver` for GPIO access
  - `event_manager` / `esp_event` for receiving commands and posting notifications/errors
  - `common` for `CHECK_ERR_LOG_*` macros
  - `logger_component` for logging

### components/heater_controller_component/Kconfig

Key runtime knobs:

- `CONFIG_HEATER_CONTROLLER_GPIO`: which GPIO drives the heater (default 25)
- `CONFIG_HEATER_WINDOW_SIZE_MS`: window size $W$ (default 10000ms)
- Task settings: name/stack/priority

### include/heater_controller_component.h (public API)

This component intentionally exposes only lifecycle functions:

- `init_heater_controller_component()`
- `shutdown_heater_controller_component()`

All command/control is expected to flow through events (see `event_registry.h`’s `HEATER_CONTROLLER_EVENT`).

### src/heater_controller_internal.h (internal API + context)

#### Context: heater_controller_context_t

This module maintains a global singleton context:

```c
extern heater_controller_context_t* g_heater_controller_context;
```

The context tracks:

- `task_handle`: FreeRTOS task handle
- `heater_state`: current ON/OFF state (declared `volatile`)
- `target_power_level`: requested power fraction (0..1)
- `task_running`: loop control flag
- `initialized`: init guard

Important nuance:

- `heater_state` exists in the struct but is not currently updated in `toggle_heater(...)`.

### src/heater_controller_core.c

#### Function: init_heater_controller_component(void)

High-level behavior:

1. Idempotent: if context exists and `initialized` is true, returns `ESP_OK`.
2. Allocates `g_heater_controller_context` via `calloc` (singleton).
3. Initializes event subscriptions: `init_events(ctx)`.
4. Initializes heater hardware: `init_heater_controller(...)`.
5. Starts the heater control task: `init_heater_controller_task(ctx)`.
6. Sets `initialized = true`.

This init path uses `CHECK_ERR_LOG_RET(...)`, so it is fail-fast: it logs and returns on errors.

Important integration issue to note:

- `heater_controller_internal.h` declares `esp_err_t init_heater_controller();` (no args)
- `heater_controller_core.c` calls `init_heater_controller(g_heater_controller_context)` (with a ctx arg)

Unless there is another prototype elsewhere, this is a signature mismatch and would break compilation under normal C rules.

#### Function: shutdown_heater_controller_component(void)

Shuts down in reverse order:

1. Stops the task
2. Shuts down heater hardware
3. Frees the context

### src/heater_controller_task.c

#### Windowed time-proportional control

The task computes per window:

- `on_time = target_power_level * window_ms`
- `off_time = window_ms - on_time`

Then:

1. If `on_time > 0`: turn heater ON and block for `on_time`.
2. If `off_time > 0`: turn heater OFF and block for `off_time`.

Blocking is done via `ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(...))`.
This acts like “sleep unless someone notifies me”. It allows the shutdown path to wake the task immediately using `xTaskNotifyGive(...)`.

#### Function: heater_controller_task(void \*args)

Expected behavior:

- Cast `args` to `heater_controller_context_t*` and loop while `ctx->task_running`.
- Ensure heater is OFF on exit.

Important integration issue to note:

- In `init_heater_controller_task(...)`, the task is created with `pvParameters = NULL`.
- But `heater_controller_task` immediately casts `args` to `heater_controller_context_t*` and dereferences it.

As written, this would dereference NULL and crash. The expected behavior is to pass `ctx` as the task parameter.

#### Function: set_heater_target_power_level(...)

Validates `power_level` is in `[0,1]`, then stores it into the context.

Important nuance:

- This does not notify the task.
- So a new target power level will take effect on the next ON/OFF segment boundary (worst case: up to one window duration).

#### Error handling in the task

If toggling fails, it posts a `FURNACE_ERROR_EVENT` with:

- severity: `SEVERITY_CRITICAL`
- source: `SOURCE_HEATER_CONTROLLER`
- error_code: `HEATER_CONTROLLER_ERROR_GPIO`

### src/heater_controller_events.c

#### Subscription model

`init_events(ctx)` subscribes the module to all IDs on the `HEATER_CONTROLLER_EVENT` base:

- `event_manager_subscribe(HEATER_CONTROLLER_EVENT, ESP_EVENT_ANY_ID, handler, ctx)`

The event handler currently supports:

- `HEATER_CONTROLLER_SET_POWER_LEVEL`: expects `event_data` to point to a `float` in `[0,1]` and stores it via `set_heater_target_power_level(...)`.
- `HEATER_CONTROLLER_STATUS_REPORT_REQUESTED`: placeholder (TODO)

Important nuance / bug:

```c
if (event_data == NULL || sizeof(float) != sizeof(event_data))
```

`sizeof(event_data)` is the size of a pointer, not the payload size. ESP-IDF’s event handler signature does not include payload length, so this check cannot work as intended.

Practical implication:

- The handler should likely only check `event_data != NULL` and assume callers post a `float` payload for SET_POWER_LEVEL.

#### Event posting helpers

- `post_heater_controller_error(...)` posts to `FURNACE_ERROR_EVENT`.
- `post_heater_controller_event(...)` posts to `HEATER_CONTROLLER_EVENT` with a chosen ID and payload.

### src/heater_controller.c (hardware GPIO)

#### Function: init_heater_controller(void)

- Initializes the GPIO driver wrapper (`gpio_master_driver_init()`).
- Configures `CONFIG_HEATER_CONTROLLER_GPIO` as output.

#### Function: toggle_heater(const bool state)

- Writes the output level (1 for ON, 0 for OFF).
- Posts `HEATER_CONTROLLER_HEATER_TOGGLED` with a `bool` payload.

Important nuance:

- It logs at INFO every toggle, which at a 10s window is fine, but at smaller windows could become log spam.

#### Function: shutdown_heater_controller(void)

- Calls `toggle_heater(HEATER_OFF)`.

### Practical implications for merging your code later

- This component likely expects the coordinator (or some control task) to compute power (maybe from PID output) and post `HEATER_CONTROLLER_SET_POWER_LEVEL` events.
- There are a couple of correctness issues worth addressing before relying on it:
  - Task argument is passed as NULL but the task dereferences it.
  - `init_heater_controller(...)` is called with a context argument but declared without one.
  - Event payload validation is incorrect for SET_POWER_LEVEL.
- Conceptually, the windowed control pattern is a good match for “PID output in [0..1]” → “heater duty within a window”.

---

## components/coordinator_component/

### Purpose

This component is the **system orchestrator** for “run a heating profile” mode.

Conceptually it should:

1. Receive high-level commands (start/pause/resume/stop/status) over the event bus.
2. Track the current profile/time/state.
3. Generate a setpoint from `temperature_profile_controller`.
4. Read the latest measured temperature (from the temperature processor).
5. Compute a control output (via `pid_component`).
6. Command the heater controller (`HEATER_CONTROLLER_SET_POWER_LEVEL`).

### components/coordinator_component/CMakeLists.txt

- Compiles all `src/*.c`.
- Depends on:
  - `temperature_profile_controller` (setpoint generation)
  - `pid_component` (control output)
  - `event_manager` / `esp_event` (receive coordinator commands; post results)
  - `common` + `logger_component`

### components/coordinator_component/Kconfig

Defines sizing limits:

- `CONFIG_COORDINATOR_MAX_HEATING_PROFILE_STEPS` (default 20)
- `CONFIG_COORDINATOR_MAX_PROFILES_STORED` (default 5)

Important nuance:

- These values are not currently referenced in the coordinator implementation shown here (no enforcement/allocation based on them).

### include/coordinator_component_types.h

#### Type: coordinator_config_t

```c
typedef struct {
      heating_profile_t *profiles;
      size_t num_profiles;
} coordinator_config_t;
```

Meaning:

- The coordinator is configured with an array of `heating_profile_t` plus a count.
- It stores this pointer; it does not deep-copy profiles.

#### Type: heating_task_state_t

This is the “status snapshot” struct:

- `profile_index`
- `current_temperature`, `target_temperature`
- flags: `is_active`, `is_paused`, `is_completed`
- time: `current_time_elapsed_ms`, `total_time_ms`
- booleans for outputs: `heating_element_on`, `fan_on`

This is what the coordinator returns when asked for a status report.

### include/coordinator_component.h (public API)

Public functions:

- `init_coordinator(const coordinator_config_t *config)`
- `stop_coordinator(void)`

Note:

- The coordinator is primarily driven via events (`COORDINATOR_EVENT` base), not direct function calls.

### src/coordinator_component_internal.h

#### Internal context: coordinator_ctx_t

Holds:

- `task_handle`: handle for the heating/profile task
- `heating_profiles`, `num_profiles`: copied from config
- `running`, `paused`
- `current_temperature`
- `heating_task_state`: current status snapshot
- `events_initialized`

Also defines `INVALID_PROFILE_INDEX = 0xFFFFFFFF` as a sentinel.

### src/coordinator_core.c

#### Function: init_coordinator(const coordinator_config_t \*config)

Behavior:

1. Allocates a singleton `g_coordinator_ctx` on first init.
2. Stores profile pointer + count.
3. Subscribes to coordinator events via `init_coordinator_events(ctx)`.

Important nuance / edge cases:

- `config` is not checked for NULL.
- It only returns early if `g_coordinator_ctx != NULL && g_coordinator_ctx->running`.
  - This means `init_coordinator` can be called multiple times while not “running”, but it will keep the same singleton and overwrite pointers.

#### Function: coordinator_list_heating_profiles(void)

Lists available profiles.

Important note:

- This function is not declared in the public header.
- The current logging call has a format string/argument mismatch (it prints name but the format string includes duration/target temp fields).

#### Function: stop_coordinator(void)

Stops the coordinator:

1. Unsubscribes from coordinator events.
2. Stops the heating profile task.
3. Frees the singleton context.

Important nuance:

- It returns early without freeing if `!g_coordinator_ctx->running`.
  - So if you call `init_coordinator(...)` and never start a profile, `stop_coordinator()` currently does not free the allocated context.

### src/coordinator_component_events.c

#### Coordinator command handling (COORDINATOR_EVENT)

`init_coordinator_events(ctx)` subscribes to `COORDINATOR_EVENT` with `ESP_EVENT_ANY_ID`.

The event handler responds to these RX IDs (from `event_registry.h`):

- `COORDINATOR_EVENT_START_PROFILE`
- `COORDINATOR_EVENT_PAUSE_PROFILE`
- `COORDINATOR_EVENT_RESUME_PROFILE`
- `COORDINATOR_EVENT_STOP_PROFILE`
- `COORDINATOR_EVENT_GET_STATUS_REPORT`
- `COORDINATOR_EVENT_GET_CURRENT_PROFILE`

For each, it calls the corresponding internal function (`start_heating_profile`, `pause_heating_profile`, etc.).
On failures, it posts `COORDINATOR_EVENT_ERROR_OCCURRED` with a `coordinator_error_data_t` payload.

Important nuance / edge cases:

- The handler does not validate `event_data` for NULL before dereferencing for START_PROFILE.
- `event_registry.h` defines a `coordinator_start_profile_data_t { size_t profile_index; }`, but the handler treats `event_data` as `size_t*`.
  - The intention is compatible (both contain a `size_t`), but the type contract is inconsistent.

#### Temperature updates (TEMP_PROCESSOR_EVENT)

There is a `temperature_processor_event_handler(...)` intended to update the coordinator’s notion of current temperature.

However, in the current `init_coordinator_events(...)`, the coordinator only subscribes to `COORDINATOR_EVENT`.

- There is no subscription to `TEMP_PROCESSOR_EVENT`, so this handler will never run.

Additionally:

- The handler stores the value into a global `coordinator_current_temperature`, not into `ctx->current_temperature`.
- The `ctx` local in that handler is unused.

Practical implication:

- As written, `ctx->current_temperature` is never updated from the temperature pipeline.

#### Posting events

- `post_coordinator_event(...)` posts into `COORDINATOR_EVENT`.
- `post_heater_controller_event(...)` posts into `HEATER_CONTROLLER_EVENT`.

Note:

- `COORDINATOR_EVENT_GET_STATUS_REPORT` and `COORDINATOR_EVENT_GET_CURRENT_PROFILE` are used as both “request” and “response” IDs (it posts the same ID it receives). That works mechanically but can be confusing for subscribers.

### src/heating_profile_task.c

#### The “heating profile” task

This file implements the runtime loop that should:

1. Advance elapsed time
2. Get target temperature from profile
3. Compute PID output
4. Send heater power command

Key functions:

#### Function: start_heating_profile(coordinator_ctx_t\* ctx, size_t profile_index)

Sets up the run:

- Validates `profile_index < num_profiles`.
- Initializes `heating_task_state` fields.
- Calls `load_heating_profile(...)` to load the selected profile into the profile controller.
- Creates the FreeRTOS task (`heating_profile_task`) and marks `ctx->running = true`.

Important nuances / potential issues:

- `total_time_ms` is set to `first_node->duration_ms` only; it does not sum all nodes.
- There is no explicit initialization of `last_wake_time` before the first compute loop.

#### Function: heating_profile_task(void\* args)

Loop structure:

- Waits on `ulTaskNotifyTake(pdTRUE, portMAX_DELAY)` each iteration.
- If not paused, advances `current_time_elapsed_ms`.
- Calls `get_target_temperature_at_time(...)` to compute `target_temperature`.
- Calls `pid_controller_compute(...)` to compute a `power_output`.
- Posts `HEATER_CONTROLLER_SET_POWER_LEVEL` with `power_output`.

Important nuance / likely bug:

- Nothing in this file notifies the task periodically.
  - The task will block forever unless something else calls `xTaskNotifyGive(ctx->task_handle)`.
  - `stop_heating_profile(...)` does notify (to wake it for shutdown), but “run the loop every N ms” is not implemented.

Important nuance about PID argument order and dt units:

- `pid_controller_compute(setpoint, measured_value, dt)` expects setpoint first.
- The call uses `(ctx->current_temperature, target_temperature, last_update_duration)`.
  - That appears reversed: setpoint should be `target_temperature` and measured should be `current_temperature`.
- `last_update_duration` is in milliseconds, but the PID implementation treats `dt` as a plain scalar; if you intend seconds, it should be converted.

#### Pause/resume/stop APIs

- `pause_heating_profile(...)` sets `paused = true` and updates `heating_task_state.is_paused`.
- `resume_heating_profile(...)` clears paused.
- `stop_heating_profile(...)` clears `running`, notifies the task to wake, and calls `shutdown_profile_controller()`.

### Practical implications for merging your code later

- The coordinator is clearly intended to be the “glue” that connects temperature processing → profile setpoint → PID → heater commands.
- There are a few wiring gaps to address before it can actually control heat reliably:
  - Subscribe to `TEMP_PROCESSOR_EVENT` and update `ctx->current_temperature`.
  - Add a periodic timing mechanism for the heating task (e.g., `vTaskDelayUntil`) instead of blocking forever on notifications.
  - Fix PID argument order and decide on `dt` units.
  - Compute `total_time_ms` as the sum of node durations.

---

## components/error_manager/

### Purpose

Provides a lightweight “error description registry” so that a `furnace_error_t` (severity/source/error_code) can be translated into a human-readable string.

High-level design:

- Each component (identified by an integer component/source ID) can register a callback.
- Given an error payload, the error manager looks up the callback based on `error->source` and calls it to get a description string.

This pairs nicely with the project’s event-driven error channel (`FURNACE_ERROR_EVENT`):

- Components publish `furnace_error_t` events.
- A central error consumer (or UI/logging layer) can call `get_error_description(...)` to print something meaningful.

### components/error_manager/CMakeLists.txt

- Compiles all `src/*.c`.
- Depends on:
  - `common` (for `furnace_error_types.h`)
  - `logger_component`
  - `esp_common`

### components/error_manager/Kconfig

Defines:

- `CONFIG_ERROR_MANGER_MAX_MODULES` (default 10): maximum number of component IDs that can register a descriptor.

Important nuance:

- Typo: “MANGER” instead of “MANAGER”. This is fine as long as code and Kconfig match (they do), but it’s easy to miss when searching.

### include/error_manager.h (public API)

#### Macro: ERROR_CODE(type, sub_type, value, data)

Builds a packed 32-bit error code:

```c
( ((uint32_t)(type)   << 24) |
   ((uint32_t)(sub_type) << 16) |
   ((uint32_t)(value)  << 8)  |
   ((uint32_t)(data)) )
```

Meaning:

- Top byte: `type`
- Next byte: `sub_type`
- Next byte: `value`
- Low byte: `data`

This is a convenience for structuring error codes so they can carry multiple fields.

#### Type: error_descriptor_func_t

```c
typedef const char* (*error_descriptor_func_t)(uint16_t error_code);
```

This is the per-component callback signature:

- Input: a component-defined error code (note: `uint16_t` here)
- Output: a description string (expected to have static lifetime)

#### Function: register_error_descriptor(uint16_t component_id, error_descriptor_func_t descriptor_func)

Registers a callback for a given component/source ID.

#### Function: get_error_description(const furnace_error_t\* error)

Returns a string description for the error, based on `error->source`.

### src/error_manager.c

#### Internal registry

```c
static error_descriptor_func_t error_manager_funcs[CONFIG_ERROR_MANGER_MAX_MODULES] = {NULL};
```

This is a fixed-size lookup table:

- Index = component/source ID
- Value = descriptor function pointer

#### Function: register_error_descriptor(...)

Behavior:

- If `component_id` is out of range, logs an error and returns.
- Otherwise stores the function pointer.

#### Function: get_error_description(const furnace_error_t\* error)

Behavior:

1. Reads:
   - `error_source = error->source`
   - `error_code = error->error_code`
2. If out of range or no function registered: returns a generic string.
3. Otherwise: returns `error_manager_funcs[error_source](error_code)`.

Important nuances / edge cases:

- `error` is not checked for NULL.
- `furnace_error_t.error_code` is a `uint32_t` (from the common header), but the descriptor callback takes a `uint16_t`.
  - In `get_error_description`, `error_code` is stored in a `uint16_t`, so it truncates.
  - If you rely on the 32-bit packed `ERROR_CODE(...)` macro, you will lose information unless you redesign the descriptor signature.

### Practical implications for merging your code later

- If you want richer error decoding, consider making `error_descriptor_func_t` accept a `uint32_t`.
- This component does not subscribe to `FURNACE_ERROR_EVENT` on its own; it’s a utility. A separate “error consumer” component (or future coordinator/monitor feature) would typically:
  - subscribe to `FURNACE_ERROR_EVENT`
  - call `get_error_description(&err)`
  - log/act accordingly.
