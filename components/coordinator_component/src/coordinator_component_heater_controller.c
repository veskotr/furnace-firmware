#include "utils.h"
#include "sdkconfig.h"
#include "esp_timer.h"
#include "temperature_profile_controller.h"
#include "pid_component.h"
#include "coordinator_component_types.h"
#include "coordinator_component_internal.h"
#include "event_manager.h"
#include "event_registry.h"
#include "furnace_error_types.h"
#include "error_manager.h"

#include <stdatomic.h>
#include <string.h>

#include "commands_dispatcher.h"

static const char* TAG = "COORDINATOR_TASK";

static const health_monitor_data_t coordinator_health_data = {
    .component_id = CONFIG_COORDINATOR_COMPONENT_ID,
    .component_name = "Coordinator",
    .timeout_ticks = pdMS_TO_TICKS(CONFIG_COORDINATOR_HEARTBEAT_TIMEOUT_MS),
};

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
 * @brief esp_timer callback — wakes the heater controller task at a fixed interval.
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

static void send_heater_command(heater_command_type_t type, float power_level)
{
    heater_command_data_t cmd = {
        .type = type,
        .power_level = power_level
    };
    command_t command = {
        .target = COMMAND_TARGET_HEATER,
        .data = &cmd,
        .data_size = sizeof(cmd)
    };
    post_heater_controller_command(&command);
}

static void kill_heater(void)
{
    send_heater_command(COMMAND_TYPE_HEATER_CLEAR, 0.0f);
    send_heater_command(COMMAND_TYPE_HEATER_SET_POWER, 0.0f);
}

static void apply_pending_target_update(coordinator_ctx_t *ctx)
{
    if (!atomic_exchange(&ctx->target_update.pending, false)) {
        return;
    }

    int new_target    = ctx->target_update.target_t_c;
    int new_delta_x10 = ctx->target_update.delta_t_per_min_x10;
    float cur_temp    = ctx->current_temperature;

    int abs_diff = new_target > (int)cur_temp
                 ? new_target - (int)cur_temp
                 : (int)cur_temp - new_target;
    uint32_t ramp_ms;
    if (new_delta_x10 > 0 && abs_diff > 0) {
        uint32_t ramp_min = ((uint32_t)abs_diff * 10U + (uint32_t)new_delta_x10 - 1U)
                          / (uint32_t)new_delta_x10;  /* ceiling div */
        if (ramp_min < 1) ramp_min = 1;
        ramp_ms = ramp_min * 60U * 1000U;
    } else {
        ramp_ms = 60U * 1000U;  /* 1 min minimum */
    }

    profile_update_stage_target((float)new_target, ramp_ms, cur_temp);

    ctx->heating_task_state.estimated_total_duration_ms =
        ctx->heating_task_state.current_time_elapsed_ms + ramp_ms;

    LOGGER_LOG_INFO(TAG, "Manual target applied: %d C, delta_x10=%d, ramp=%lu ms",
                    new_target, new_delta_x10, (unsigned long)ramp_ms);
}

static void handle_profile_completion(coordinator_ctx_t *ctx)
{
    LOGGER_LOG_INFO(TAG, "Profile complete (profile_tick): temp %.1f C",
                    ctx->current_temperature);

    kill_heater();

    ctx->heating_task_state.is_completed = true;
    post_coordinator_event(COORDINATOR_EVENT_PROFILE_COMPLETED, NULL, 0);
    ctx->running = false;
}

static void heater_controller_task(void* args)
{
    coordinator_ctx_t* ctx = (coordinator_ctx_t*)args;

    LOGGER_LOG_INFO(TAG, "Coordinator task started");

    uint32_t last_wake_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
    profile_tick_result_t tick_result = {0};

    /* Stall detection state — tracks expected vs actual temp rise during HEATING */
    float  stall_start_temp     = ctx->current_temperature;
    float  stall_start_setpoint = 0.0f;
    uint32_t stall_elapsed_ms   = 0;
    bool   stall_tracking       = false;

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

        apply_pending_target_update(ctx);

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

        if (err == PROFILE_CONTROLLER_ERROR_THRESHOLD_EXCEEDED)
        {
            LOGGER_LOG_ERROR(TAG, "EMERGENCY STOP: temperature overshoot threshold exceeded!");

            kill_heater();

            /* Post critical furnace error */
            furnace_error_t furnace_err = {
                .severity = SEVERITY_CRITICAL,
                .source = SOURCE_COORDINATOR,
                .error_code = ERROR_CODE(0, 0, PROFILE_CONTROLLER_ERROR_THRESHOLD_EXCEEDED, 0)
            };
            event_manager_post_blocking(FURNACE_ERROR_EVENT,
                                        FURNACE_ERROR_EVENT_ID,
                                        &furnace_err, sizeof(furnace_err));

            ctx->heating_task_state.is_completed = true;
            post_coordinator_event(COORDINATOR_EVENT_PROFILE_COMPLETED, NULL, 0);
            ctx->running = false;
            break;
        }

        if (err != PROFILE_CONTROLLER_ERROR_NONE)
        {
            LOGGER_LOG_WARN(TAG, "profile_tick error: %d", err);
            continue;
        }

        if (tick_result.stage_changed) {
            LOGGER_LOG_INFO(TAG, "Stage changed → stage %d, phase %d",
                            tick_result.current_stage_index, (int)tick_result.phase);

            /* On cooling or cooldown entry: heater off, fan on */
            if (tick_result.phase == STAGE_PHASE_COOLING ||
                tick_result.phase == STAGE_PHASE_COOLDOWN) {
                kill_heater();
                ctx->heating_task_state.fan_on = true;
                /* TODO: Send fan-on command via GPIO or command handler when hardware interface is available */
                LOGGER_LOG_INFO(TAG, "%s: heater off, fan on",
                                tick_result.phase == STAGE_PHASE_COOLDOWN ? "Cooldown" : "Cooling stage");
            }
        }

        float power_output = 0.0f;

        /* ── Stall detection during HEATING ─────────────────────────── */
        if (tick_result.phase == STAGE_PHASE_HEATING) {
            if (!stall_tracking || tick_result.stage_changed) {
                /* (Re)start tracking window */
                stall_start_temp     = ctx->current_temperature;
                stall_start_setpoint = tick_result.setpoint;
                stall_elapsed_ms     = 0;
                stall_tracking       = true;
            } else {
                stall_elapsed_ms += last_update_duration;

                if (stall_elapsed_ms >= CONFIG_COORDINATOR_STALL_CHECK_MS) {
                    float expected_rise = tick_result.setpoint - stall_start_setpoint;
                    float actual_rise   = ctx->current_temperature - stall_start_temp;

                    if (expected_rise > 1.0f && actual_rise < expected_rise * 0.5f) {
                        LOGGER_LOG_ERROR(TAG, "STALL: expected +%.1f C in %lu ms, got +%.1f C",
                                         expected_rise,
                                         (unsigned long)stall_elapsed_ms,
                                         actual_rise);

                        /* Pause the program (don't kill — user can inspect & resume) */
                        ctx->paused = true;
                        ctx->heating_task_state.is_paused = true;
                        kill_heater();

                        esp_err_t stall_err = ESP_FAIL;
                        post_coordinator_error_event(
                            COORDINATOR_EVENT_ERROR_OCCURRED,
                            &stall_err,
                            COORDINATOR_ERROR_STALL_DETECTED);

                        stall_tracking = false;
                    } else {
                        /* Window passed — reset for next check */
                        stall_start_temp     = ctx->current_temperature;
                        stall_start_setpoint = tick_result.setpoint;
                        stall_elapsed_ms     = 0;
                    }
                }
            }
        } else {
            stall_tracking = false;
        }

        /* During cooling/cooldown the heater is off — skip PID computation */
        if (tick_result.phase != STAGE_PHASE_COOLDOWN &&
            tick_result.phase != STAGE_PHASE_COOLING) {
            power_output = pid_controller_compute(tick_result.setpoint,
                                                  ctx->current_temperature,
                                                  last_update_duration);

            if (tick_result.stage_changed) {
                send_heater_command(COMMAND_TYPE_HEATER_CLEAR, power_output);
            }
            send_heater_command(COMMAND_TYPE_HEATER_SET_POWER, power_output);
        }

        post_status_update(ctx, tick_result.setpoint, power_output);

        if (tick_result.profile_complete) {
            handle_profile_completion(ctx);
            break;
        }

        event_manager_post_health(HEALTH_MONITOR_EVENT_HEARTBEAT, &coordinator_health_data);
    }

    LOGGER_LOG_INFO(TAG, "Temperature monitor task exiting");
    stop_heating_profile(ctx);
    ctx->task_handle = NULL;
    vTaskDelete(NULL);
}

static uint32_t calculate_program_duration_ms(const program_draft_t *prog,
                                               float current_temperature,
                                               int cooldown_rate_x10,
                                               uint32_t *out_stages_ms)
{
    uint32_t stages_ms = 0;
    float last_stage_temp = current_temperature;
    for (int i = 0; i < PROGRAMS_TOTAL_STAGE_COUNT; ++i) {
        if (prog->stages[i].is_set) {
            stages_ms += (uint32_t)prog->stages[i].t_min * 60U * 1000U;
            last_stage_temp = (float)prog->stages[i].target_t_c;
        }
    }

    uint32_t total_ms = stages_ms;
    if (last_stage_temp > 0.0f && cooldown_rate_x10 > 0) {
        float cooldown_min = (last_stage_temp * 10.0f) / (float)cooldown_rate_x10;
        if (cooldown_min < 1.0f) cooldown_min = 1.0f;
        total_ms += (uint32_t)(cooldown_min * 60.0f * 1000.0f);
    }

    *out_stages_ms = stages_ms;
    return total_ms;
}

static void init_heating_task_state(coordinator_ctx_t *ctx,
                                    uint32_t total_ms,
                                    uint32_t stages_ms)
{
    ctx->heating_task_state.is_active = true;
    ctx->heating_task_state.is_paused = false;
    ctx->heating_task_state.is_completed = false;
    ctx->heating_task_state.current_time_elapsed_ms = 0;
    ctx->heating_task_state.estimated_total_duration_ms = total_ms;
    ctx->heating_task_state.heating_stages_duration_ms = stages_ms;
    ctx->heating_task_state.current_temperature = ctx->current_temperature;
    ctx->heating_task_state.heating_element_on = false;
    ctx->heating_task_state.fan_on = false;
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

    memcpy(&ctx->run_program, program, sizeof(ctx->run_program));
    ctx->has_program = true;
    const program_draft_t *prog = &ctx->run_program;

    uint32_t stages_ms = 0;
    uint32_t total_ms = calculate_program_duration_ms(prog, ctx->current_temperature,
                                                     cooldown_rate_x10, &stages_ms);

    init_heating_task_state(ctx, total_ms, stages_ms);

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
                               heater_controller_task,
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

    event_manager_post_health(HEALTH_MONITOR_EVENT_REGISTER, &coordinator_health_data);

    return ESP_OK;
}

//TODO make sure it has control over heater and fan
//ask svetlio how long can a pause last and what temp difference is acceptable
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
    /* Always clean up timer and profile — the task may have self-exited
     * (profile complete / emergency stop) with ctx->running already false. */
    if (ctx->pid_tick_timer != NULL)
    {
        esp_timer_stop(ctx->pid_tick_timer);
        esp_timer_delete(ctx->pid_tick_timer);
        ctx->pid_tick_timer = NULL;
        LOGGER_LOG_INFO(TAG, "PID tick timer stopped");
    }

    if (ctx->running)
    {
        ctx->running = false;
        if (ctx->task_handle != NULL)
        {
            xTaskNotifyGive(ctx->task_handle);
        }
    }

    ctx->heating_task_state.profile_index = INVALID_PROFILE_INDEX;
    ctx->heating_task_state.is_paused = false;

    kill_heater();
    shutdown_profile_controller();

    LOGGER_LOG_INFO(TAG, "Coordinator task shutdown complete");

    return ESP_OK;
}
