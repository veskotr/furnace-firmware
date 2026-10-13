#pragma once

#include "esp_event.h"
#include <stdint.h>
#include "core_types.h"

// ============================================================================
// COORDINATOR COMPONENT EVENTS
// ============================================================================

ESP_EVENT_DECLARE_BASE(COORDINATOR_EVENT); // Event base declaration

typedef enum
{
    // RX Events (external -> coordinator)
    COORDINATOR_EVENT_PROFILE_STARTED = 0,
    COORDINATOR_EVENT_PROFILE_PAUSED,
    COORDINATOR_EVENT_PROFILE_RESUMED,
    COORDINATOR_EVENT_PROFILE_STOPPED,
    COORDINATOR_EVENT_PROFILE_COMPLETED,
    COORDINATOR_EVENT_STATUS_UPDATE,
    COORDINATOR_EVENT_CURRENT_PROFILE,
    COORDINATOR_EVENT_NODE_STARTED,
    COORDINATOR_EVENT_NODE_COMPLETED,
    COORDINATOR_EVENT_ERROR_OCCURRED,
} coordinator_event_id_t;

// Event data structures for coordinator
typedef struct
{
    program_draft_t program;   // Full program to execute
    int cooldown_rate_x10;  // User-configured cooldown rate (x10)
} coordinator_start_profile_data_t;

typedef enum
{
    COORDINATOR_ERROR_NONE = 0,
    COORDINATOR_ERROR_PROFILE_NOT_PAUSED,
    COORDINATOR_ERROR_PROFILE_NOT_RESUMED,
    COORDINATOR_ERROR_PROFILE_NOT_STOPPED,
    COORDINATOR_ERROR_NOT_STARTED,
    COORDINATOR_ERROR_STALL_DETECTED,
    COORDINATOR_ERROR_HOLD_DEVIATION,
} coordinator_error_code_t;

typedef struct
{
    coordinator_error_code_t error_code;
    esp_err_t esp_error_code;

    /* Diagnostic context for run-time faults (stall / hold deviation) so the
     * HMI can tell the worker WHERE and WHEN it failed. Left zero by the
     * control-flow errors (start/pause/resume/stop), which don't use them. */
    float    temperature_c;     ///< Chamber temp when the fault tripped
    float    setpoint_c;        ///< Active setpoint when the fault tripped
    int8_t   stage_index;       ///< 0-based active-stage ordinal, -1 if N/A
    uint32_t fault_elapsed_ms;  ///< How long the fault condition persisted
} coordinator_error_data_t;

/**
 * @brief Live manual-mode target update.
 *
 * Posted by the HMI when the user adjusts temp / delta buttons during
 * a running manual program.  The coordinator applies the change on the
 * next PID tick without stopping the profile.
 */

/**
 * @brief Stage phase, mirrored from temperature_profile_types.h so HMI
 *        code can switch on it without pulling that header in.
 */
typedef enum {
    COORD_STAGE_PHASE_HEATING  = 0,
    COORD_STAGE_PHASE_HOLDING  = 1,
    COORD_STAGE_PHASE_COOLING  = 2,
    COORD_STAGE_PHASE_COOLDOWN = 3,
    COORD_STAGE_PHASE_COMPLETE = 4,
} coordinator_stage_phase_t;

typedef struct
{
    float current_temperature;
    float target_temperature;
    float power_output;         // 0.0 – 1.0  (PID output)
    uint32_t elapsed_ms;
    uint32_t total_ms;

    /* Stage tracking (added so HMI can show e.g. "S2/5 RAMP 150C"). */
    int8_t  stage_index;                /* 0..N-1 active stage; -1 in cooldown/complete */
    int8_t  total_active_stages;        /* Count of is_set stages in the program */
    uint8_t phase;                      /* coordinator_stage_phase_t */
    uint32_t stage_remaining_ms;        /* Time left in the active stage (HOLDING etc.); 0 if N/A */
} coordinator_status_data_t;

// ============================================================================
// HEATER CONTROLLER COMPONENT EVENTS
// ============================================================================

ESP_EVENT_DECLARE_BASE(HEATER_CONTROLLER_EVENT);

typedef enum
{
    HEATER_CONTROLLER_ERROR_OCCURRED = 0,
    HEATER_CONTROLLER_HEATER_TOGGLED,
    HEATER_CONTROLLER_STATUS_REPORT_RESPONSE
} heater_controller_event_t;

// ============================================================================
// HEALTH MONITOR COMPONENT EVENTS
// ============================================================================

ESP_EVENT_DECLARE_BASE(HEALTH_MONITOR_EVENT);

typedef enum
{
    HEALTH_MONITOR_EVENT_HEARTBEAT = 0,
    HEALTH_MONITOR_EVENT_REGISTER,
    HEALTH_MONITOR_EVENT_UNREGISTER,
} health_monitor_event_id_t;

typedef struct
{
    uint16_t component_id;
    const char *component_name;
    TickType_t timeout_ticks;
} health_monitor_data_t;

// ============================================================================
// TEMPERATURE PROCESSOR EVENTS
// ============================================================================

ESP_EVENT_DECLARE_BASE(TEMP_PROCESSOR_EVENT);

#define PROCESS_TEMPERATURE_EVENT_DATA 0
#define PROCESS_TEMPERATURE_VALIDITY_EVENT_DATA 1

// ============================================================================
// FURNACE ERROR EVENTS
// ============================================================================
ESP_EVENT_DECLARE_BASE(FURNACE_ERROR_EVENT);

#define FURNACE_ERROR_EVENT_ID 0

// ============================================================================
// DEVICE MANAGER EVENTS
// ============================================================================
ESP_EVENT_DECLARE_BASE(DEVICE_MANAGER_EVENT);

#define DEVICE_MANAGER_UPDATED_EVENT 0

// ============================================================================
// INITIALIZATION FUNCTION
// ============================================================================

/**
 * @brief Initialize all event bases
 *
 * Must be called after event_manager_init() but before components use events
 *
 * @return ESP_OK on success
 */
esp_err_t event_registry_init(void);
