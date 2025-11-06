#include "logger-component.h"

#include "freertos/task.h"
#include "esp_log.h"

#define LOGGER_QUEUE_LEN 16
static QueueHandle_t logger_queue;
static const char *LOG_TAG = "LOGGER";

static const char *LOGGER_TASK_NAME = "LOGGER_TASK";
static const uint32_t LOGGER_STACK_SIZE = 4096;
static const UBaseType_t LOGGER_TASK_PRIORITY = 4;
static const char *QUEUE_ERROR_MSG = "Failed to create queue";

static void logger_task(void *args)
{
    log_message_t msg;
    while (1)
    {
        if (xQueueReceive(logger_queue, &msg, portMAX_DELAY))
        {
            switch (msg.level)
            {
            case LOG_LEVEL_WARN:
                ESP_LOGW(msg.tag, "%s", msg.message);
                break;
            case LOG_LEVEL_ERROR:
                ESP_LOGE(msg.tag, "%s", msg.message);
                break;
            case LOG_LEVEL_DEBUG:
                ESP_LOGD(msg.tag, "%s", msg.message);
                break;
            case LOG_LEVEL_VERBOSE:
                ESP_LOGV(msg.tag, "%s", msg.message);
                break;
            default:
                ESP_LOGI(msg.tag, "%s", msg.message);
                break;
            }
        }
    }
}

void logger_init(void)
{
    logger_queue = xQueueCreate(LOGGER_QUEUE_LEN, sizeof(log_message_t));
    if (logger_queue == NULL)
    {
        ESP_LOGE(LOG_TAG, "%s", QUEUE_ERROR_MSG);
        return;
    }

    xTaskCreate(logger_task, LOGGER_TASK_NAME, LOGGER_STACK_SIZE, NULL, LOGGER_TASK_PRIORITY, NULL);
}

void logger_send(log_level_t log_level, const char *tag, const char *message)
{
    log_message_t msg = {.tag = tag, .message = message, .level = log_level};
    xQueueSend(logger_queue, &msg, 0); // Non-blocking
}

void logget_send_info(const char *tag, const char *message)
{
    logger_send(LOG_LEVEL_INFO, tag, message);
}
void logger_send_warn(const char *tag, const char *message)
{
    logger_send(LOG_LEVEL_WARN, tag, message);
}
void logger_send_error(const char *tag, const char *message)
{
    logger_send(LOG_LEVEL_ERROR, tag, message);
}
void logger_send_debug(const char *tag, const char *message)
{
    logger_send(LOG_LEVEL_DEBUG, tag, message);
}
void logger_send_verbose(const char *tag, const char *message)
{
    logger_send(LOG_LEVEL_VERBOSE, tag, message);
}
