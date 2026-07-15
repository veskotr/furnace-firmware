#pragma once

#include "commands_dispatcher.h"
#include "esp_err.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "core_types.h"
#include "coordinator_component_types.h"
#include "event_registry.h"
#include <stdatomic.h>

#define INVALID_PROFILE_INDEX ((size_t) 0xFFFFFFFF)

/**
 * @brief Pending live-update mailbox for manual-mode target changes.
 *
 * Written by the event-handler task, read+cleared by the profile task
 * on every PID tick.  target_update_mutex serializes the complete mailbox
 * transaction so target and rate cannot be observed from different updates.
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
    SemaphoreHandle_t exit_semaphore;
    esp_timer_handle_t pid_tick_timer; // Periodic timer driving the PID control loop
    esp_timer_handle_t sensor_data_expiry_timer; // One-shot aggregate freshness deadline

    program_draft_t run_program; // Copy of the program being executed
    bool has_program; // True after a program has been loaded

    bool running;
    bool paused;
    float current_temperature;
    SemaphoreHandle_t temperature_mutex;
    /* Set only after a temperature processor event passes its invalid-value
     * guard. This is an atomic start gate, not a freshness contract. */
    atomic_bool has_valid_temperature;
    atomic_bool sensor_data_inhibited;
    /* Set by the one-shot expiry callback before it directly inhibits the
     * heater. Cleared only after a later fresh aggregate is accepted. */
    atomic_bool sensor_data_expired;
    /* Protected by temperature_mutex; records the accepted aggregate lease
     * and serializes recovery-count updates between the event and control
     * tasks. */
    TickType_t last_valid_aggregate_tick;
    bool has_valid_aggregate;
    uint8_t sensor_recovery_count;

    heating_task_state_t heating_task_state;

    bool events_initialized;

    SemaphoreHandle_t target_update_mutex;
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

static inline float coordinator_get_current_temperature(const coordinator_ctx_t* ctx)
{
    float temperature = 0.0f;
    if (ctx != NULL && ctx->temperature_mutex != NULL &&
        xSemaphoreTake(ctx->temperature_mutex, portMAX_DELAY) == pdTRUE)
    {
        temperature = ctx->current_temperature;
        xSemaphoreGive(ctx->temperature_mutex);
    }
    return temperature;
}

// ============================================
// Event handling and posting functions
// ============================================
esp_err_t init_coordinator_events(coordinator_ctx_t* ctx);

esp_err_t shutdown_coordinator_events(coordinator_ctx_t* ctx);
esp_err_t init_sensor_data_expiry_timer(coordinator_ctx_t* ctx);
esp_err_t shutdown_sensor_data_expiry_timer(coordinator_ctx_t* ctx);
esp_err_t arm_sensor_data_expiry_timer(coordinator_ctx_t* ctx, TickType_t sample_tick);
bool coordinator_sensor_data_is_fresh(coordinator_ctx_t* ctx);
void coordinator_inhibit_for_sensor_data_expiry(coordinator_ctx_t* ctx);

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
