#include "temperature_sensors.h"
#include "logger_component.h"
#include "spi_master_component.h"
#include "max31865_registers.h"

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
    .fault_status = 0x07};

static esp_err_t init_temp_sensors(void)
{
    esp_err_t ret;
    uint8_t config_value = 0;
    config_value |= (1 << 7); // Vbias ON
    config_value |= (1 << 4); // 3-wire RTD
    config_value |= (1 << 1); // Auto conversion mode
    config_value |= (0 << 1); // Filter 50Hz

    for (size_t i = 0; i < temp_monitor.number_of_attatched_sensors; i++)
    {
        ret = init_temp_sensor(i, config_value);
        if (ret != ESP_OK)
        {
            logger_send_error(TAG, "Failed to initialize temperature sensor %d: %s", i, esp_err_to_name(ret));
            return ret;
        }
    }

    return ESP_OK;
}

static esp_err_t init_temp_sensor(uint8_t sensor_index, uint8_t sensor_config)
{
    // send config register via SPI
    uint8_t addr = max31865_registers.config_register_write_address;

    spi_transfer(sensor_index, &addr, NULL, 0);          // Transmit address
    spi_transfer(sensor_index, &sensor_config, NULL, 1); // Transmit config value
    return ESP_OK;
}

static esp_err_t read_temp_sensors_data(float *data_buffer)
{

    return ESP_OK;
}

static esp_err_t read_temperature_sensor(uint8_t sensor_index, float *temperature)
{
    esp_err_t ret;

    uint8_t addr = max31865_registers.rtd_msb_read_address; // MSB address
    uint8_t rx_data[2] = {0};

    // Read 2 bytes (MSB + LSB) in one SPI transaction
    ret = spi_transfer(sensor_index, &addr, rx_data, 2);
    if (ret != ESP_OK)
    {
        logger_send_error(TAG, "Failed to read temperature sensor %d data: %s",
                          sensor_index, esp_err_to_name(ret));
        return ret;
    }

    uint16_t raw = ((uint16_t)rx_data[0] << 8) | rx_data[1];

   if (raw & 0x0001)
    {
        logger_send_error(TAG, "Fault detected in temperature sensor %d", sensor_index);
        return ESP_FAIL;
    }

    uint16_t raw_data = raw >> 1; // Remove fault bit and align data

    *temperature = process_temperature_data(raw_data);

    return ESP_OK;
}

static float process_temperature_data(uint16_t sensor_data)
{
    //TODO: Extract consts to macros or config
    // Convert raw data to temperature using Callendar-Van Dusen equation
    const float R0 = 100.0;                             // Resistance at 0°C for PT100
    const float A = 3.9083e-3;                          // Callendar-Van Dusen coefficient A
    const float B = -5.775e-7;                          // Callendar-Van Dusen coefficient B
    float resistance = (sensor_data * 400.0) / 32768.0; // Assuming Vref = 3.3V and RTD excitation current of 1mA
    float temp_c = (-A + sqrtf(A * A - 4 * B * (1 - (resistance / R0)))) / (2 * B);

    return temp_c;
}
