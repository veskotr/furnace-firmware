#include "logger-component.h"
#include "esp_log.h"

#include "freertos/task.h"

// Component tag
static const char *TAG = "LOGGER";

// Logger queue handle
static QueueHandle_t logger_queue;

// Component initialized flag
static bool logger_initialized = false;

// ----------------------------
// Configuration
// ----------------------------
typedef struct
{
    const char *task_name;
    uint32_t stack_size;
    UBaseType_t task_priority;
} LoggerConfig_t;

static const LoggerConfig_t logger_config = {
    .task_name = "LOGGER_TASK",
    .stack_size = 4096,
    .task_priority = 4};

// ----------------------------
// Logger Task
// ----------------------------
static void logger_task(void *args)
{
    log_message_t msg;

    while (1)
    {
        if (xQueueReceive(logger_queue, &msg, portMAX_DELAY))
        {
            switch (msg.level)
            {
            case LOG_LEVEL_INFO:
                ESP_LOGI(msg.tag, "%s", msg.message);
                break;
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

// ----------------------------
// Public API
// ----------------------------
void logger_init(void)
{
    if (logger_initialized)
    {
        return;
    }
    logger_queue = xQueueCreate(CONFIG_LOG_QUEUE_SIZE, sizeof(log_message_t));
    if (logger_queue == NULL)
    {
        ESP_LOGE(TAG, "%s", "Failed to create logger queue");
        return;
    }

    xTaskCreate(logger_task, logger_config.task_name, logger_config.stack_size, NULL, logger_config.task_priority, NULL);
    logger_initialized = true;
}

void logger_send(log_level_t log_level, const char *tag, const char *fmt, ...)
{
     if (logger_queue == NULL) {
        // Queue not initialized
        ESP_LOGW("LOGGER", "Logger queue not initialized");
        return;
    }

    log_message_t msg;
    msg.level = log_level;
    msg.tag = tag;

    // Format the message safely
    va_list args;
    va_start(args, fmt);
    vsnprintf(msg.message, sizeof(msg.message), fmt, args);
    va_end(args);

    // Send to queue - wait up to a tick if full
    if (xQueueSend(logger_queue, &msg, pdMS_TO_TICKS(10)) != pdTRUE) {
        ESP_LOGW("LOGGER", "Logger queue full, message dropped: %s", msg.message);
    }
}
