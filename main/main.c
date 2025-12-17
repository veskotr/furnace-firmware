#include "logger_component.h"
#include "temperature_monitor_component.h"
#include "temperature_processor_component.h"
#include "coordinator_component.h"
#include "event_manager.h"
#include "utils.h"
#include "sdkconfig.h"
#include "health_monitor.h"

static const char* TAG = "main";

void app_main(void)
{
    logger_init();
    CHECK_ERR_LOG(event_manager_init(),
                  "Failed to initialize event manager");

    temp_monitor_config_t temp_monitor_config = {
        .number_of_attached_sensors = 5
    };
    CHECK_ERR_LOG(init_temp_monitor(&temp_monitor_config),
                  "Failed to initialize temperature monitor");

    CHECK_ERR_LOG(init_temp_processor(),
                  "Failed to initialize temperature processor");

    const coordinator_config_t coordinator_config = {
        .profiles = NULL,
        .num_profiles = 0
    };
    CHECK_ERR_LOG(init_coordinator(&coordinator_config),
                  "Failed to initialize coordinator");

    CHECK_ERR_LOG(init_health_monitor(),
                  "Failed to initialize health monitor");

    LOGGER_LOG_INFO(TAG, "System initialized successfully");


    while (1)
    {
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}
