#ifndef COORDINATOR_COMPONENT_INTERNAL_H
#define COORDINATOR_COMPONENT_INTERNAL_H

#include "esp_err.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "core_types.h"
#include "coordinator_component_types.h"
#include "event_registry.h"

#define INVALID_PROFILE_INDEX (size_t) 0xFFFFFFFF;

typedef struct
{
    TaskHandle_t task_handle;
    esp_timer_handle_t pid_tick_timer;  // Periodic timer driving the PID control loop

    program_draft_t run_program;       // Copy of the program being executed
    bool has_program;               // True after a program has been loaded

    bool running;
    bool paused;
    float current_temperature;

    heating_task_state_t heating_task_state;

    bool events_initialized;
} coordinator_ctx_t;

// ============================================
// Event handling and posting functions
// ============================================
esp_err_t init_coordinator_events(coordinator_ctx_t* ctx);

esp_err_t shutdown_coordinator_events(coordinator_ctx_t* ctx);

esp_err_t post_coordinator_error_event(coordinator_event_id_t event_type, const esp_err_t* event_data,
                                       coordinator_error_code_t coordinator_error_code);
esp_err_t post_coordinator_event(coordinator_event_id_t event_type, void* event_data, size_t event_data_size);

esp_err_t post_heater_controller_event(heater_controller_event_t event_type, void* event_data, size_t event_data_size);


// ============================================
// Heating profile task management functions
// ============================================
esp_err_t start_heating_profile(coordinator_ctx_t* ctx, const program_draft_t *program);

esp_err_t pause_heating_profile(coordinator_ctx_t* ctx);

esp_err_t resume_heating_profile(coordinator_ctx_t* ctx);

esp_err_t get_heating_task_state(const coordinator_ctx_t* ctx);

esp_err_t get_current_heating_profile(const coordinator_ctx_t* ctx);

esp_err_t stop_heating_profile(coordinator_ctx_t* ctx);

#endif // COORDINATOR_COMPONENT_INTERNAL_H
