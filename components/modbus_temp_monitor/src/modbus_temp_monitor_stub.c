/**
 * @file modbus_temp_monitor_stub.c
 * @brief No-op stubs when CONFIG_MODBUS_TEMP_ENABLED is not set.
 */

#include "modbus_temp_monitor.h"

esp_err_t modbus_temp_monitor_init(void)     { return ESP_OK; }
esp_err_t modbus_temp_monitor_shutdown(void)  { return ESP_OK; }
