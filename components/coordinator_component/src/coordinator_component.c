#include "coordinator_component.h"

#include "logger_component.h"

#include "temperature_processor_component.h"
#include "temperature_profile_controller.h"
#include "temperature_monitor_component.h"
#include "coordinator_component_events.h"
#include "coordinator_component_log.h"
#include "utils.h"
#include "sdkconfig.h"
#include "heating_profile_task.h"

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

    CHECK_ERR_LOG_RET(init_heater_controller_component(coordinator_event_loop_handle),
                      "Failed to initialize heater controller component");

    return ESP_OK;
}

esp_err_t coordinator_list_heating_profiles(void)
{
    if (coordinator_heating_profiles == NULL || number_of_profiles == 0)
    {
        LOGGER_LOG_WARN(TAG, "No heating profiles available");
        return ESP_ERR_NOT_FOUND;
    }

    LOGGER_LOG_INFO(TAG, "Available Heating Profiles:");
    for (size_t i = 0; i < number_of_profiles; i++)
    {
        const heating_profile_t *profile = &coordinator_heating_profiles[i];
        LOGGER_LOG_INFO(TAG, "Profile Index: %d, Name: %s, Duration: %d ms, Target Temp: %.2f C",
                        i,
                        profile->name);
    }
    return ESP_OK;
}

esp_err_t stop_coordinator(void)
{
    CHECK_ERR_LOG_RET(shutdown_temp_monitor(),
                      "Failed to shutdown temperature monitor component");

    CHECK_ERR_LOG_RET(shutdown_heater_controller_component(),
                      "Failed to shutdown heater controller component");

    CHECK_ERR_LOG_RET(shutdown_coordinator_events(),
                      "Failed to shutdown coordinator events");

    CHECK_ERR_LOG_RET(shutdown_temp_processor(),
                      "Failed to shutdown temperature processor component");

    CHECK_ERR_LOG_RET(stop_heating_profile(),
                      "Failed to stop heating profile");

    return ESP_OK;
}
