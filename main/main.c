
#include "logger_component.h"
#include "spi_master_component.h"

void app_main(void)
{
    logger_init();
    esp_err_t ret = init_spi(1);
    if (ret != ESP_OK)
    {
        logger_send_error("APP", "Failed to initialize SPI: %s", esp_err_to_name(ret));
        return;
    }

    logger_send_info("APP", "Application is running");

    uint8_t tx_data[4] = {'T', 'E', 'S', 'T'};
    uint8_t rx_data[4] = {0};

    uint8_t messge_index = 0;
    while (1)
    {
        spi_transfer(0, &tx_data[messge_index], &rx_data[messge_index], 1);
        logger_send_info("APP", "SPI Transfer completed. Received: %c", rx_data[messge_index]);
        if(++messge_index > 3)
        {
            messge_index = 0;
        }
    
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
