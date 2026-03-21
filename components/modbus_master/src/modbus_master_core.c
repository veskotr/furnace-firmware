//
// Created by vesko on 3.3.2026 г..
//
#include "esp_err.h"
#include "esp_modbus_common.h"
#include "esp_modbus_master.h"
#include "logger_component.h"
#include "modbus_master.h"
#include "utils.h"

static const char* TAG = "MODBUS_MASTER";

static void* master_handle;

esp_err_t modbus_master_init(const modbus_config_t* config)
{
    LOGGER_LOG_INFO(TAG, "Modbus transport initialized");
    if (config == NULL)
    {
        LOGGER_LOG_ERROR(TAG, "Invalid transport config");
        return ESP_ERR_INVALID_ARG;
    }

    mb_communication_info_t comm_info = {
        .ser_opts.port = config->uart_num,
        .ser_opts.mode = MB_RTU,
        .ser_opts.baudrate = config->baud_rate,
        .ser_opts.parity = MB_PARITY_NONE,
        .ser_opts.data_bits = UART_DATA_8_BITS,
        .ser_opts.stop_bits = UART_STOP_BITS_1,
        .ser_opts.uid = 0,
        .ser_opts.response_tout_ms = 1000,
    };

    CHECK_ERR_LOG_CALL_RET(mbc_master_create_serial(&comm_info, &master_handle),
                           modbus_master_shutdown(),
                           "Failed to create Modbus master");
    if (master_handle == NULL)
    {
        LOGGER_LOG_ERROR(TAG, "Failed to create Modbus master: %d");
        return ESP_ERR_INVALID_ARG;
    }

    CHECK_ERR_LOG_CALL_RET(
        uart_set_pin(config->uart_num, config->tx_pin, config->rx_pin, config->de_pin, UART_PIN_NO_CHANGE),
        modbus_master_shutdown(),
        "Failed to set UART pins");

    CHECK_ERR_LOG_CALL_RET(
        uart_set_mode(config->uart_num, UART_MODE_RS485_HALF_DUPLEX),
        modbus_master_shutdown(),
        "Failed to set UART mode"
    );

    vTaskDelay(pdMS_TO_TICKS(5)); // Let the UART settle

    CHECK_ERR_LOG_CALL_RET(mbc_master_start(master_handle),
                           modbus_master_shutdown(),
                           "Failed to start Modbus master");

    LOGGER_LOG_INFO(TAG, "Modbus master initialized on UART%d (TX=%d, RX=%d, DE=%d, baud=%d)",
                    config->uart_num,
                    config->tx_pin,
                    config->rx_pin,
                    config->de_pin,
                    config->baud_rate);

    return ESP_OK;
}

esp_err_t modbus_master_shutdown(void)
{
    if (master_handle == NULL)
    {
        return ESP_OK;
    }

    CHECK_ERR_LOG_RET(mbc_master_stop(master_handle),
                      "Failed to stop Modbus master");
    CHECK_ERR_LOG_RET(mbc_master_delete(master_handle),
                      "Failed to delete Modbus master");

    LOGGER_LOG_INFO(TAG, "Modbus transport shutdown complete");
    return ESP_OK;
}

esp_err_t modbus_master_read_register(uint8_t slave_addr,
                                      uint16_t reg,
                                      uint16_t* dest)
{
    return modbus_master_send_request_raw(slave_addr, MB_FUNC_READ_HOLDING_REGISTER, reg, 1, dest);
}

esp_err_t modbus_master_read_registers(uint8_t slave_addr,
                                       uint16_t reg_start,
                                       uint16_t reg_count,
                                       uint16_t* dest)
{
    return modbus_master_send_request_raw(slave_addr, MB_FUNC_READ_HOLDING_REGISTER, reg_start, reg_count, dest);
}

esp_err_t modbus_master_write_register(uint8_t slave_addr,
                                       uint16_t reg,
                                       uint16_t value)
{
    return modbus_master_send_request_raw(slave_addr, MB_FUNC_WRITE_SINGLE_REGISTER, reg, 1, &value);
}

esp_err_t modbus_master_write_registers(uint8_t slave_addr,
                                        uint16_t reg_start,
                                        uint16_t reg_count,
                                        const uint16_t* values)
{
    return modbus_master_send_request_raw(slave_addr, MB_FUNC_WRITE_MULTIPLE_REGISTERS, reg_start, reg_count,
                                          (void*)values);
}

esp_err_t modbus_master_send_request_raw(uint8_t slave_addr,
                                         mb_function_code_t command,
                                         uint16_t reg_start,
                                         uint16_t reg_size,
                                         void* data)

{
    mb_param_request_t request = {
        .slave_addr = slave_addr,
        .command = command,
        .reg_start = reg_start,
        .reg_size = reg_size
    };

    return mbc_master_send_request(master_handle, &request, data);
}