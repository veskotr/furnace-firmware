#ifndef SPI_BUS_INTERNAL_H
#define SPI_BUS_INTERNAL_H

#include "spi_master_component.h"
#include "driver/spi_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

// ============================================================================
// INTERNAL STRUCTURES (not exposed to users)
// ============================================================================

/**
 * @brief Internal SPI bus context
 * 
 * This structure is opaque to users. Only internal implementation
 * files can access these fields.
 */
struct spi_bus_context_t {
    // Configuration
    spi_driver_bus_config_t config;
    
    // State
    bool initialized;
    
    // Hardware handles
    spi_device_handle_t *device_handles;  // Array of device handles
    
    // Synchronization
    SemaphoreHandle_t mutex;
    
    // Statistics (optional)
    uint32_t transfer_count;
    uint32_t error_count;
};

// ============================================================================
// INTERNAL HELPERS (used across implementation files)
// ============================================================================

/**
 * @brief Validate SPI bus configuration
 * 
 * @param config Configuration to validate
 * @return ESP_OK if valid, error code otherwise
 */
esp_err_t spi_internal_validate_config(const spi_driver_bus_config_t *config);

/**
 * @brief Add a single device to the bus
 * 
 * @param ctx Bus context
 * @param index Device index
 * @return ESP_OK on success
 */
esp_err_t spi_internal_add_device(struct spi_bus_context_t *ctx, uint8_t index);

/**
 * @brief Remove all devices from the bus
 * 
 * @param ctx Bus context
 */
void spi_internal_remove_all_devices(struct spi_bus_context_t *ctx);

/**
 * @brief Get the logger tag for SPI component
 */
const char* spi_internal_get_tag(void);

#endif // SPI_BUS_INTERNAL_H