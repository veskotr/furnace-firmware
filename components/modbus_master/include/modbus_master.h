#ifndef MODBUS_MASTER_H
#define MODBUS_MASTER_H
#include "esp_err.h"
#include "portmacro.h"
#include "stdbool.h"

typedef struct
{
    uint8_t uart_num;
    uint8_t tx_pin;
    uint8_t rx_pin;
    uint8_t de_pin;
    uint32_t baud_rate;
} modbus_config_t;

typedef enum
{
    DEVICE_TYPE_UNKNOWN = 0,
    DEVICE_TYPE_TEMPERATURE_SENSOR = 1
} device_type_t;

typedef struct
{
    bool used;
    uint8_t slave_addr;

    device_type_t device_type;

    uint16_t reg_start;
    uint16_t reg_count;

    TickType_t last_poll_tick;

    esp_err_t (*process_data)(uint16_t *raw); // optional
} modbus_device_t;

esp_err_t modbus_master_init(const modbus_config_t* config);

esp_err_t modbus_master_shutdown(void);

#endif // MODBUS_MASTER_H
