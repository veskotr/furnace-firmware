#include "commands_dispatcher.h"
#include "utils.h"
#include "temperature_profile_controller.h"
#include "freertos/FreeRTOS.h"
#include "logger_component.h"
#include "coordinator_component_types.h"
#include "coordinator_component_internal.h"
#include "event_manager.h"
#include "event_registry.h"
#include "heater_controller_component.h"
#include "temperature_processor_component.h"

static const char* TAG = "COORDINATOR_EVENTS";

static void temperature_processor_event_handler(void* handler_arg, esp_event_base_t base, int32_t id, void* event_data)
{
    coordinator_ctx_t* ctx = (coordinator_ctx_t*)handler_arg;

    if (id == PROCESS_TEMPERATURE_VALIDITY_EVENT_DATA)
    {
        if (event_data == NULL)
        {
            return;
        }

        const temperature_processor_sample_t* sample = event_data;
        const bool fresh = sample->valid &&
                           (xTaskGetTickCount() - sample->sample_tick) <=
                               pdMS_TO_TICKS(CONFIG_COORDINATOR_SENSOR_AGGREGATE_STALE_TIMEOUT_MS);
        if (!fresh)
        {
            ctx->sensor_recovery_count = 0;
            if (!atomic_load_explicit(&ctx->sensor_data_inhibited, memory_order_acquire))
            {
                atomic_store_explicit(&ctx->sensor_data_inhibited, true, memory_order_release);
                CHECK_ERR_LOG(heater_controller_set_sensor_data_inhibit(true),
                              "Failed to enforce sensor-data heater inhibit");
            }
            if (ctx->running && !ctx->paused)
            {
                CHECK_ERR_LOG(pause_heating_profile(ctx),
                              "Failed to pause profile for invalid temperature aggregate");
            }
            LOGGER_LOG_WARN(TAG, "Temperature aggregate invalid/stale (%u/%u fresh sensors)",
                            sample->valid_sensor_count, sample->total_sensor_count);
            return;
        }

        if (atomic_load_explicit(&ctx->sensor_data_inhibited, memory_order_acquire))
        {
            if (ctx->sensor_recovery_count < CONFIG_COORDINATOR_SENSOR_RECOVERY_VALID_AGGREGATES)
            {
                ++ctx->sensor_recovery_count;
            }
            if (CONFIG_COORDINATOR_SENSOR_AUTO_RECOVERY &&
                ctx->sensor_recovery_count >= CONFIG_COORDINATOR_SENSOR_RECOVERY_VALID_AGGREGATES)
            {
                const esp_err_t release_err = heater_controller_set_sensor_data_inhibit(false);
                if (release_err == ESP_OK)
                {
                    atomic_store_explicit(&ctx->sensor_data_inhibited, false, memory_order_release);
                    ctx->sensor_recovery_count = 0;
                    if (ctx->running && ctx->paused && !heater_controller_output_is_inhibited())
                    {
                        CHECK_ERR_LOG(resume_heating_profile(ctx),
                                      "Failed to resume profile after sensor recovery");
                    }
                    else if (ctx->running && ctx->paused)
                    {
                        LOGGER_LOG_WARN(TAG, "Sensor data recovered but another heater inhibit remains active");
                    }
                }
                else
                {
                    LOGGER_LOG_ERROR(TAG, "Sensor recovery could not release heater inhibit: %s",
                                     esp_err_to_name(release_err));
                }
            }
        }
        return;
    }

    if (id != PROCESS_TEMPERATURE_EVENT_DATA)
    {
        LOGGER_LOG_WARN(TAG, "Unknown Temperature Processor Event ID: %d", id);
        return;
    }
    else
    {
        if (event_data == NULL)
        {
            LOGGER_LOG_WARN(TAG, "Temperature Processor Event Data is NULL");
            return;
        }

        const float temperature = *((float*)event_data);
        if (temperature == -0.0f)  // Check for invalid temperature reading (e.g., sensor error)
        {
            LOGGER_LOG_WARN(TAG, "Temperature processor data marked invalid");
            return;
        }
        if (ctx->temperature_mutex != NULL &&
            xSemaphoreTake(ctx->temperature_mutex, portMAX_DELAY) == pdTRUE)
        {
            ctx->current_temperature = temperature;
            ctx->heating_task_state.current_temperature = temperature;
            xSemaphoreGive(ctx->temperature_mutex);
        }
        atomic_store_explicit(&ctx->has_valid_temperature, true, memory_order_release);
        LOGGER_LOG_DEBUG(TAG, "Updated current temperature to %.2f C",
                         coordinator_get_current_temperature(ctx));
    }
}

static esp_err_t coordinator_command_handler(void* handler_arg, const void* command_data)
{
    coordinator_ctx_t* ctx = (coordinator_ctx_t*)handler_arg;
    if (command_data == NULL)
    {
        LOGGER_LOG_ERROR(TAG, "Invalid coordinator command data");
        return ESP_ERR_INVALID_ARG;
    }
    const coordinator_command_data_t* data = (const coordinator_command_data_t*)command_data;
    switch (data->type)
    {
    case COMMAND_TYPE_COORDINATOR_START_PROFILE:
        {
            LOGGER_LOG_INFO(TAG, "Coordinator Event: Start Profile '%s'", data->program.name);
            const esp_err_t err = start_heating_profile(ctx, &data->program, data->cooldown_rate_x10);
            if (err != ESP_OK)
            {
                CHECK_ERR_LOG(
                    post_coordinator_error_event(COORDINATOR_EVENT_ERROR_OCCURRED, &err, COORDINATOR_ERROR_NOT_STARTED),
                    "Failed to send coordinator error event for start profile failure");
                LOGGER_LOG_ERROR(TAG, "Failed to start heating profile '%s': %s",
                                 data->program.name,
                                 esp_err_to_name(err));
                return err;
            }
            post_coordinator_event(COORDINATOR_EVENT_PROFILE_STARTED, NULL, 0);
            break;
        }
    case COMMAND_TYPE_COORDINATOR_PAUSE_PROFILE:
        {
            LOGGER_LOG_INFO(TAG, "Coordinator Event: Pause Profile");
            const esp_err_t err = pause_heating_profile(ctx);
            if (err != ESP_OK)
            {
                CHECK_ERR_LOG(
                    post_coordinator_error_event(COORDINATOR_EVENT_ERROR_OCCURRED, &err,
                        COORDINATOR_ERROR_PROFILE_NOT_PAUSED),
                    "Failed to send coordinator error event for pause profile failure");
                LOGGER_LOG_ERROR(TAG, "Failed to pause heating profile: %s",
                                 esp_err_to_name(err));
                return err;
            }
            post_coordinator_event(COORDINATOR_EVENT_PROFILE_PAUSED, NULL, 0);
            break;
        }
    case COMMAND_TYPE_COORDINATOR_STOP_PROFILE:
        {
            LOGGER_LOG_INFO(TAG, "Coordinator Event: Stop Profile");
            const esp_err_t err = stop_heating_profile(ctx);
            if (err != ESP_OK)
            {
                CHECK_ERR_LOG(
                    post_coordinator_error_event(COORDINATOR_EVENT_ERROR_OCCURRED, &err,
                        COORDINATOR_ERROR_PROFILE_NOT_STOPPED),
                    "Failed to send coordinator error event for stop profile failure");
                LOGGER_LOG_ERROR(TAG, "Failed to stop heating profile: %s",
                                 esp_err_to_name(err));
                return err;
            }
            post_coordinator_event(COORDINATOR_EVENT_PROFILE_STOPPED, NULL, 0);
            break;
        }
        case COMMAND_TYPE_UPDATE_MANUAL_TARGET:
        {
            LOGGER_LOG_INFO(TAG, "Coordinator Event: Update Manual Target t=%d delta_x10=%d",
                            data->target_t_c, data->delta_t_per_min_x10);

            /* Write the complete mailbox under one lock so the profile task
             * cannot observe a target from one update with the rate from
             * another. */
            if (ctx->target_update_mutex == NULL ||
                xSemaphoreTake(ctx->target_update_mutex, portMAX_DELAY) != pdTRUE)
            {
                LOGGER_LOG_ERROR(TAG, "Failed to lock manual-target mailbox");
                return ESP_ERR_INVALID_STATE;
            }
            ctx->target_update.target_t_c          = data->target_t_c;
            ctx->target_update.delta_t_per_min_x10 = data->delta_t_per_min_x10;
            ctx->target_update.pending = true;
            xSemaphoreGive(ctx->target_update_mutex);
            break;
        }
    case COMMAND_TYPE_COORDINATOR_RESUME_PROFILE:
        {
            LOGGER_LOG_INFO(TAG, "Coordinator Event: Resume Profile");
            const esp_err_t err = resume_heating_profile(ctx);
            if (err != ESP_OK)
            {
                CHECK_ERR_LOG(
                    post_coordinator_error_event(COORDINATOR_EVENT_ERROR_OCCURRED, &err,
                        COORDINATOR_ERROR_PROFILE_NOT_RESUMED),
                    "Failed to send coordinator error event for resume profile failure");
                LOGGER_LOG_ERROR(TAG, "Failed to resume heating profile: %s",
                                 esp_err_to_name(err));
                return err;
            }
            post_coordinator_event(COORDINATOR_EVENT_PROFILE_RESUMED, NULL, 0);
            break;
        }
    case COMMAND_TYPE_COORDINATOR_GET_STATUS_REPORT:
        {
            LOGGER_LOG_INFO(TAG, "Coordinator Event: Get Status Report");
            heating_task_state_t state;
            get_heating_task_state(ctx);
            CHECK_ERR_LOG(
                post_coordinator_event(COORDINATOR_EVENT_STATUS_UPDATE, &state, sizeof(heating_task_state_t)),
                "Failed to send coordinator status report event");
            break;
        }
    case COMMAND_TYPE_COORDINATOR_GET_CURRENT_PROFILE:
        {
            LOGGER_LOG_INFO(TAG, "Coordinator Event: Get Current Profile");
            size_t profile_index;
            get_current_heating_profile(ctx);
            CHECK_ERR_LOG(post_coordinator_event(COORDINATOR_EVENT_CURRENT_PROFILE, &profile_index, sizeof(size_t)),
                          "Failed to send coordinator current profile event");
            break;
        }
    default:
        LOGGER_LOG_ERROR(TAG, "Unknown coordinator command type: %d", data->type);
        return ESP_ERR_INVALID_ARG;
    }

    return ESP_OK;
}

esp_err_t post_heater_controller_command(command_t *command)
{
    CHECK_ERR_LOG_RET(commands_dispatcher_dispatch_command(command),
                          "Failed to post heater controller command");
    return ESP_OK;
}

esp_err_t post_coordinator_error_event(const coordinator_event_id_t event_type, const esp_err_t* esp_error,
                                       const coordinator_error_code_t coordinator_error_code)
{
    coordinator_error_data_t error = {
        .esp_error_code = *esp_error,
        .error_code = coordinator_error_code
    };

    return post_coordinator_event(event_type, &error, sizeof(error));
}

esp_err_t post_coordinator_event(const coordinator_event_id_t event_type, void* event_data,
                                 const size_t event_data_size)
{
    CHECK_ERR_LOG_RET_FMT(event_manager_post_blocking(
                              COORDINATOR_EVENT,
                              event_type,
                              event_data,
                              event_data_size),
                          "Failed to post coordinator event type %d",
                          event_type);
    return ESP_OK;
}

esp_err_t init_coordinator_events(coordinator_ctx_t* ctx)
{
    CHECK_ERR_LOG_RET(register_command_handler(
                          COMMAND_TARGET_COORDINATOR,
                          &coordinator_command_handler,
                          ctx),
                      "Failed to register coordinator command handler");

    CHECK_ERR_LOG_RET(event_manager_subscribe(
                          TEMP_PROCESSOR_EVENT,
                          ESP_EVENT_ANY_ID,
                          &temperature_processor_event_handler,
                          ctx),
                      "Failed to subscribe to temperature processor events");

    ctx->events_initialized = true;

    return ESP_OK;
}

esp_err_t shutdown_coordinator_events(coordinator_ctx_t* ctx)
{
    if (!ctx->events_initialized)
    {
        return ESP_OK;
    }
    CHECK_ERR_LOG_RET(unregister_command_handler(COMMAND_TARGET_COORDINATOR),
                      "Failed to unsubscribe from coordinator events");


    ctx->events_initialized = false;
    return ESP_OK;
}
