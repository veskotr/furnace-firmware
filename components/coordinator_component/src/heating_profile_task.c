#include "utils.h"
#include "sdkconfig.h"
#include "esp_timer.h"
#include "temperature_profile_controller.h"
#include "pid_component.h"
#include "coordinator_component_types.h"
#include "coordinator_component_internal.h"

#include <stdatomic.h>
#include <string.h>

#include "commands_dispatcher.h"

static const char* TAG = "COORDINATOR_TASK";

typedef struct
{
    const char* task_name;
    uint32_t stack_size;
    UBaseType_t task_priority;
} CoordinatorTaskConfig_t;

static const CoordinatorTaskConfig_t coordinator_task_config = {
    .task_name = CONFIG_COORDINATOR_TASK_NAME,
    .stack_size = CONFIG_COORDINATOR_TASK_STACK_SIZE,
    .task_priority = CONFIG_COORDINATOR_TASK_PRIORITY
};

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

/**
 * @brief Build and post a coordinator status update event.
 */
static void post_status_update(const coordinator_ctx_t *ctx,
                                float setpoint,
                                float power_output)
{
    coordinator_status_data_t status = {
        .current_temperature = ctx->current_temperature,
        .target_temperature  = setpoint,
        .power_output        = power_output,
        .elapsed_ms          = ctx->heating_task_state.current_time_elapsed_ms,
        .total_ms            = ctx->heating_task_state.estimated_total_duration_ms,
    };
    post_coordinator_event(COORDINATOR_EVENT_STATUS_UPDATE,
                           &status, sizeof(status));
}

static void heating_profile_task(void* args)
{
    coordinator_ctx_t* ctx = (coordinator_ctx_t*)args;

    LOGGER_LOG_INFO(TAG, "Coordinator task started");

    uint32_t last_wake_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
    profile_tick_result_t tick_result = {0};

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

        //TODO extract to helper
        /* ── Apply pending manual-mode target update ──────────────── */
        if (atomic_exchange(&ctx->target_update.pending, false)) {
            int new_target   = ctx->target_update.target_t_c;
            int new_delta_x10 = ctx->target_update.delta_t_per_min_x10;
            float cur_temp    = ctx->current_temperature;

            /* Compute ramp time from current temp to new target at the given delta */
            int abs_diff = new_target > (int)cur_temp
                         ? new_target - (int)cur_temp
                         : (int)cur_temp - new_target;
            uint32_t ramp_ms;
            if (new_delta_x10 > 0 && abs_diff > 0) {
                /* time_min = abs_diff / (delta/10) = abs_diff * 10 / delta */
                uint32_t ramp_min = ((uint32_t)abs_diff * 10U + (uint32_t)new_delta_x10 - 1U)
                                  / (uint32_t)new_delta_x10;  /* ceiling div */
                if (ramp_min < 1) ramp_min = 1;
                ramp_ms = ramp_min * 60U * 1000U;
            } else {
                ramp_ms = 60U * 1000U;  /* 1 min minimum */
            }

            profile_update_stage_target((float)new_target, ramp_ms, cur_temp);

            /* Also update estimated total for time-remaining display */
            ctx->heating_task_state.estimated_total_duration_ms =
                ctx->heating_task_state.current_time_elapsed_ms + ramp_ms;

            LOGGER_LOG_INFO(TAG, "Manual target applied: %d C, delta_x10=%d, ramp=%lu ms",
                            new_target, new_delta_x10, (unsigned long)ramp_ms);
        }

        const profile_controller_error_t err = profile_tick(
            last_update_duration,
            ctx->current_temperature,
            &tick_result);
        ctx->heating_task_state.target_temperature = tick_result.setpoint;

        LOGGER_LOG_INFO(TAG, "Elapsed: %lu ms, Stage: %d, Phase: %d, Setpoint: %.2f C",
                        (unsigned long)ctx->heating_task_state.current_time_elapsed_ms,
                        tick_result.current_stage_index,
                        (int)tick_result.phase,
                        tick_result.setpoint);

        if (err != PROFILE_CONTROLLER_ERROR_NONE)
        {
            LOGGER_LOG_WARN(TAG, "profile_tick error: %d", err);
            continue;
        }

        /* Log stage transitions */
        if (tick_result.stage_changed) {
            LOGGER_LOG_INFO(TAG, "Stage changed → stage %d, phase %d",
                            tick_result.current_stage_index, (int)tick_result.phase);
        }

        /* Warn if a stage was forced to advance */
        if (tick_result.extension_warning) {
            LOGGER_LOG_WARN(TAG, "Stage extension limit reached — forced advance");
        }

        // Calculate power output based on current and target temperature
        float power_output = pid_controller_compute(tick_result.setpoint,
                                                    ctx->current_temperature,
                                                    last_update_duration);
        if (tick_result.stage_changed)
        {
            heater_command_data_t cmd = {
                .type = COMMAND_TYPE_HEATER_CLEAR,
                .power_level = power_output
            };

            command_t command = {
                .target = COMMAND_TARGET_HEATER,
                .data = &cmd,
                .data_size = sizeof(cmd)
            };
            post_heater_controller_command(&command);
        }

        heater_command_data_t cmd = {
            .type = COMMAND_TYPE_HEATER_SET_POWER,
            .power_level = power_output
        };

        command_t command = {
            .target = COMMAND_TARGET_HEATER,
            .data = &cmd,
            .data_size = sizeof(cmd)
        };

        post_heater_controller_command(&command);

        /* Push status update to HMI (elapsed, remaining, power, temps) */
        post_status_update(ctx, tick_result.setpoint, power_output);

        /* ── Profile completion — driven by profile_tick() state machine ── */
        if (tick_result.profile_complete) {
            LOGGER_LOG_INFO(TAG, "Profile complete (profile_tick): temp %.1f C",
                            ctx->current_temperature);

            heater_command_data_t cmd = {
                .type = COMMAND_TYPE_HEATER_CLEAR,
            };

            command_t command = {
                .target = COMMAND_TARGET_HEATER,
                .data = &cmd,
                .data_size = sizeof(cmd)
            };

            post_heater_controller_command(&command);

            /* Set power to zero before signaling completion */
            float zero_power = 0.0f;

            cmd.type = COMMAND_TYPE_HEATER_SET_POWER;
            cmd.power_level = zero_power;

            post_heater_controller_command(&command);

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

esp_err_t start_heating_profile(coordinator_ctx_t* ctx, const program_draft_t *program, int cooldown_rate_x10)
{
    if (ctx->task_handle != NULL && ctx->running)
    {
        return ESP_OK;
    }

    if (!program) {
        LOGGER_LOG_ERROR(TAG, "Program is NULL");
        return ESP_ERR_INVALID_ARG;
    }

    // Store a local copy of the program for the task lifetime
    memcpy(&ctx->run_program, program, sizeof(ctx->run_program));
    ctx->has_program = true;
    const program_draft_t *prog = &ctx->run_program;

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
    if (last_stage_temp > 0.0f && cooldown_rate_x10 > 0) {
        float cooldown_min = (last_stage_temp * 10.0f) / (float)cooldown_rate_x10;
        if (cooldown_min < 1.0f) cooldown_min = 1.0f;
        total_ms += (uint32_t)(cooldown_min * 60.0f * 1000.0f);
    }

    ctx->heating_task_state.is_active = true;
    ctx->heating_task_state.is_paused = false;
    ctx->heating_task_state.is_completed = false;
    ctx->heating_task_state.current_time_elapsed_ms = 0;
    ctx->heating_task_state.estimated_total_duration_ms = total_ms;
    ctx->heating_task_state.heating_stages_duration_ms = stages_ms;
    ctx->heating_task_state.current_temperature = ctx->current_temperature;
    ctx->heating_task_state.heating_element_on = false;
    ctx->heating_task_state.fan_on = false;

    const temp_profile_config_t temp_profile_config = {
        .program = prog,
        .initial_temperature = ctx->current_temperature,
        .cooldown_rate_x10 = cooldown_rate_x10
    };

    const profile_controller_error_t err = load_heating_profile(temp_profile_config);

    if (err != PROFILE_CONTROLLER_ERROR_NONE)
    {
        LOGGER_LOG_ERROR(TAG, "Failed to load heating profile '%s', error: %d",
                         prog->name,
                         err);
        return ESP_FAIL;
    }

    /* Set running BEFORE task creation — the new task checks ctx->running
     * in its while-loop condition and may be scheduled before we return. */
    ctx->running = true;

    CHECK_ERR_LOG_CALL_RET(xTaskCreate(
                               heating_profile_task,
                               coordinator_task_config.task_name,
                               coordinator_task_config.stack_size,
                               ctx,
                               coordinator_task_config.task_priority,
                               &ctx->task_handle) == pdPASS
                           ? ESP_OK
                           : ESP_FAIL,
                           ctx->task_handle = NULL; ctx->running = false,
                           "Failed to create coordinator task");

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

esp_err_t get_heating_task_state(const coordinator_ctx_t* ctx)
{
    //TODO Post state event
    return ESP_FAIL;
}

esp_err_t get_current_heating_profile(const coordinator_ctx_t* ctx)
{
    //TODO Post current profile event
    return ESP_FAIL;
}

esp_err_t stop_heating_profile(coordinator_ctx_t *ctx)
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
    heater_command_data_t heater_cmd = {
        .type = COMMAND_TYPE_HEATER_SET_POWER,
        .power_level = zero_power
    };

    command_t cmd = {
        .target = COMMAND_TARGET_HEATER,
        .data = &heater_cmd,
        .data_size = sizeof(heater_cmd)
    };
    commands_dispatcher_dispatch_command(&cmd);

    shutdown_profile_controller();

    LOGGER_LOG_INFO(TAG, "Coordinator task shutdown complete");

    return ESP_OK;
}
