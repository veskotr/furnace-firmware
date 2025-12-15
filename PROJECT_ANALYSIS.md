# Furnace Firmware - Comprehensive Project Analysis

## Executive Summary

The project is an ESP32-based furnace control system with temperature monitoring, PID control, and programmable heating profiles. The architecture uses event-driven component design with FreeRTOS tasks. While the overall structure is reasonable, there are several critical bugs, logical errors, and architectural concerns that need addressing.

---

## Architecture Overview

### High-Level Design
```
┌─────────────────────────────────────────────────────────────┐
│                        Main Application                     │
│                   (main.c - Currently: SPI Test)            │
└────────────┬────────────────────────────────────────────────┘
             │
   ┌─────────┴──────────────────────────────────────────┐
   │          Coordinator Component (Orchestrator)      │
   │  - Manages heating profile lifecycle               │
   │  - Synchronizes temperature monitoring              │
   │  - Runs PID control loop                            │
   └─────┬────────────┬──────────────┬──────────────┬──┘
         │            │              │              │
    ┌────▼─────┐  ┌───▼──────┐  ┌───▼─────────┐  ┌▼───────────┐
    │Temperature│  │Heater    │  │PID Control  │  │Temperature │
    │Monitor    │  │Controller│  │Component    │  │Profile     │
    │Component  │  │Component │  │             │  │Controller  │
    └────┬─────┘  └───┬──────┘  └─────────────┘  └────────────┘
         │            │
    ┌────▼──────────────▼────────┐
    │Temperature Processor       │
    │Component                   │
    │ - Ring Buffer Management   │
    │ - Sample Averaging         │
    │ - Anomaly Detection        │
    └────────────────────────────┘
         │
    ┌────▼────────────┐
    │SPI Master       │
    │Component        │
    │(MAX31865 RTD)   │
    └─────────────────┘
```

### Component Structure
- **Coordinator Component**: Central orchestrator managing state and synchronization
- **Temperature Monitor Component**: Hardware interface for MAX31865 RTD sensors via SPI
- **Temperature Processor Component**: Statistical analysis and averaging
- **Heater Controller Component**: PWM-based heater control via GPIO
- **Temperature Profile Controller**: Mathematical interpolation between heating nodes
- **PID Component**: Proportional-Integral-Derivative control algorithm
- **Logger Component**: Thread-safe logging queue
- **SPI Master Component**: SPI bus abstraction

---

## Critical Issues Found

### 🔴 **CRITICAL BUG #1: Missing Event Loop in Coordinator Initialization**
**File**: `coordinator_core.c` (line 31-56)
**Severity**: CRITICAL - Will cause NULL pointer dereference

```c
esp_err_t init_coordinator(const coordinator_config_t *config)
{
    // ...
    g_coordinator_ctx->heating_profiles = (heating_profile_t *)config->profiles;
    
    // ERROR: loop_handle is never created or initialized!
    CHECK_ERR_LOG_RET(init_coordinator_events(),
                      "Failed to initialize coordinator events");
    
    // Later used:
    temp_monitor_config.temperature_events_loop_handle = g_coordinator_ctx->loop_handle; // ← NULL!
}
```

**Issue**: `g_coordinator_ctx->loop_handle` is used without ever being initialized. The event loop must be created with `esp_event_loop_create_default()` or similar.

**Fix Required**:
```c
esp_event_loop_args_t loop_args = {
    .queue_size = CONFIG_ESP_SYSTEM_EVENT_QUEUE_SIZE,
    .task_name = "coordinator_loop",
    .task_priority = uxTaskPriorityGet(NULL),
    .task_stack_size = 4096,
    .task_core_id = tskNO_AFFINITY
};
CHECK_ERR_LOG_RET(esp_event_loop_create(&loop_args, &g_coordinator_ctx->loop_handle),
                  "Failed to create coordinator event loop");
```

---

### 🔴 **CRITICAL BUG #2: Incorrect Function Signature in Heater Controller Task**
**File**: `heater_controller_task.c` (line 54)
**Severity**: CRITICAL - Compilation error/undefined behavior

```c
CHECK_ERR_LOG(post_heater_controller_event(
    HEATER_CONTROLLER_ERROR_OCCURRED, 
    HEATER_CONTROLLER_ERR_GPIO,           // ← WRONG TYPE (enum not pointer)
    sizeof(HEATER_CONTROLLER_ERR_GPIO)    // ← INVALID SIZEOF
), "Failed to post heater controller error event");
```

**Issue**: `post_heater_controller_event()` expects `void *event_data`, but `HEATER_CONTROLLER_ERR_GPIO` is an enum value, not a pointer. Also, `sizeof(enum)` doesn't make sense here.

**Fix Required**:
```c
heater_controller_error_t error = HEATER_CONTROLLER_ERR_GPIO;
CHECK_ERR_LOG(post_heater_controller_event(
    HEATER_CONTROLLER_ERROR_OCCURRED, 
    &error,
    sizeof(heater_controller_error_t)
), "Failed to post heater controller error event");
```

---

### 🔴 **CRITICAL BUG #3: Temperature Data Loss - Unread Ring Buffer**
**File**: `temperature_processor_task.c` (line 47-51)
**Severity**: CRITICAL - Data loss without error indication

```c
size_t samples_count = temp_ring_buffer_pop_all(samples_buffer, CONFIG_TEMP_SENSORS_RING_BUFFER_SIZE);

if (samples_count == 0)
{
    LOGGER_LOG_WARN(TAG, "No temperature samples available for processing");
    continue;  // ← Silent skip, temperature not updated!
}
```

**Issue**: If no samples are available, the heating profile task continues without temperature updates. The coordinator task can run indefinitely without fresh temperature data.

**Fix Required**: Either buffer temperature readings or prevent coordinator task from running until valid data is available.

---

### 🟠 **MAJOR BUG #4: Race Condition - Unprotected Global Variables**
**File**: `heater_controller_core.c` (line 10-11)
**Severity**: HIGH - Race conditions in multithreaded environment

```c
float heater_target_power_level = 0.0f;  // ← Unprotected
esp_event_loop_handle_t heater_controller_event_loop_handle = NULL;  // ← Unprotected
```

**Issue**: These globals are accessed from multiple tasks without synchronization:
- Read in `heater_controller_task()` with semaphore protection
- Written in `set_heater_target_power_level()` with semaphore protection
- BUT the event loop handle is accessed WITHOUT protection in `post_heater_controller_event()`

This can cause crashes when the event loop handle is used while being modified.

**Fix Required**: Use atomic operations or ensure proper synchronization for ALL global variables.

---

### 🟠 **MAJOR BUG #5: Missing Configuration Parameter**
**File**: `coordinator_core.c` (line 41)
**Severity**: HIGH - Hardcoded magic numbers

```c
temp_monitor_config_t temp_monitor_config = {
    .number_of_attached_sensors = 5, // TODO make configurable
    .temperature_events_loop_handle = g_coordinator_ctx->loop_handle
};
```

**Issue**: Number of sensors is hardcoded. If the actual configuration has a different number, the system will fail silently.

**Fix Required**: Pass from `coordinator_config_t` or read from `sdkconfig.h`.

---

### 🟠 **MAJOR BUG #6: Potential Divide By Zero in PID Controller**
**File**: `pid_component.c` (line 33)
**Severity**: MEDIUM - Edge case crash

```c
float pid_controller_compute(float setpoint, float measured_value, float dt)
{
    float error = setpoint - measured_value;
    pid_state.integral += error * dt;
    float derivative = (error - pid_state.previous_error) / dt;  // ← dt could be 0!
```

**Issue**: If `dt` (delta time) is 0, divide-by-zero occurs. In testing, rapid successive calls could trigger this.

**Fix Required**:
```c
if (dt <= 0.0f) {
    return pid_params.output_min;  // Or handle gracefully
}
float derivative = (error - pid_state.previous_error) / dt;
```

---

### 🟡 **MODERATE BUG #7: Incorrect Time Tracking in Heating Profile Task**
**File**: `heating_profile_task.c` (line 24-27)
**Severity**: MEDIUM - Timing inaccuracy

```c
static uint32_t last_wake_time = 0;
static uint32_t current_time = 0;
static uint32_t last_update_duration = 0;

current_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
last_update_duration = current_time - last_wake_time;
```

**Issue**: 
1. Time calculation is only updated when task receives notification (not periodic)
2. `last_wake_time` is never reset, causing timing drift
3. Static variables persist across multiple profile starts/stops

**Fix Required**:
```c
// Use task notification timing or periodic timer
static TickType_t last_wake_tick = 0;
TickType_t current_tick = xTaskGetTickCount();
uint32_t last_update_duration = (current_tick - last_wake_tick) * portTICK_PERIOD_MS;
last_wake_tick = current_tick;
```

---

### 🟡 **MODERATE BUG #8: Uninitialized Profile Index on Shutdown**
**File**: `heating_profile_task.c` (line 176)
**Severity**: MEDIUM - Logic error

```c
g_coordinator_ctx->heating_task_state.profile_index = -1;  // ← -1 assigned to size_t!
```

**Issue**: `profile_index` is declared as `size_t` (unsigned), but -1 is assigned (signed). This creates an invalid value. Later code may use this in array indexing.

**Fix Required**:
```c
g_coordinator_ctx->heating_task_state.profile_index = (size_t)-1;  // Explicit cast
// Or better:
g_coordinator_ctx->heating_task_state.is_active = false;  // Use flag instead
```

---

### 🟡 **MODERATE BUG #9: Missing Event Loop Initialization in Temperature Processor**
**File**: `temperature_processor_core.c` (line 13)
**Severity**: MEDIUM - Incomplete initialization

```c
esp_err_t init_temp_processor(temp_processor_config_t *config)
{
    // ...
    g_temp_processor_ctx->temperature_event_loop_handle = config->temperature_event_loop_handle;
    g_temp_processor_ctx->processor_running = true;
```

**Issue**: Event loop handle is copied but never validated. If NULL, `post_temp_processor_event()` will crash.

**Fix Required**:
```c
if (config->temperature_event_loop_handle == NULL) {
    LOGGER_LOG_ERROR(TAG, "Invalid temperature event loop handle");
    return ESP_ERR_INVALID_ARG;
}
```

---

### 🟡 **MODERATE BUG #10: Semaphore Never Given After Take**
**File**: `heater_controller_task.c` (line 66)
**Severity**: MEDIUM - Deadlock risk

```c
heater_controller_mutex = xSemaphoreCreateMutex();
if (heater_controller_mutex == NULL) {
    return ESP_FAIL;
}
xSemaphoreGive(heater_controller_mutex);  // ← Weird to give immediately after create
```

**Issue**: While technically correct (mutex needs to be "given" initially), this pattern is unusual and error-prone. The initialization should be clearer.

---

## Architectural Issues

### 🏗️ **ARCHITECTURE ISSUE #1: Global Context Pointers Without Initialization Guards**

Multiple components use global pointers without proper lifecycle management:
- `g_coordinator_ctx` - Can be allocated mid-operation
- `g_temp_monitor_ctx` - Same issue
- `g_temp_processor_ctx` - Same issue

**Problem**: If a component is used before initialization or after shutdown, unpredictable behavior occurs.

**Recommendation**: 
```c
// Instead of checking if NULL:
if (g_coordinator_ctx == NULL) {
    return ESP_ERR_INVALID_STATE;
}

// Add a proper state machine:
typedef enum {
    COMPONENT_STATE_UNINITIALIZED,
    COMPONENT_STATE_INITIALIZED,
    COMPONENT_STATE_RUNNING,
    COMPONENT_STATE_SHUTDOWN
} component_state_t;
```

---

### 🏗️ **ARCHITECTURE ISSUE #2: Event Loop Coupling**

The coordinator creates an event loop but doesn't expose the ability to use it for other purposes. Components are tightly coupled to specific event loops:
- `COORDINATOR_TX_EVENT` and `COORDINATOR_RX_EVENT` are tied to one loop
- `TEMP_MONITOR_EVENT` is tied to another
- `HEATER_CONTROLLER_TX_EVENT` and `HEATER_CONTROLLER_RX_EVENT` are tied to another

**Problem**: No unified communication model. Makes testing and composition difficult.

**Recommendation**: 
1. Have coordinator create/manage a single event loop for the entire system
2. All components post to the same loop
3. Or use message queues instead of events for inter-component communication

---

### 🏗️ **ARCHITECTURE ISSUE #3: Missing Error Recovery and State Transitions**

The heating profile task has no error recovery:
```c
profile_controller_error_t err = get_target_temperature_at_time(...);
if (err != PROFILE_CONTROLLER_ERROR_NONE)
{
    LOGGER_LOG_WARN(TAG, "Failed to get target temperature at time...");
    continue;  // ← Just skip this iteration
}
```

**Problem**: If temperature profile calculation fails, heating continues with stale target. No shutdown or alert.

**Recommendation**: 
```c
if (err != PROFILE_CONTROLLER_ERROR_NONE) {
    LOGGER_LOG_ERROR(TAG, "Profile calculation failed - shutting down");
    CHECK_ERR_LOG(stop_heating_profile(), "Failed to stop profile");
    CHECK_ERR_LOG(send_coordinator_error_event(...), "Failed to notify error");
    return;  // Exit task
}
```

---

### 🏗️ **ARCHITECTURE ISSUE #4: Temperature Processor Task Dependency Not Enforced**

The heating profile task depends on temperature readings but has no mechanism to ensure temperature processor is ready:

```c
// In heating_profile_task():
float power_output = pid_controller_compute(g_coordinator_ctx->current_temperature, ...);
// ← What if current_temperature is never updated?
```

**Problem**: If temperature processor task crashes, heating continues with stale data.

**Recommendation**: 
```c
// Add timeout and validation
if (xTaskGetTickCount() - last_temperature_update_time > MAX_TEMPERATURE_STALENESS_MS) {
    LOGGER_LOG_ERROR(TAG, "Temperature data is stale - possible processor failure");
    // Take corrective action
}
```

---

### 🏗️ **ARCHITECTURE ISSUE #5: No Graceful Shutdown Sequencing**

`stop_coordinator()` calls multiple shutdown functions, but they have no defined order:
```c
CHECK_ERR_LOG_RET(shutdown_temp_monitor(), ...);
CHECK_ERR_LOG_RET(shutdown_heater_controller_component(), ...);
CHECK_ERR_LOG_RET(shutdown_coordinator_events(), ...);
CHECK_ERR_LOG_RET(shutdown_temp_processor(), ...);
CHECK_ERR_LOG_RET(stop_heating_profile(), ...);
```

**Problem**: 
1. If heater shutdown fails, we still shut down temperature monitor (heater stays on!)
2. If heating profile doesn't stop cleanly, we proceed anyway
3. No dependencies respected

**Recommendation**: Use a dependency graph or state machine for shutdown.

---

### 🏗️ **ARCHITECTURE ISSUE #6: Uninitialized Ring Buffer**

`temperature_processor_task.c` uses a `samples_buffer` without initialization:
```c
static temp_sample_t samples_buffer[CONFIG_TEMP_SENSORS_RING_BUFFER_SIZE] = {0};
```

**Problem**: While zero-initialized, there's no validation that this is the correct size for the actual number of attached sensors.

---

## Logical Errors & Design Issues

### 🔴 **LOGIC ERROR #1: Heating Profile Task Triggered Without Guarantee of Temperature Data**

**File**: `coordinator_component_events.c` (line 43-52)
**Issue**: 

```c
case PROCESS_TEMPERATURE_EVENT_DATA:
{
    coordinator_current_temperature = *((float *)event_data);
    if (g_coordinator_ctx != NULL && g_coordinator_ctx->is_running)
    {
        xTaskNotifyGive(g_coordinator_ctx->task_handle);
    }
}
```

The heating profile task is woken up every time temperature is processed, even if only ONE sensor was read. But it needs data from ALL sensors to make proper control decisions.

**Recommendation**: Buffer temperature readings and process in batches:
```c
// Wait for complete sample set before notifying
if (completed_sample_set) {
    xTaskNotifyGive(g_coordinator_ctx->task_handle);
}
```

---

### 🔴 **LOGIC ERROR #2: PID Integral Windup Not Prevented**

**File**: `pid_component.c` (line 30-31)
**Issue**: 

```c
pid_state.integral += error * dt;  // ← Unbounded accumulation
float output = (pid_params.kp * error) + (pid_params.ki * pid_state.integral) + ...;
```

The integral term grows without bounds. Over a 1-hour heating profile, the integral could become huge, causing overshoot.

**Recommendation**:
```c
// Clamp integral (anti-windup)
pid_state.integral += error * dt;
if (pid_state.integral > (pid_params.output_max / pid_params.ki)) {
    pid_state.integral = pid_params.output_max / pid_params.ki;
}
if (pid_state.integral < (pid_params.output_min / pid_params.ki)) {
    pid_state.integral = pid_params.output_min / pid_params.ki;
}
```

---

### 🔴 **LOGIC ERROR #3: Profile Duration Calculation Incomplete**

**File**: `heating_profile_task.c` (line 125)
**Issue**: 

```c
g_coordinator_ctx->heating_task_state.total_time_ms = g_coordinator_ctx->heating_profiles[profile_index].first_node->duration_ms;
```

Only the FIRST node's duration is used as total profile time. If the profile has multiple nodes, total time is completely wrong.

**Recommendation**:
```c
uint32_t total_duration = 0;
heating_node_t *node = profile->first_node;
while (node != NULL) {
    total_duration += node->duration_ms;
    node = node->next_node;
}
g_coordinator_ctx->heating_task_state.total_time_ms = total_duration;
```

---

### 🟠 **LOGIC ERROR #4: Stale Data in Coordinator Context**

**File**: `coordinator_core.c` (line 31) & `heating_profile_task.c` (line 127)
**Issue**: 

```c
g_coordinator_ctx->heating_task_state.current_temperature = g_coordinator_ctx->current_temperature;
// ← Set once at startup, never updated in loop!
```

Then in the loop:
```c
float power_output = pid_controller_compute(g_coordinator_ctx->current_temperature, ...);
```

The `heating_task_state.current_temperature` is stale. Meanwhile, `g_coordinator_ctx->current_temperature` is updated by events, but not consistently.

**Recommendation**: Use a single source of truth for current temperature, properly synchronized.

---

### 🟠 **LOGIC ERROR #5: No Maximum Temperature Safety Threshold**

**Issue**: The heating profile controller interpolates target temperatures but has no maximum safe threshold check.

```c
// In heating_profile_task.c
float power_output = pid_controller_compute(g_coordinator_ctx->current_temperature, target_temp, ...);
```

If `target_temp` is calculated as 2000°C due to profile misconfiguration, the heater will be fully on indefinitely.

**Recommendation**:
```c
#define MAX_SAFE_TEMPERATURE_C 1200
if (g_coordinator_ctx->heating_task_state.target_temperature > MAX_SAFE_TEMPERATURE_C) {
    LOGGER_LOG_ERROR(TAG, "Target temperature %.2f exceeds max safe %.2f",
                     g_coordinator_ctx->heating_task_state.target_temperature,
                     MAX_SAFE_TEMPERATURE_C);
    CHECK_ERR_LOG_RET(stop_heating_profile(), "Failed to stop unsafe heating");
    return ESP_ERR_INVALID_ARG;
}
```

---

### 🟠 **LOGIC ERROR #6: Event Handler Called After Context Free**

**File**: `coordinator_core.c` (line 49-67) & `coordinator_component_events.c`
**Issue**: 

In `stop_coordinator()`:
```c
free(g_coordinator_ctx);
g_coordinator_ctx = NULL;
```

But the event handlers are still registered and could be called:
```c
// In event handler
if (g_coordinator_ctx != NULL && g_coordinator_ctx->is_running) {
    xTaskNotifyGive(g_coordinator_ctx->task_handle);  // ← Context was freed!
}
```

**Recommendation**: Unregister event handlers BEFORE freeing context.

---

## Code Quality Issues

### 1. **Inconsistent Null Pointer Checking**
Some functions check for NULL, others don't:
```c
// heating_profile_task.c - No checks
float power_output = pid_controller_compute(g_coordinator_ctx->current_temperature, ...);

// vs temperature_processor.c - Has checks
if (g_temp_processor_ctx == NULL) {
    return ESP_ERR_INVALID_STATE;
}
```

### 2. **Incomplete TODO Comments**
```c
// coordinator_component_events.c line 80
case TEMP_MONITOR_ERR_SENSOR_READ:
    // TODO Handle sensor read error
```
No clear action specified.

### 3. **Magic Numbers Throughout Code**
- Hardcoded stack sizes (8192, 4096)
- Hardcoded sensor count (5)
- Hardcoded task priorities (5)

Should be in `sdkconfig` or component headers.

### 4. **Inconsistent Error Handling**
Some functions return errors, some just log:
```c
// Some return
CHECK_ERR_LOG_RET(init_spi(...), "Failed to init SPI");

// Others just log
CHECK_ERR_LOG(set_heater_target_power_level(...), "Failed to set");
```

### 5. **Memory Leak Risk in init_temp_monitor**
```c
if (g_temp_monitor_ctx == NULL) {
    g_temp_monitor_ctx = calloc(1, sizeof(temp_monitor_context_t));
    // ...
}

// Later if an error occurs:
CHECK_ERR_LOG_RET(init_spi(...), ...);  // ← If this fails, context not freed!
```

---

## Missing Features & Validations

1. **No input validation** in most functions - assumes caller provides valid data
2. **No watchdog timer** - system could hang undetected
3. **No over-temperature shutdown** - could damage equipment
4. **No under-temperature detection** - heating failure goes unnoticed
5. **No sensor validation** - faulty sensors not detected
6. **No rate limiting** - rapid changes not prevented
7. **No power loss recovery** - state not persisted
8. **No timeout protection** - tasks could block indefinitely

---

## Summary of Issues by Severity

| Severity | Count | Issues |
|----------|-------|--------|
| 🔴 CRITICAL | 3 | Null pointer dereference, incorrect function calls, data loss |
| 🟠 MAJOR | 3 | Race conditions, missing config, divide-by-zero |
| 🟡 MODERATE | 4 | Timing inaccuracy, uninitialized values, missing validation |
| 🏗️ ARCHITECTURE | 6 | Design issues in event loops, error handling, dependencies |
| 🔴 LOGIC | 6 | Integral windup, incomplete calculations, unsafe conditions |
| ⚠️ QUALITY | 5+ | Inconsistent patterns, magic numbers, incomplete error handling |

---

## Recommendations (Priority Order)

### Phase 1 (Critical Fixes)
1. ✅ Create event loop in coordinator initialization
2. ✅ Fix heater controller event posting signature
3. ✅ Add temperature data staleness check
4. ✅ Implement divide-by-zero protection in PID
5. ✅ Fix profile duration calculation

### Phase 2 (Major Fixes)
1. ✅ Add synchronization for all shared globals
2. ✅ Implement comprehensive error recovery
3. ✅ Add maximum temperature safety threshold
4. ✅ Fix ring buffer initialization
5. ✅ Proper shutdown sequencing

### Phase 3 (Architectural Improvements)
1. ✅ Unified event loop model
2. ✅ Component state machine
3. ✅ Configuration framework
4. ✅ Proper dependency management
5. ✅ Comprehensive testing

### Phase 4 (Robustness)
1. ✅ Watchdog timer integration
2. ✅ Sensor health monitoring
3. ✅ PID tuning and anti-windup
4. ✅ Rate limiting
5. ✅ Persistent state management

---

## Testing Recommendations

1. **Unit Tests**: Test each component in isolation
2. **Integration Tests**: Test component interactions
3. **Stress Tests**: Rapid start/stop cycles, long-duration heating
4. **Fault Injection**: Simulate sensor failures, missing temperature data
5. **Safety Tests**: Over-temperature conditions, loss of temperature readings

