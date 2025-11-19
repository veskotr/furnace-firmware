#include "coordinator_component.h"

#include "logger_component.h"

#include "temperature_processor_component.h"
#include "temperature_profile_controller.h"
#include "temperature_monitor_component.h"
#include "coordinator_component_events.h"
#include "coordinator_component_log.h"
#include "utils.h"
#include "sdkconfig.h"
#include "coordinator_component_task.h"
#include <memory.h>

const char *TAG = COORDINATOR_COMPONENT_LOG;

heating_profile_t *coordinator_heating_profiles = NULL;
size_t number_of_profiles = 0;

esp_err_t init_coordinator(const heating_profile_t *profiles, size_t num_profiles)
{
    coordinator_heating_profiles = (heating_profile_t *)profiles;
    number_of_profiles = num_profiles;

    // Initialize Coordinator Events
    CHECK_ERR_LOG_RET(init_coordinator_events(),
                      "Failed to initialize coordinator events");

    // Temp monitor component configuration
    temp_monitor_config_t temp_monitor_config = {
        .number_of_attached_sensors = 5, // TODO make configurable
        .temperature_events_loop_handle = coordinator_event_loop_handle};
    // Initialize Temperature Monitor Component
    CHECK_ERR_LOG_RET(init_temp_monitor(&temp_monitor_config),
                      "Failed to initialize temperature monitor component");

    // Initialize Temperature Processor Component
    CHECK_ERR_LOG_RET(init_temp_processor(coordinator_event_loop_handle),
                      "Failed to initialize temperature processor component");
    return ESP_OK;
}

esp_err_t stop_coordinator(void)
{
    // Shutdown Temperature Processor Component
    CHECK_ERR_LOG_RET(shutdown_temp_processor(),
                      "Failed to shutdown temperature processor component");

    // Shutdown Temperature Monitor Component
    CHECK_ERR_LOG_RET(shutdown_temp_monitor(),
                      "Failed to shutdown temperature monitor component");

    // Shutdown Coordinator Events
    CHECK_ERR_LOG_RET(shutdown_coordinator_events(),
                      "Failed to shutdown coordinator events");

    // Shutdown Coordinator Task
    CHECK_ERR_LOG_RET(shutdown_coordinator_task(),
                      "Failed to shutdown coordinator task");

    return ESP_OK;
}

#include "sdkconfig.h"
