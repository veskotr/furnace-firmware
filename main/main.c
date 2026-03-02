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

#ifdef CONFIG_MODBUS_TEMP_ENABLED
#include "modbus_temp_monitor.h"
#endif

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

    bool sensors_available = false;

#ifdef CONFIG_MODBUS_TEMP_ENABLED
    /* ── MODBUS path: MS9024 provides temperature via RS485 ────────── */
    LOGGER_LOG_WARN(TAG, "*** MODBUS temperature source (MS9024) ***");
    LOGGER_LOG_WARN(TAG, "SPI temperature monitor and processor SKIPPED");
    sensors_available = true;   /* MODBUS component posts events directly */
#else
    /* ── SPI path: MAX31865 thermocouples ──────────────────────────── */
    temp_monitor_config_t temp_monitor_config = {
        .number_of_attached_sensors = 5
    };
    esp_err_t temp_err = init_temp_monitor(&temp_monitor_config);
    if (temp_err != ESP_OK) {
        LOGGER_LOG_ERROR(TAG, "Failed to initialize temperature monitor: %s (continuing without sensors)",
                         esp_err_to_name(temp_err));
    } else {
        sensors_available = true;
    }

    if (sensors_available) {
        CHECK_ERR_LOG_CALL(init_temp_processor(),
                           return,
                           "Failed to initialize temperature processor");
    } else {
        LOGGER_LOG_WARN(TAG, "Skipping temperature processor init (no sensors)");
    }
#endif

    const coordinator_config_t coordinator_config = { 0 };
    CHECK_ERR_LOG_CALL(init_coordinator(&coordinator_config),
                       return,
                       "Failed to initialize coordinator");

#ifdef CONFIG_MODBUS_TEMP_ENABLED
    /* Start MODBUS monitor AFTER coordinator (needs event base ready) */
    CHECK_ERR_LOG_CALL(modbus_temp_monitor_init(),
                       return,
                       "Failed to initialize MODBUS temperature monitor");
#endif

    if (sensors_available) {
#ifndef CONFIG_MODBUS_TEMP_ENABLED
        /* Health monitor expects heartbeats from temp_monitor/processor;
           those don't exist in MODBUS mode, so only init for SPI path. */
        CHECK_ERR_LOG_CALL(init_health_monitor(),
                           return,
                           "Failed to initialize health monitor");
#else
        LOGGER_LOG_WARN(TAG, "Health monitor skipped in MODBUS mode (no temp_monitor heartbeats)");
#endif
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
