#include "temperature_sensors.h"
#include "temperature_monitor_internal_types.h"
#include "logger_component.h"
#include "spi_master_component.h"
#include "max31865_registers.h"
#include <math.h>
#include <float.h>
#include "utils.h"
#include "temperature_monitor_log.h"
static const char *TAG = TEMP_MONITOR_LOG;

const max31865_registers_t max31865_registers = {
    .config_register_read_address = 0x00,
    .config_register_write_address = 0x80,
    .rtd_msb_read_address = 0x01,
    .rtd_lsb_read_address = 0x02,
    .high_fault_threshold_msb_read_address = 0x03,
    .high_fault_threshold_msb_write_address = 0x83,
    .high_fault_threshold_lsb_read_address = 0x04,
    .high_fault_threshold_lsb_write_address = 0x84,
    .low_fault_threshold_msb_read_address = 0x05,
    .low_fault_threshold_msb_write_address = 0x85,
    .low_fault_threshold_lsb_read_address = 0x06,
    .low_fault_threshold_lsb_write_address = 0x86,
    .fault_status_read_address = 0x07};

static esp_err_t init_temp_sensor(uint8_t sensor_index, uint8_t sensor_config);

static float process_temperature_data(uint16_t sensor_data);

static esp_err_t read_temp_sensor(uint8_t sensor_index, temp_sensor_t *data);

static esp_err_t handle_max31865_fault(uint8_t sensor_index, uint8_t *fault_byte, max31865_fault_flags_t *fault_flags);

static esp_err_t parse_max31865_faults(uint8_t *fault_byte, max31865_fault_flags_t *fault_flags);

static temp_sensor_fault_type_t classify_sensor_fault(temp_sensor_t *sensor_data);

esp_err_t init_temp_sensors(void)
{
    uint8_t config_value = 0;
    config_value |= (1 << 7); // Vbias ON
    config_value |= (1 << 4); // 3-wire RTD
    config_value |= (1 << 1); // Auto conversion mode
    config_value |= (0 << 1); // Filter 50Hz

    for (size_t i = 0; i < temp_monitor.number_of_attached_sensors; i++)
    {
        CHECK_ERR_LOG_RET_FMT(init_temp_sensor(i, config_value), "Failed to initialize temperature sensor %d", i);
    }

    return ESP_OK;
}

esp_err_t init_temp_sensor(uint8_t sensor_index, uint8_t sensor_config)
{
    // send config register via SPI
    uint8_t tx_buff[2] = {
        max31865_registers.config_register_write_address,
        sensor_config};
    CHECK_ERR_LOG_RET(spi_transfer(sensor_index, tx_buff, NULL, sizeof(tx_buff)), "Failed to send config to temperature sensor"); // Send config
    return ESP_OK;
}

esp_err_t read_temp_sensors_data(temp_sensor_t *data_buffer)
{
    for (size_t i = 0; i < temp_monitor.number_of_attached_sensors; i++)
    {
        CHECK_ERR_LOG_RET_FMT(read_temp_sensor(i, &data_buffer[i]), "Failed to read temperature sensor %d data", i);
    }

    return ESP_OK;
}

static esp_err_t read_temp_sensor(uint8_t sensor_index, temp_sensor_t *data)
{
    uint8_t tx_data[3] = {max31865_registers.rtd_msb_read_address, 0x00, 0x00};
    uint8_t rx_data[3] = {0};

    data->index = sensor_index;
    data->sensor_fault.raw_fault_byte = 0;
    data->sensor_fault.faults = (max31865_fault_flags_t){0};
    data->valid = true;
    data->sensor_fault.type = SENSOR_ERR_NONE;

    // Read 2 bytes (MSB + LSB)
    CHECK_ERR_LOG_RET_FMT(spi_transfer(sensor_index, tx_data, rx_data, sizeof(tx_data)),
                          "Failed to read temperature sensor %d data",
                          sensor_index);

    uint16_t raw = ((uint16_t)rx_data[1] << 8) | rx_data[2];

    if (raw & 0x0001)
    {
        LOGGER_LOG_ERROR(TAG, "Fault detected in temperature sensor %d", sensor_index);
        CHECK_ERR_LOG_RET_FMT(handle_max31865_fault(sensor_index, &data->sensor_fault.raw_fault_byte, &data->sensor_fault.faults),
                              "Failed to handle fault for sensor %d",
                              sensor_index);
        data->sensor_fault.type = classify_sensor_fault(data);
        
        data->valid = false;
        return ESP_OK;
    }

    uint16_t raw_data = raw >> 1; // Remove fault bit and align data

    data->temperature_c = process_temperature_data(raw_data);

    return ESP_OK;
}

static float process_temperature_data(uint16_t sensor_data)
{
    // TODO: Extract consts to macros or config
    //  Convert raw data to temperature using Callendar-Van Dusen equation
    const float R0 = 100.0;                             // Resistance at 0°C for PT100
    const float A = 3.9083e-3;                          // Callendar-Van Dusen coefficient A
    const float B = -5.775e-7;                          // Callendar-Van Dusen coefficient B
    float resistance = (sensor_data * 400.0) / 32768.0; // Assuming Vref = 3.3V and RTD excitation current of 1mA
    float temp_c = (-A + sqrtf(A * A - 4 * B * (1 - (resistance / R0)))) / (2 * B);

    return temp_c;
}

static esp_err_t handle_max31865_fault(uint8_t sensor_index, uint8_t *fault_byte, max31865_fault_flags_t *fault_flags)
{
    uint8_t config;
    uint8_t addr;

    // Read fault status
    addr = max31865_registers.fault_status_read_address;
    CHECK_ERR_LOG_RET_FMT(spi_transfer(sensor_index, &addr, fault_byte, 1),
                          "Failed to read fault status from sensor %d",
                          sensor_index);

    // Read config
    addr = max31865_registers.config_register_read_address;
    CHECK_ERR_LOG_RET_FMT(spi_transfer(sensor_index, &addr, &config, 1),
                          "Failed to read config from sensor %d",
                          sensor_index);

    // Clear fault bit
    config |= (1 << 1);
    uint8_t config_tx_buffer[2] = {
        max31865_registers.config_register_write_address,
        config};
    CHECK_ERR_LOG_RET_FMT(spi_transfer(sensor_index, config_tx_buffer, NULL, 2),
                          "Failed to clear fault bit on sensor %d",
                          sensor_index);

    // Parse fault flags
    CHECK_ERR_LOG_RET_FMT(parse_max31865_faults(fault_byte, fault_flags), "Failed to parse fault flags for sensor %d", sensor_index);

    return ESP_OK;
}

static esp_err_t parse_max31865_faults(uint8_t *fault_byte, max31865_fault_flags_t *fault_flags)
{
    // Parse fault flags
    fault_flags->high_threshold = (*fault_byte & MAX31865_FAULT_HIGHTHRESH) != 0;
    fault_flags->low_threshold = (*fault_byte & MAX31865_FAULT_LOWTHRESH) != 0;
    fault_flags->refin_force_closed = (*fault_byte & MAX31865_FAULT_REFIN_FORCE_C) != 0;
    fault_flags->refin_force_open = (*fault_byte & MAX31865_FAULT_REFIN_FORCE_O) != 0;
    fault_flags->rtdin_force_open = (*fault_byte & MAX31865_FAULT_RTDIN_FORCE_O) != 0;
    fault_flags->over_under_voltage = (*fault_byte & MAX31865_FAULT_OV_UV) != 0;

    return ESP_OK;
}

// Classify sensor error based on internal sensor data
static temp_sensor_fault_type_t classify_sensor_fault(temp_sensor_t *sensor_data)
{
    if (sensor_data->sensor_fault.raw_fault_byte == 0)
    {
        return SENSOR_ERR_NONE;
    }
    else if (sensor_data->sensor_fault.faults.rtdin_force_open || sensor_data->sensor_fault.faults.refin_force_open || sensor_data->sensor_fault.faults.refin_force_closed)
    {
        return SENSOR_ERR_RTD_FAULT;
    }
    else if (sensor_data->sensor_fault.faults.over_under_voltage)
    {
        return SENSOR_ERR_COMMUNICATION;
    }
    else
    {
        return SENSOR_ERR_UNKNOWN;
    }
}

