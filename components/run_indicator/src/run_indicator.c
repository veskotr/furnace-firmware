#include "run_indicator.h"

#include "driver/gpio.h"
#include "event_manager.h"
#include "event_registry.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "logger_component.h"

#define RUN_INDICATOR_GPIO GPIO_NUM_14

static const char *TAG = "run_indicator";

typedef enum
{
    RUN_INDICATOR_OFF = 0,
    RUN_INDICATOR_ON,
    RUN_INDICATOR_BLINK
} run_indicator_mode_t;

static run_indicator_mode_t s_mode = RUN_INDICATOR_OFF;
static TaskHandle_t s_task_handle = NULL;

static void run_indicator_task(void *arg)
{
    bool led_state = false;

    while (true) {
        switch (s_mode) {
        case RUN_INDICATOR_ON:
            led_state = true;
            gpio_set_level(RUN_INDICATOR_GPIO, 1);
            vTaskDelay(pdMS_TO_TICKS(200));
            break;
        case RUN_INDICATOR_OFF:
            led_state = false;
            gpio_set_level(RUN_INDICATOR_GPIO, 0);
            vTaskDelay(pdMS_TO_TICKS(200));
            break;
        case RUN_INDICATOR_BLINK:
        default:
            led_state = !led_state;
            gpio_set_level(RUN_INDICATOR_GPIO, led_state ? 1 : 0);
            vTaskDelay(pdMS_TO_TICKS(500));
            break;
        }
    }
}

static void run_indicator_event_handler(void *handler_arg, esp_event_base_t base, int32_t id, void *event_data)
{
    (void)handler_arg;
    (void)base;
    (void)event_data;

    switch (id) {
    case COORDINATOR_EVENT_PROFILE_STARTED:
    case COORDINATOR_EVENT_PROFILE_RESUMED:
        s_mode = RUN_INDICATOR_ON;
        LOGGER_LOG_INFO(TAG, "Run indicator ON (coordinator confirmed)");
        break;
    case COORDINATOR_EVENT_PROFILE_PAUSED:
        s_mode = RUN_INDICATOR_BLINK;
        LOGGER_LOG_INFO(TAG, "Run indicator BLINK (coordinator confirmed)");
        break;
    case COORDINATOR_EVENT_PROFILE_STOPPED:
        s_mode = RUN_INDICATOR_OFF;
        LOGGER_LOG_INFO(TAG, "Run indicator OFF (coordinator confirmed)");
        break;
    default:
        break;
    }
}

void run_indicator_init(void)
{
    gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << RUN_INDICATOR_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&cfg);
    gpio_set_level(RUN_INDICATOR_GPIO, 0);

    if (s_task_handle == NULL) {
        xTaskCreate(run_indicator_task, "run_indicator", 2048, NULL, 5, &s_task_handle);
    }

    event_manager_subscribe(COORDINATOR_EVENT, ESP_EVENT_ANY_ID, &run_indicator_event_handler, NULL);
}
