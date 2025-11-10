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
    for (size_t i = 0; i < temp_monitor.number_of_attatched_sensors; i++)
    {
        ret = init_temp_sensor(i);
        if (ret != ESP_OK)
        {
            logger_send_error(TAG, "Failed to initialize temperature sensor %d: %s", i, esp_err_to_name(ret));
            return ret;
        }
    }

    return ESP_OK;
}

static esp_err_t init_temp_sensor(uint8_t sensor_index)
{
    //send config register via SPI
    spi_transfer(sensor_index, NULL, NULL, 0); // Placeholder for actual SPI transfer
    return ESP_OK;
}

static esp_err_t read_temp_sensors_data(uint16_t *data_buffer, size_t *buffer_len)
{

    return ESP_OK;
}

static esp_err_t read_temperature_sensor(uint8_t sensor_index, float *temperature)
{

    spi_transfer(sensor_index, NULL, NULL, 0); // Placeholder for actual SPI transfer

    return ESP_OK;
}

static esp_err_t process_temperature_data(uint16_t sensor_data, float *temperature)
{

    return ESP_OK;
}
