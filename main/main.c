#include "logger_component.h"
#include "temperature_monitor_component.h"
#include "temperature_processor_component.h"
#include "coordinator_component.h"
#include "event_manager.h"
#include "event_registry.h"
#include "nextion_hmi.h"
#include "run_indicator.h"
#include "utils.h"
#include "sdkconfig.h"
#include "health_monitor.h"

static const char* TAG = "main";

void app_main(void)
{
    logger_init();
    CHECK_ERR_LOG_CALL(event_manager_init(),
                       return,
                       "Failed to initialize event manager");
    CHECK_ERR_LOG_CALL(event_registry_init(),
                       return,
                       "Failed to initialize event registry");

    temp_monitor_config_t temp_monitor_config = {
        .number_of_attached_sensors = 5
    };
    esp_err_t temp_err = init_temp_monitor(&temp_monitor_config);
    if (temp_err != ESP_OK) {
        LOGGER_LOG_ERROR(TAG, "Failed to initialize temperature monitor: %s (continuing without sensors)",
                         esp_err_to_name(temp_err));
    }

    if (temp_err == ESP_OK) {
        CHECK_ERR_LOG_CALL(init_temp_processor(),
                           return,
                           "Failed to initialize temperature processor");
    } else {
        LOGGER_LOG_WARN(TAG, "Skipping temperature processor init (no sensors)");
    }

    size_t program_count = 0;
    const ProgramDraft *programs = hmi_get_run_program(&program_count);
    const coordinator_config_t coordinator_config = {
        .programs = programs,
        .num_programs = program_count
    };
    CHECK_ERR_LOG_CALL(init_coordinator(&coordinator_config),
                       return,
                       "Failed to initialize coordinator");

    if (temp_err == ESP_OK) {
        CHECK_ERR_LOG_CALL(init_health_monitor(),
                           return,
                           "Failed to initialize health monitor");
    } else {
        LOGGER_LOG_WARN(TAG, "Skipping health monitor init (temp sensors unavailable, WDT would trigger)");
    }

    run_indicator_init();

    nextion_hmi_init();

    LOGGER_LOG_INFO(TAG, "System initialized successfully");


    while (1)
    {
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}
