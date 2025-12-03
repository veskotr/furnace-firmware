/**
 * @file spi_bus_config.c
 * @brief SPI bus configuration helpers
 * 
 * Responsible for:
 * - Building config from Kconfig defaults
 * - Providing convenient configuration builders
 */

#include "spi_master_component.h"
#include "sdkconfig.h"
#include "logger_component.h"

static const char *TAG = "SPI_CONFIG";

// Static array of CS pins from Kconfig
static int s_default_cs_pins[] = {
    CONFIG_SPI_SLAVE1_CS,
#if CONFIG_SPI_MAX_NUM_SLAVES >= 2
    CONFIG_SPI_SLAVE2_CS,
#endif
#if CONFIG_SPI_MAX_NUM_SLAVES >= 3
    CONFIG_SPI_SLAVE3_CS,
#endif
#if CONFIG_SPI_MAX_NUM_SLAVES >= 4
    CONFIG_SPI_SLAVE4_CS,
#endif
#if CONFIG_SPI_MAX_NUM_SLAVES >= 5
    CONFIG_SPI_SLAVE5_CS,
#endif
#if CONFIG_SPI_MAX_NUM_SLAVES >= 6
    CONFIG_SPI_SLAVE6_CS,
#endif
#if CONFIG_SPI_MAX_NUM_SLAVES >= 7
    CONFIG_SPI_SLAVE7_CS,
#endif
#if CONFIG_SPI_MAX_NUM_SLAVES >= 8
    CONFIG_SPI_SLAVE8_CS,
#endif
#if CONFIG_SPI_MAX_NUM_SLAVES >= 9
    CONFIG_SPI_SLAVE9_CS,
#endif
};

esp_err_t spi_bus_config_from_kconfig(spi_driver_bus_config_t *config, uint8_t num_devices)
{
    if (config == NULL) {
        LOGGER_LOG_ERROR(TAG, "Config pointer is NULL");
        return ESP_ERR_INVALID_ARG;
    }
    
    if (num_devices == 0 || num_devices > CONFIG_SPI_MAX_NUM_SLAVES) {
        LOGGER_LOG_ERROR(TAG, "Invalid num_devices: %d (max: %d)", 
                        num_devices, CONFIG_SPI_MAX_NUM_SLAVES);
        return ESP_ERR_INVALID_ARG;
    }
    
    // Fill with Kconfig defaults
    config->miso_io = CONFIG_SPI_BUS_MISO;
    config->mosi_io = CONFIG_SPI_BUS_MOSI;
    config->sclk_io = CONFIG_SPI_BUS_SCK;
    config->max_transfer_size = CONFIG_SPI_MAX_TRANSFER_SIZE;
    config->host = HSPI_HOST;  // Could also be from Kconfig
    config->num_devices = num_devices;
    config->cs_pins = s_default_cs_pins;  // Point to static array
    config->clock_speed_hz = CONFIG_SPI_CLOCK_SPEED_HZ;
    config->mode = CONFIG_SPI_BUS_MODE;
    
    LOGGER_LOG_INFO(TAG, "Config from Kconfig: MISO=%d, MOSI=%d, SCLK=%d, devices=%d",
                    config->miso_io, config->mosi_io, config->sclk_io, num_devices);
    
    return ESP_OK;
}