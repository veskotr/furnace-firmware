#include "fan_controller.h"

#include "sdkconfig.h"
#include "driver/gpio.h"
#include "event_manager.h"
#include "event_registry.h"
#include "logger_component.h"

#define FAN_GPIO     CONFIG_FAN_CONTROLLER_GPIO_PIN
#define FAN_ON_C     CONFIG_FAN_CONTROLLER_AUTO_ON_C
#define FAN_OFF_C    CONFIG_FAN_CONTROLLER_AUTO_OFF_C

static const char *TAG = "fan_controller";

typedef enum {
    FAN_MODE_AUTO = 0,   /* No program running — follow temp thresholds */
    FAN_MODE_PROGRAM,    /* Program running — fan held on for the duration */
} fan_mode_t;

static fan_mode_t s_mode = FAN_MODE_AUTO;
static bool       s_fan_on = false;
static float      s_last_temp_c = 0.0f;
static bool       s_have_temp = false;

static void fan_set(bool on)
{
    if (on == s_fan_on) {
        return;
    }
    gpio_set_level(FAN_GPIO, on ? 1 : 0);
    s_fan_on = on;
    LOGGER_LOG_INFO(TAG, "Fan %s", on ? "ON" : "OFF");
}

static void apply_auto_threshold(void)
{
    if (s_mode != FAN_MODE_AUTO || !s_have_temp) {
        return;
    }
    if (s_last_temp_c >= (float)FAN_ON_C) {
        fan_set(true);
    } else if (s_last_temp_c < (float)FAN_OFF_C) {
        fan_set(false);
    }
    /* Within the hysteresis band — preserve current state. */
}

static void temp_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg; (void)base;
    if (id != PROCESS_TEMPERATURE_EVENT_DATA || data == NULL) {
        return;
    }
    s_last_temp_c = *(const float*)data;
    s_have_temp = true;
    apply_auto_threshold();
}

static void coordinator_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg; (void)base; (void)data;
    switch (id) {
    case COORDINATOR_EVENT_PROFILE_STARTED:
    case COORDINATOR_EVENT_PROFILE_RESUMED:
        s_mode = FAN_MODE_PROGRAM;
        fan_set(true);
        LOGGER_LOG_INFO(TAG, "Program took control of fan");
        break;
    case COORDINATOR_EVENT_PROFILE_STOPPED:
    case COORDINATOR_EVENT_PROFILE_COMPLETED:
        s_mode = FAN_MODE_AUTO;
        LOGGER_LOG_INFO(TAG, "Fan released to auto mode");
        /* Re-evaluate from the most recent temperature reading. If the
         * unit is still hot, the fan keeps running until it cools below
         * the off-threshold. */
        apply_auto_threshold();
        break;
    default:
        break;
    }
}

void fan_controller_init(void)
{
    gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << FAN_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&cfg);
    gpio_set_level(FAN_GPIO, 0);
    s_fan_on = false;
    s_mode = FAN_MODE_AUTO;
    s_have_temp = false;

    event_manager_subscribe(TEMP_PROCESSOR_EVENT, ESP_EVENT_ANY_ID, &temp_event_handler, NULL);
    event_manager_subscribe(COORDINATOR_EVENT, ESP_EVENT_ANY_ID, &coordinator_event_handler, NULL);

    LOGGER_LOG_INFO(TAG, "Fan controller initialized (auto on >=%d C / off <%d C, GPIO %d)",
                    FAN_ON_C, FAN_OFF_C, FAN_GPIO);
}
