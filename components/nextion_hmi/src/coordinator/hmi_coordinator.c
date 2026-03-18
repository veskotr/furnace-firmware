#include "hmi_coordinator_internal.h"

#include "sdkconfig.h"
#include "nextion_events_internal.h"
#include "nextion_run_handlers.h"
#include "nextion_file_reader_internal.h"
#include "nextion_storage_internal.h"
#include "heating_program_models_internal.h"
#include "logger_component.h"
#include "event_manager.h"
#include "event_registry.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include <string.h>

static const char* TAG = "hmi_coord";
static QueueHandle_t s_cmd_queue = NULL;

/* ── Deferred-send state ───────────────────────────────────────────
 *
 * While a file transfer (storage / file-reader) owns the UART we keep
 * draining the command queue so it never fills up, but we skip all
 * nextion_send_cmd() calls.  Instead we capture the latest snapshot
 * of volatile data (temperature) and buffer the small number of
 * critical events (profile state, errors) for replay once the UART
 * is free again.
 * ----------------------------------------------------------------- */

/** Maximum critical events we can buffer during one file transfer. */
#define DEFERRED_CRITICAL_MAX  8

/** True while a file transfer is in progress and we are deferring. */
static bool s_deferring = false;

/** Latest temperature cached while deferring. */
static struct
{
    float temperature;
    bool valid;
    bool pending; /**< true if at least one update was deferred */
} s_deferred_temp;

/** Small ring of critical commands deferred during a transfer. */
static hmi_cmd_t s_deferred_critical[DEFERRED_CRITICAL_MAX];
static int s_deferred_critical_count = 0;

/** Check whether storage or file-reader currently owns the UART. */
static bool uart_busy(void)
{
    return nextion_storage_active() || nextion_file_reader_active();
}

/** Replay all deferred work after a file transfer completes. */
static void flush_deferred(void)
{
    /* 1. Replay critical events in order */
    for (int i = 0; i < s_deferred_critical_count; i++)
    {
        const hmi_cmd_t* c = &s_deferred_critical[i];
        switch (c->type)
        {
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
        case HMI_CMD_PROFILE_COMPLETED:
            nextion_event_handle_profile_completed();
            break;
        case HMI_CMD_PROFILE_ERROR:
            nextion_event_handle_profile_error(
                c->error.error_code, c->error.esp_error);
            break;
        default:
            break;
        }
    }
    s_deferred_critical_count = 0;

    /* 2. Push the latest temperature reading (one send, not many) */
    if (s_deferred_temp.pending)
    {
        nextion_event_handle_temp_update(
            s_deferred_temp.temperature, s_deferred_temp.valid);
        s_deferred_temp.pending = false;
    }

    LOGGER_LOG_INFO(TAG, "Deferred commands flushed");
}

/* ── ESP event → HMI queue bridge handlers ─────────────────────── */
// These run on the event_manager's event-loop task and simply serialize
// incoming system events into the HMI command queue. The actual UI work
// happens on the coordinator task.

static void temp_processor_event_bridge(void* handler_arg, esp_event_base_t base,
                                        int32_t id, void* event_data)
{
    if (!s_cmd_queue || id != PROCESS_TEMPERATURE_EVENT_DATA || !event_data)
    {
        return;
    }

    const float temperature = *((const float*)event_data);
    hmi_cmd_t cmd = {0};
    cmd.type = HMI_CMD_TEMP_UPDATE;
    cmd.temp.average_temperature = temperature;
    cmd.temp.valid = true;

    // Non-blocking: drop event if queue is full (UI updates are best-effort)
    xQueueSend(s_cmd_queue, &cmd, 0);
}

static void coordinator_event_bridge(void* handler_arg, esp_event_base_t base,
                                     int32_t id, void* event_data)
{
    if (!s_cmd_queue)
    {
        return;
    }

    hmi_cmd_t cmd = {0};

    switch (id)
    {
    case COORDINATOR_EVENT_PROFILE_STARTED:
        cmd.type = HMI_CMD_PROFILE_STARTED;
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

    case COORDINATOR_EVENT_PROFILE_COMPLETED:
        cmd.type = HMI_CMD_PROFILE_COMPLETED;
        break;

    case COORDINATOR_EVENT_STATUS_UPDATE:
        cmd.type = HMI_CMD_STATUS_UPDATE;
        if (event_data)
        {
            const coordinator_status_data_t* s = event_data;
            cmd.status.current_temperature = s->current_temperature;
            cmd.status.target_temperature = s->target_temperature;
            cmd.status.power_output = s->power_output;
            cmd.status.elapsed_ms = s->elapsed_ms;
            cmd.status.total_ms = s->total_ms;
        }
        break;

    case COORDINATOR_EVENT_ERROR_OCCURRED:
        cmd.type = HMI_CMD_PROFILE_ERROR;
        if (event_data)
        {
            const coordinator_error_data_t* err = event_data;
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

static void hmi_coordinator_task(void* arg)
{
    (void)arg;
    hmi_cmd_t cmd;

    while (true)
    {
        /*
         * Use a short timeout instead of portMAX_DELAY so we can detect
         * the transition from "UART busy" → "UART free" and flush
         * any deferred work promptly.
         */
        BaseType_t got = xQueueReceive(s_cmd_queue, &cmd,
                                       pdMS_TO_TICKS(50));

        /* ── Check for deferral state transitions ─────────────────── */
        bool busy = uart_busy();

        if (busy && !s_deferring)
        {
            /* Just entered a file transfer */
            s_deferring = true;
            s_deferred_temp.pending = false;
            s_deferred_critical_count = 0;
            LOGGER_LOG_INFO(TAG, "UART busy — deferring HMI commands");
        }
        else if (!busy && s_deferring)
        {
            /* File transfer just finished — replay deferred work */
            s_deferring = false;
            flush_deferred();
        }

        if (got != pdTRUE)
        {
            nextion_run_tick(); /* pause-time display updates */
            continue;
        }

        /* ── If deferring, buffer instead of sending ──────────────── */
        if (s_deferring)
        {
            switch (cmd.type)
            {
            case HMI_CMD_TEMP_UPDATE:
                /* Only keep the latest reading (RAM model still updated) */
                if (cmd.temp.valid)
                {
                    program_set_current_temp_f(cmd.temp.average_temperature);
                }
                s_deferred_temp.temperature = cmd.temp.average_temperature;
                s_deferred_temp.valid = cmd.temp.valid;
                s_deferred_temp.pending = true;
                break;

            case HMI_CMD_STATUS_UPDATE:
                /* Best-effort display data — drop while UART is busy */
                break;

            case HMI_CMD_PROFILE_STARTED:
            case HMI_CMD_PROFILE_PAUSED:
            case HMI_CMD_PROFILE_RESUMED:
            case HMI_CMD_PROFILE_STOPPED:
            case HMI_CMD_PROFILE_COMPLETED:
            case HMI_CMD_PROFILE_ERROR:
                /* Buffer critical events for replay */
                if (s_deferred_critical_count < DEFERRED_CRITICAL_MAX)
                {
                    s_deferred_critical[s_deferred_critical_count++] = cmd;
                }
                else
                {
                    LOGGER_LOG_WARN(TAG, "Deferred critical buffer full, dropping cmd %d",
                                    (int)cmd.type);
                }
                break;

            case HMI_CMD_HANDLE_LINE:
                /* RX task is parked during transfers, but just in case */
                LOGGER_LOG_WARN(TAG, "Line received during transfer (dropped): %s",
                                cmd.line);
                break;

            default:
                break;
            }
            continue;
        }

        /* ── Normal dispatch (UART is free) ───────────────────────── */
        switch (cmd.type)
        {
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

        case HMI_CMD_STATUS_UPDATE:
            nextion_event_handle_status_update(
                cmd.status.elapsed_ms,
                cmd.status.total_ms,
                cmd.status.current_temperature,
                cmd.status.target_temperature,
                cmd.status.power_output);
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

        case HMI_CMD_PROFILE_COMPLETED:
            nextion_event_handle_profile_completed();
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

    if (!s_cmd_queue)
    {
        LOGGER_LOG_ERROR(TAG, "Failed to create command queue");
        return;
    }

    // Subscribe to temperature processor events (live temp readings)
    esp_err_t err = event_manager_subscribe(
        TEMP_PROCESSOR_EVENT,
        PROCESS_TEMPERATURE_EVENT_DATA,
        temp_processor_event_bridge,
        NULL);
    if (err != ESP_OK)
    {
        LOGGER_LOG_ERROR(TAG, "Failed to subscribe to temp events: %s",
                         esp_err_to_name(err));
    }

    // Subscribe to coordinator TX events (profile status feedback)
    err = event_manager_subscribe(
        COORDINATOR_EVENT,
        ESP_EVENT_ANY_ID,
        coordinator_event_bridge,
        NULL);
    if (err != ESP_OK)
    {
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

    if (ok != pdPASS)
    {
        LOGGER_LOG_ERROR(TAG, "Failed to create coordinator task");
    }
}

bool hmi_coordinator_post_line(const char* line)
{
    if (!s_cmd_queue || !line)
    {
        return false;
    }

    hmi_cmd_t cmd;
    cmd.type = HMI_CMD_HANDLE_LINE;
    strncpy(cmd.line, line, sizeof(cmd.line) - 1);
    cmd.line[sizeof(cmd.line) - 1] = '\0';

    if (xQueueSend(s_cmd_queue, &cmd, pdMS_TO_TICKS(100)) != pdTRUE)
    {
        LOGGER_LOG_WARN(TAG, "Command queue full, dropping line");
        return false;
    }

    return true;
}

bool hmi_coordinator_post_cmd(hmi_cmd_type_t type)
{
    if (!s_cmd_queue)
    {
        return false;
    }

    hmi_cmd_t cmd = {0};
    cmd.type = type;

    if (xQueueSend(s_cmd_queue, &cmd, pdMS_TO_TICKS(100)) != pdTRUE)
    {
        LOGGER_LOG_WARN(TAG, "Command queue full, dropping cmd %d", (int)type);
        return false;
    }

    return true;
}
