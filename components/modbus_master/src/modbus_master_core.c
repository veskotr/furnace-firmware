#include "esp_err.h"
#include "modbus_master.h"
#include "logger_component.h"
#include "utils.h"

#include "transport/modbus_transport.h"

static const char* TAG = "MODBUS_MASTER";

esp_err_t modbus_master_init(const modbus_config_t* config)
{
    if (config == NULL)
    {
        LOGGER_LOG_ERROR(TAG, "  - config is NULL");
        return ESP_ERR_INVALID_ARG;
    }

    modbus_transport_config_t transport_config = {
        .uart_num = config->uart_num,
        .tx_pin = config->tx_pin,
        .rx_pin = config->rx_pin,
        .de_pin = config->de_pin,
        .baud_rate = config->baud_rate
    };

    CHECK_ERR_LOG_CALL_RET(modbus_transport_init(&transport_config),
                           modbus_master_shutdown(),
                           "Failed to initialize Modbus transport");

    LOGGER_LOG_INFO(TAG, "Modbus master initialized on UART%d (TX=%d, RX=%d, DE=%d, baud=%d)",
                    transport_config.uart_num,
                    transport_config.tx_pin,
                    transport_config.rx_pin,
                    transport_config.de_pin,
                    transport_config.baud_rate);


    return ESP_OK;
}
