#ifndef SPI_MASTER_COMPONENT_H
#define SPI_MASTER_COMPONENT_H

#include "esp_err.h"
#include "driver/spi_master.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// TYPES
// ============================================================================

/**
 * @brief Opaque handle to an initialized SPI bus
 * 
 */
typedef struct spi_bus_context_t* spi_bus_handle_t;

/**
 * @brief SPI bus configuration
 */
typedef struct {
    // Bus pins
    int miso_io;              ///< MISO pin
    int mosi_io;              ///< MOSI pin
    int sclk_io;              ///< Clock pin
    
    // Bus configuration
    int max_transfer_size;    ///< Maximum transfer size in bytes
    spi_host_device_t host;   ///< SPI host (HSPI_HOST, VSPI_HOST, etc.)
    
    // Device configuration
    uint8_t num_devices;      ///< Number of devices on this bus
    int *cs_pins;             ///< Array of chip select pins
    int clock_speed_hz;       ///< Clock speed in Hz
    int mode;                 ///< SPI mode (0-3)
} spi_driver_bus_config_t;

// ============================================================================
// LIFECYCLE FUNCTIONS
// ============================================================================

/**
 * @brief Create SPI bus configuration from Kconfig defaults
 * 
 * Fills a configuration structure with values from menuconfig.
 * You can then modify individual fields before calling spi_bus_init().
 * 
 * @param[out] config Configuration structure to fill
 * @param[in] num_devices Number of devices to use (must be <= CONFIG_SPI_MAX_NUM_SLAVES)
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG if num_devices is invalid
 * 
 * @note The cs_pins array will point to static memory - safe to use directly
 * 
 * Example:
 * @code
 * spi_bus_config_t config;
 * spi_bus_config_from_kconfig(&config, 3);
 * config.clock_speed_hz = 2000000;  // Override clock speed
 * spi_bus_init(&config, &handle);
 * @endcode
 */
esp_err_t spi_bus_config_from_kconfig(spi_driver_bus_config_t *config, uint8_t num_devices);

/**
 * @brief Initialize SPI bus and create handle
 * 
 * Creates a new SPI bus with the specified configuration.
 * The returned handle must be freed with spi_bus_shutdown().
 * 
 * @param[in] config Bus configuration (must not be NULL)
 * @param[out] out_handle Created handle (set to NULL on failure)
 * @return 
 *     - ESP_OK on success
 *     - ESP_ERR_INVALID_ARG if config or out_handle is NULL
 *     - ESP_ERR_NO_MEM if allocation fails
 *     - Other error codes from SPI driver
 * 
 * @note This function is thread-safe
 */
esp_err_t spi_bus_init(const spi_driver_bus_config_t *config, spi_bus_handle_t *out_handle);

/**
 * @brief Shutdown SPI bus and free handle
 * 
 * Removes all devices, frees the bus, and deallocates the handle.
 * Safe to call with NULL handle (no-op).
 * 
 * @param[in] handle Handle to shutdown (can be NULL)
 * @return ESP_OK on success
 * 
 * @note This function is thread-safe
 * @note After calling this, the handle is invalid and must not be used
 */
esp_err_t spi_bus_shutdown(spi_bus_handle_t handle);

// ============================================================================
// OPERATION FUNCTIONS
// ============================================================================

/**
 * @brief Transfer data to/from SPI device
 * 
 * Performs a synchronous SPI transfer. This function blocks until
 * the transfer completes.
 * 
 * @param[in] handle SPI bus handle
 * @param[in] device_index Which device to talk to (0 to num_devices-1)
 * @param[in] tx_data Data to transmit (can be NULL for read-only)
 * @param[out] rx_data Buffer for received data (can be NULL for write-only)
 * @param[in] length Number of bytes to transfer
 * @return 
 *     - ESP_OK on success
 *     - ESP_ERR_INVALID_ARG if parameters are invalid
 *     - ESP_ERR_INVALID_STATE if bus not initialized
 *     - ESP_ERR_TIMEOUT if mutex acquisition times out
 * 
 * @note This function is thread-safe (uses internal mutex)
 * @note Either tx_data or rx_data (or both) must be non-NULL
 */
esp_err_t spi_bus_transfer(spi_bus_handle_t handle, 
                           uint8_t device_index,
                           const uint8_t *tx_data, 
                           uint8_t *rx_data, 
                           size_t length);

// ============================================================================
// QUERY FUNCTIONS
// ============================================================================

/**
 * @brief Get number of devices on this bus
 * 
 * @param[in] handle SPI bus handle
 * @return Number of devices, or 0 if handle is NULL/invalid
 */
uint8_t spi_bus_get_device_count(spi_bus_handle_t handle);

/**
 * @brief Check if handle is valid
 * 
 * @param[in] handle Handle to check
 * @return true if handle is valid and initialized, false otherwise
 */
bool spi_bus_is_valid(spi_bus_handle_t handle);

// ============================================================================
// LEGACY API (Deprecated - for backward compatibility only)
// ============================================================================

/**
 * @deprecated Use spi_bus_init() instead
 */
esp_err_t init_spi(uint8_t number_of_slaves) __attribute__((deprecated));

/**
 * @deprecated Use spi_bus_transfer() instead
 */
esp_err_t spi_transfer(int slave_index, uint8_t *tx, uint8_t *rx, size_t len) __attribute__((deprecated));

/**
 * @deprecated Use spi_bus_shutdown() instead
 */
esp_err_t shutdown_spi(void) __attribute__((deprecated));

#ifdef __cplusplus
}
#endif

#endif // SPI_MASTER_COMPONENT_H