#include "hmi_coordinator_internal.h"

#include "sdkconfig.h"
#include "nextion_events_internal.h"
#include "logger_component.h"
#include "event_manager.h"
#include "event_registry.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include <string.h>

static const char *TAG = "hmi_coord";
static QueueHandle_t s_cmd_queue = NULL;

/* ── ESP event → HMI queue bridge handlers ─────────────────────── */
// These run on the event_manager's event-loop task and simply serialize
// incoming system events into the HMI command queue. The actual UI work
// happens on the coordinator task.

static void temp_processor_event_bridge(void *handler_arg, esp_event_base_t base,
                                        int32_t id, void *event_data)
{
    if (!s_cmd_queue || id != PROCESS_TEMPERATURE_EVENT_DATA || !event_data) {
        return;
    }

    const temp_processor_data_t *data = (const temp_processor_data_t *)event_data;
    hmi_cmd_t cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.type = HMI_CMD_TEMP_UPDATE;
    cmd.temp.average_temperature = data->average_temperature;
    cmd.temp.valid = data->valid;

    // Non-blocking: drop event if queue is full (UI updates are best-effort)
    xQueueSend(s_cmd_queue, &cmd, 0);
}

static void coordinator_event_bridge(void *handler_arg, esp_event_base_t base,
                                     int32_t id, void *event_data)
{
    if (!s_cmd_queue) {
        return;
    }

    hmi_cmd_t cmd;
    memset(&cmd, 0, sizeof(cmd));

    switch (id) {
        case COORDINATOR_EVENT_PROFILE_STARTED:
            cmd.type = HMI_CMD_PROFILE_STARTED;
            if (event_data) {
                const coordinator_start_profile_data_t *d = event_data;
                (void)d;  // profile_index available if needed
            }
            break;

        case COORDINATOR_EVENT_PROFILE_PAUSED:
            cmd.type = HMI_CMD_PROFILE_PAUSED;
            break;

        case COORDINATOR_EVENT_PROFILE_RESUMED:
            cmd.type = HMI_CMD_PROFILE_RESUMED;
            break;

        case COORDINATOR_EVENT_PROFILE_STOPPED:
            cmd.type = HMI_CMD_PROFILE_STOPPED;
            break;

        case COORDINATOR_EVENT_ERROR_OCCURRED:
            cmd.type = HMI_CMD_PROFILE_ERROR;
            if (event_data) {
                const coordinator_error_data_t *err = event_data;
                cmd.error.error_code = err->error_code;
                cmd.error.esp_error = err->esp_error_code;
            }
            break;

        default:
            // Ignore RX events (START/PAUSE/STOP/RESUME — we sent those)
            // and status report responses (we didn't request them)
            return;
    }

    xQueueSend(s_cmd_queue, &cmd, 0);
}

/* ── Coordinator task ──────────────────────────────────────────────── */

static void hmi_coordinator_task(void *arg)
{
    (void)arg;
    hmi_cmd_t cmd;

    while (true) {
        if (xQueueReceive(s_cmd_queue, &cmd, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        switch (cmd.type) {
            case HMI_CMD_INIT_DISPLAY:
                nextion_event_handle_init();
                break;

            case HMI_CMD_HANDLE_LINE:
                nextion_event_handle_line(cmd.line);
                break;

            case HMI_CMD_TEMP_UPDATE:
                nextion_event_handle_temp_update(
                    cmd.temp.average_temperature, cmd.temp.valid);
                break;

            case HMI_CMD_PROFILE_STARTED:
                nextion_event_handle_profile_started();
                break;

            case HMI_CMD_PROFILE_PAUSED:
                nextion_event_handle_profile_paused();
                break;

            case HMI_CMD_PROFILE_RESUMED:
                nextion_event_handle_profile_resumed();
                break;

            case HMI_CMD_PROFILE_STOPPED:
                nextion_event_handle_profile_stopped();
                break;

            case HMI_CMD_PROFILE_ERROR:
                nextion_event_handle_profile_error(
                    cmd.error.error_code, cmd.error.esp_error);
                break;

            default:
                LOGGER_LOG_WARN(TAG, "Unknown command type: %d", (int)cmd.type);
                break;
        }
    }
}

/* ── Public API ────────────────────────────────────────────────────── */

void hmi_coordinator_init(void)
{
    s_cmd_queue = xQueueCreate(
        CONFIG_NEXTION_COORDINATOR_QUEUE_DEPTH,
        sizeof(hmi_cmd_t)
    );

    if (!s_cmd_queue) {
        LOGGER_LOG_ERROR(TAG, "Failed to create command queue");
        return;
    }

    // Subscribe to temperature processor events (live temp readings)
    esp_err_t err = event_manager_subscribe(
        TEMP_PROCESSOR_EVENT,
        PROCESS_TEMPERATURE_EVENT_DATA,
        temp_processor_event_bridge,
        NULL);
    if (err != ESP_OK) {
        LOGGER_LOG_ERROR(TAG, "Failed to subscribe to temp events: %s",
                         esp_err_to_name(err));
    }

    // Subscribe to coordinator TX events (profile status feedback)
    err = event_manager_subscribe(
        COORDINATOR_EVENT,
        ESP_EVENT_ANY_ID,
        coordinator_event_bridge,
        NULL);
    if (err != ESP_OK) {
        LOGGER_LOG_ERROR(TAG, "Failed to subscribe to coordinator events: %s",
                         esp_err_to_name(err));
    }

    BaseType_t ok = xTaskCreate(
        hmi_coordinator_task,
        "hmi_coord",
        CONFIG_NEXTION_COORDINATOR_TASK_STACK_SIZE,
        NULL,
        CONFIG_NEXTION_COORDINATOR_TASK_PRIORITY,
        NULL
    );

    if (ok != pdPASS) {
        LOGGER_LOG_ERROR(TAG, "Failed to create coordinator task");
    }
}

bool hmi_coordinator_post_line(const char *line)
{
    if (!s_cmd_queue || !line) {
        return false;
    }

    hmi_cmd_t cmd;
    cmd.type = HMI_CMD_HANDLE_LINE;
    strncpy(cmd.line, line, sizeof(cmd.line) - 1);
    cmd.line[sizeof(cmd.line) - 1] = '\0';

    if (xQueueSend(s_cmd_queue, &cmd, pdMS_TO_TICKS(100)) != pdTRUE) {
        LOGGER_LOG_WARN(TAG, "Command queue full, dropping line");
        return false;
    }

    return true;
}

bool hmi_coordinator_post_cmd(hmi_cmd_type_t type)
{
    if (!s_cmd_queue) {
        return false;
    }

    hmi_cmd_t cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.type = type;

    if (xQueueSend(s_cmd_queue, &cmd, pdMS_TO_TICKS(100)) != pdTRUE) {
        LOGGER_LOG_WARN(TAG, "Command queue full, dropping cmd %d", (int)type);
        return false;
    }

    return true;
}
