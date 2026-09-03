#pragma once

#include "commands_dispatcher.h"
#include "esp_err.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "core_types.h"
#include "coordinator_component_types.h"
#include "event_registry.h"

#define INVALID_PROFILE_INDEX ((size_t) 0xFFFFFFFF)

/**
 * @brief Pending live-update mailbox for manual-mode target changes.
 *
 * Written by the event-handler task, read+cleared by the profile task
 * on every PID tick.  The atomic flag guarantees visibility across cores.
 */
typedef struct
{
    int target_t_c; ///< Desired target temperature (°C)
    int delta_t_per_min_x10; ///< Desired heating rate (x10)
    bool pending; ///< True when new values are waiting
} pending_target_update_t;

typedef struct
{
    TaskHandle_t task_handle;
    esp_timer_handle_t pid_tick_timer; // Periodic timer driving the PID control loop

    program_draft_t run_program; // Copy of the program being executed
    bool has_program; // True after a program has been loaded

    bool running;
    bool paused;
    float current_temperature;

    heating_task_state_t heating_task_state;

    bool events_initialized;

    pending_target_update_t target_update; ///< Live manual-mode mailbox

    /* Tracking for dynamic remaining-time recompute.
     * When a stage advances earlier than its rate-derived planned duration
     * (e.g. PID overshoots and hits the target ahead of schedule), the
     * unused portion is deducted from estimated_total_duration_ms so the
     * displayed remaining time shrinks. */
    int      last_profile_stage_index;       ///< Last seen tick stage index (-1 if none/cooldown)
    uint32_t elapsed_at_stage_start_ms;      ///< Wall-clock elapsed when the current stage began
    uint32_t last_stage_planned_ms;          ///< profile_tick's runtime stage_planned_ms for the
                                              ///< currently-active stage (rate-derived, not t_min).
                                              ///< Used by update_estimate_on_stage_change so the
                                              ///< deduction matches the duration we actually
                                              ///< budgeted in calculate_program_duration_ms.
} coordinator_ctx_t;

// ============================================
// Event handling and posting functions
// ============================================
esp_err_t init_coordinator_events(coordinator_ctx_t* ctx);

esp_err_t shutdown_coordinator_events(coordinator_ctx_t* ctx);

esp_err_t post_coordinator_error_event(coordinator_event_id_t event_type, const esp_err_t* event_data,
                                       coordinator_error_code_t coordinator_error_code);
esp_err_t post_coordinator_event(coordinator_event_id_t event_type, void* event_data, size_t event_data_size);

esp_err_t post_heater_controller_command(command_t *command);


// ============================================
// Heating profile task management functions
// ============================================
esp_err_t start_heating_profile(coordinator_ctx_t* ctx, const program_draft_t* program, int cooldown_rate_x10);

esp_err_t pause_heating_profile(coordinator_ctx_t* ctx);

esp_err_t resume_heating_profile(coordinator_ctx_t* ctx);

esp_err_t get_heating_task_state(const coordinator_ctx_t* ctx);

esp_err_t get_current_heating_profile(const coordinator_ctx_t* ctx);

esp_err_t stop_heating_profile(coordinator_ctx_t* ctx);
