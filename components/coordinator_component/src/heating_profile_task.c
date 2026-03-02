#include "utils.h"
#include "sdkconfig.h"
#include "esp_timer.h"
#include "temperature_profile_controller.h"
#include "pid_component.h"
#include "coordinator_component_types.h"
#include "coordinator_component_internal.h"
#include "nextion_hmi.h"

static const char* TAG = "COORDINATOR_TASK";

typedef struct
{
    const char* task_name;
    uint32_t stack_size;
    UBaseType_t task_priority;
} CoordinatorTaskConfig_t;

/**
 * @brief esp_timer callback — wakes the heating profile task at a fixed interval.
 *        Runs in ISR context on ESP32, so only xTaskNotifyGive is safe here.
 */
static void pid_tick_timer_cb(void* arg)
{
    coordinator_ctx_t* ctx = (coordinator_ctx_t*)arg;
    if (ctx->task_handle != NULL)
    {
        xTaskNotifyGive(ctx->task_handle);
    }
}

static void heating_profile_task(void* args)
{
    coordinator_ctx_t* ctx = (coordinator_ctx_t*)args;

    LOGGER_LOG_INFO(TAG, "Coordinator task started");

    uint32_t last_wake_time = xTaskGetTickCount() * portTICK_PERIOD_MS;

    while (ctx->running)
    {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        uint32_t last_update_duration = 0;
        if (!ctx->paused)
        {
            uint32_t current_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
            last_update_duration = current_time - last_wake_time;
            ctx->heating_task_state.current_time_elapsed_ms += last_update_duration;
            last_wake_time = current_time;
        }
        else
        {
            /* While paused, keep last_wake_time current so the first
               iteration after resume doesn't accumulate paused time. */
            last_wake_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
            continue;   /* Skip PID computation while paused */
        }

        const profile_controller_error_t err = get_target_temperature_at_time(
            ctx->heating_task_state.current_time_elapsed_ms,
            &ctx->heating_task_state.target_temperature);
        LOGGER_LOG_INFO(TAG, "Elapsed Time: %d ms, Target Temperature: %.2f C",
                        ctx->heating_task_state.current_time_elapsed_ms,
                        ctx->heating_task_state.target_temperature);
        // TODO: Handle error cases
        if (err != PROFILE_CONTROLLER_ERROR_NONE)
        {
            LOGGER_LOG_WARN(TAG, "Failed to get target temperature at time %d ms, error: %d",
                            ctx->heating_task_state.current_time_elapsed_ms,
                            err);
            continue;
        }
        // Calculate power output based on current and target temperature
        float power_output = pid_controller_compute(ctx->current_temperature,
                                                    ctx->heating_task_state.target_temperature,
                                                    last_update_duration);

        // Turn on/off heaters based on power output
        CHECK_ERR_LOG(
            post_heater_controller_event(HEATER_CONTROLLER_SET_POWER_LEVEL, &power_output, sizeof(power_output)),
            "Failed to set heater target power level");

        /* Push status update to HMI (elapsed, remaining, power, temps) */
        coordinator_status_data_t status = {
            .profile_index      = ctx->heating_task_state.profile_index,
            .current_temperature = ctx->current_temperature,
            .target_temperature  = ctx->heating_task_state.target_temperature,
            .power_output        = power_output,
            .elapsed_ms          = ctx->heating_task_state.current_time_elapsed_ms,
            .total_ms            = ctx->heating_task_state.total_time_ms,
        };
        post_coordinator_event(COORDINATOR_EVENT_STATUS_UPDATE,
                               &status, sizeof(status));

        /* ── Profile completion check ────────────────────────────────
         * Complete when BOTH conditions are true:
         *   1. We've passed all heating stages (in cooldown phase)
         *   2. Actual measured temperature is below the threshold
         * ──────────────────────────────────────────────────────────── */
        bool in_cooldown = ctx->heating_task_state.current_time_elapsed_ms
                           > ctx->heating_task_state.stages_only_ms;
        bool temp_below_threshold = ctx->current_temperature
                                    < (float)CONFIG_COORDINATOR_PROFILE_COMPLETE_TEMP_C;

        if (in_cooldown && temp_below_threshold) {
            LOGGER_LOG_INFO(TAG, "Profile complete: in cooldown and temp %.1f C < %d C",
                            ctx->current_temperature,
                            CONFIG_COORDINATOR_PROFILE_COMPLETE_TEMP_C);

            /* Set power to zero before signaling completion */
            float zero_power = 0.0f;
            post_heater_controller_event(HEATER_CONTROLLER_SET_POWER_LEVEL,
                                         &zero_power, sizeof(zero_power));

            ctx->heating_task_state.is_completed = true;
            post_coordinator_event(COORDINATOR_EVENT_PROFILE_COMPLETED, NULL, 0);
            ctx->running = false;
            break;
        }

        LOGGER_LOG_INFO(TAG, "Coordinator notified. Current Temperature: %.2f C",
                        ctx->current_temperature);
    }

    LOGGER_LOG_INFO(TAG, "Temperature monitor task exiting");
    stop_heating_profile(ctx);
    ctx->task_handle = NULL;
    vTaskDelete(NULL);
}

static const CoordinatorTaskConfig_t coordinator_task_config = {
    .task_name = "COORDINATOR_TASK",
    .stack_size = 8192,
    .task_priority = 5
};

esp_err_t start_heating_profile(coordinator_ctx_t* ctx, const size_t profile_index)
{
    if (ctx->task_handle != NULL && ctx->running)
    {
        return ESP_OK;
    }

    if (profile_index != 0)
    {
        LOGGER_LOG_ERROR(TAG, "Invalid program index: %d", profile_index);
        return ESP_ERR_INVALID_ARG;
    }

    // Fetch a fresh, thread-safe copy of the run slot
    hmi_get_run_program(&ctx->run_program);
    ctx->has_program = true;
    const ProgramDraft *prog = &ctx->run_program;

    // Calculate total duration from stages
    uint32_t stages_ms = 0;
    float last_stage_temp = ctx->current_temperature;
    for (int i = 0; i < PROGRAMS_TOTAL_STAGE_COUNT; ++i) {
        if (prog->stages[i].is_set) {
            stages_ms += (uint32_t)prog->stages[i].t_min * 60U * 1000U;
            last_stage_temp = (float)prog->stages[i].target_t_c;
        }
    }
    uint32_t total_ms = stages_ms;
    /* Add implicit cooldown duration */
    if (last_stage_temp > 0.0f && CONFIG_NEXTION_COOLDOWN_RATE_X10 > 0) {
        float cooldown_min = (last_stage_temp * 10.0f) / (float)CONFIG_NEXTION_COOLDOWN_RATE_X10;
        if (cooldown_min < 1.0f) cooldown_min = 1.0f;
        total_ms += (uint32_t)(cooldown_min * 60.0f * 1000.0f);
    }

    ctx->heating_task_state.profile_index = profile_index;
    ctx->heating_task_state.is_active = true;
    ctx->heating_task_state.is_paused = false;
    ctx->heating_task_state.is_completed = false;
    ctx->heating_task_state.current_time_elapsed_ms = 0;
    ctx->heating_task_state.total_time_ms = total_ms;
    ctx->heating_task_state.stages_only_ms = stages_ms;
    ctx->heating_task_state.current_temperature = ctx->current_temperature;
    ctx->heating_task_state.heating_element_on = false;
    ctx->heating_task_state.fan_on = false;

    const temp_profile_config_t temp_profile_config = {
        .program = prog,
        .initial_temperature = ctx->current_temperature
    };

    const profile_controller_error_t err = load_heating_profile(temp_profile_config);

    if (err != PROFILE_CONTROLLER_ERROR_NONE)
    {
        LOGGER_LOG_ERROR(TAG, "Failed to load heating profile index %d, error: %d",
                         profile_index,
                         err);
        return ESP_FAIL;
    }

    CHECK_ERR_LOG_CALL_RET(xTaskCreate(
                               heating_profile_task,
                               coordinator_task_config.task_name,
                               coordinator_task_config.stack_size,
                               ctx,
                               coordinator_task_config.task_priority,
                               &ctx->task_handle) == pdPASS
                           ? ESP_OK
                           : ESP_FAIL,
                           ctx->task_handle = NULL,
                           "Failed to create coordinator task");
    ctx->running = true;

    /* Start periodic PID tick timer */
    const esp_timer_create_args_t timer_args = {
        .callback = pid_tick_timer_cb,
        .arg = ctx,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "pid_tick"
    };
    esp_err_t timer_err = esp_timer_create(&timer_args, &ctx->pid_tick_timer);
    if (timer_err == ESP_OK) {
        esp_timer_start_periodic(ctx->pid_tick_timer,
                                 (uint64_t)CONFIG_COORDINATOR_PID_TICK_INTERVAL_MS * 1000ULL);
        LOGGER_LOG_INFO(TAG, "PID tick timer started (%d ms)", CONFIG_COORDINATOR_PID_TICK_INTERVAL_MS);
    } else {
        LOGGER_LOG_ERROR(TAG, "Failed to create PID tick timer: %s", esp_err_to_name(timer_err));
    }

    LOGGER_LOG_INFO(TAG, "Coordinator task initialized");

    return ESP_OK;
}

esp_err_t pause_heating_profile(coordinator_ctx_t* ctx)
{
    if (!ctx->running)
    {
        return ESP_ERR_INVALID_STATE;
    }

    ctx->paused = true;
    ctx->heating_task_state.is_paused = true;
    LOGGER_LOG_INFO(TAG, "Heating profile paused");

    return ESP_OK;
}

esp_err_t resume_heating_profile(coordinator_ctx_t* ctx)
{
    if (!ctx->running)
    {
        return ESP_ERR_INVALID_STATE;
    }

    /* Reset PID state so it starts fresh from the current temperature.
     * During pause the furnace may have cooled — stale integral and
     * derivative terms would cause a large power spike on resume. */
    pid_controller_reset();

    /* Update the current temperature snapshot so the first PID tick
     * after resume uses the real measured value. */
    ctx->heating_task_state.current_temperature = ctx->current_temperature;

    ctx->paused = false;
    ctx->heating_task_state.is_paused = false;

    LOGGER_LOG_INFO(TAG, "Heating profile resumed (temp=%.1f C)",
                    ctx->current_temperature);

    return ESP_OK;
}

void get_heating_task_state(const coordinator_ctx_t* ctx, heating_task_state_t* state_out)
{
    if (state_out == NULL)
    {
        return;
    }

    *state_out = ctx->heating_task_state;
}

void get_current_heating_profile(const coordinator_ctx_t* ctx, size_t* profile_index_out)
{
    if (profile_index_out == NULL)
    {
        return;
    }

    *profile_index_out = ctx->heating_task_state.profile_index;
}

esp_err_t stop_heating_profile(coordinator_ctx_t* ctx)
{
    if (!ctx->running)
    {
        return ESP_OK;
    }

    /* Stop the periodic PID tick timer first */
    if (ctx->pid_tick_timer != NULL)
    {
        esp_timer_stop(ctx->pid_tick_timer);
        esp_timer_delete(ctx->pid_tick_timer);
        ctx->pid_tick_timer = NULL;
        LOGGER_LOG_INFO(TAG, "PID tick timer stopped");
    }

    ctx->running = false;

    if (ctx->task_handle != NULL)
    {
        xTaskNotifyGive(ctx->task_handle);
    }
    ctx->heating_task_state.profile_index = INVALID_PROFILE_INDEX;
    ctx->heating_task_state.is_paused = false;

    /* Zero out heater power so the heater PWM task stops toggling */
    float zero_power = 0.0f;
    post_heater_controller_event(HEATER_CONTROLLER_SET_POWER_LEVEL,
                                 &zero_power, sizeof(zero_power));

    shutdown_profile_controller();

    LOGGER_LOG_INFO(TAG, "Coordinator task shutdown complete");

    return ESP_OK;
}
