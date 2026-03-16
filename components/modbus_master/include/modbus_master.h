#pragma once

#include "esp_err.h"

typedef enum {
    MB_FUNC_READ_COILS               = 0x01,
    MB_FUNC_READ_DISCRETE_INPUTS     = 0x02,
    MB_FUNC_READ_HOLDING_REGISTER    = 0x03,
    MB_FUNC_READ_INPUT_REGISTER      = 0x04,
    MB_FUNC_WRITE_SINGLE_COIL        = 0x05,
    MB_FUNC_WRITE_SINGLE_REGISTER    = 0x06,
    MB_FUNC_WRITE_MULTIPLE_COILS     = 0x0F,
    MB_FUNC_WRITE_MULTIPLE_REGISTERS = 0x10,
    MB_FUNC_READ_WRITE_REGISTERS     = 0x17,
    MB_FUNC_DEVICE_IDENTIFICATION    = 0x2B
} mb_function_code_t;

typedef struct
{
    uint8_t uart_num;
    uint8_t tx_pin;
    uint8_t rx_pin;
    uint8_t de_pin;
    uint32_t baud_rate;
} modbus_config_t;

esp_err_t modbus_master_init(const modbus_config_t *config);

esp_err_t modbus_master_shutdown(void);

esp_err_t modbus_master_read_register(uint8_t slave_addr,
                                       uint16_t reg,
                                       uint16_t *dest);

esp_err_t modbus_master_read_registers(uint8_t slave_addr,
                                       uint16_t reg_start,
                                       uint16_t reg_count,
                                       uint16_t *dest);

esp_err_t modbus_master_write_register(uint8_t slave_addr,
                                       uint16_t reg,
                                       uint16_t value);

esp_err_t modbus_master_write_registers(uint8_t slave_addr,
                                        uint16_t reg_start,
                                        uint16_t reg_count,
                                        const uint16_t *values);

esp_err_t modbus_master_send_request_raw(uint8_t slave_addr,
                                         mb_function_code_t command,
                                         uint16_t reg_start,
                                         uint16_t reg_size,
                                         void *data);
