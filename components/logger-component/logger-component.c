#include "logger-component.h"

#include "freertos/task.h"
#include "esp_log.h"

#define LOGGER_QUEUE_LEN 16
static QueueHandle_t logger_queue;
static const char *LOG_TAG = "LOGGER";

static void logger_task(void *pvParameter)
{
    log_message_t msg;
    while (1) {
        if (xQueueReceive(logger_queue, &msg, portMAX_DELAY)) {
            ESP_LOGI(msg.tag, "%s", msg.message);
        }
    }
}

void logger_init(void)
{
    logger_queue = xQueueCreate(LOGGER_QUEUE_LEN, sizeof(log_message_t));
    if (logger_queue == NULL) {
        ESP_LOGE(LOG_TAG, "Failed to create queue");
        return;
    }

    xTaskCreate(logger_task, "logger_task", 4096, NULL, 4, NULL);
}

void logger_send(const char *tag, const char *message)
{
    log_message_t msg = {.tag = tag, .message = message};
    xQueueSend(logger_queue, &msg, 0); // Non-blocking
}

