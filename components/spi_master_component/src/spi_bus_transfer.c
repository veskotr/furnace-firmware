/**
 * @file spi_bus_transfer.c
 * @brief SPI bus data transfer operations
 * 
 * Responsible for:
 * - Data transfer (read/write/full-duplex)
 * - Thread-safe access via mutex
 * - Error handling for transfers
 */

#include "spi_bus_internal.h"
#include "logger_component.h"

static const char *TAG = "SPI_BUS_XFER";

// ============================================================================
// TRANSFER OPERATIONS
// ============================================================================

esp_err_t spi_bus_transfer(spi_bus_handle_t handle, 
                           uint8_t device_index,
                           const uint8_t *tx_data, 
                           uint8_t *rx_data, 
                           size_t length)
{
    // Validate handle
    if (handle == NULL) {
        LOGGER_LOG_ERROR(TAG, "Handle is NULL");
        return ESP_ERR_INVALID_ARG;
    }
    
    struct spi_bus_context_t *ctx = (struct spi_bus_context_t *)handle;
    
    if (!ctx->initialized) {
        LOGGER_LOG_ERROR(TAG, "Bus not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    // Validate parameters
    if (device_index >= ctx->config.num_devices) {
        LOGGER_LOG_ERROR(TAG, "Invalid device index: %d (max: %d)", 
                        device_index, ctx->config.num_devices - 1);
        return ESP_ERR_INVALID_ARG;
    }
    
    if (length == 0) {
        LOGGER_LOG_ERROR(TAG, "Transfer length is zero");
        return ESP_ERR_INVALID_ARG;
    }
    
    if (tx_data == NULL && rx_data == NULL) {
        LOGGER_LOG_ERROR(TAG, "Both tx_data and rx_data are NULL");
        return ESP_ERR_INVALID_ARG;
    }
    
    if (length > ctx->config.max_transfer_size) {
        LOGGER_LOG_ERROR(TAG, "Transfer length %d exceeds max %d", 
                        length, ctx->config.max_transfer_size);
        return ESP_ERR_INVALID_ARG;
    }
    
    // Acquire mutex for thread-safe access
    if (xSemaphoreTake(ctx->mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        LOGGER_LOG_ERROR(TAG, "Failed to acquire mutex (timeout)");
        ctx->error_count++;
        return ESP_ERR_TIMEOUT;
    }
    
    // Prepare transaction
    spi_transaction_t trans = {
        .length = length * 8,  // Length in bits
        .tx_buffer = tx_data,
        .rx_buffer = rx_data,
        .user = NULL
    };
    
    // Execute transfer
    esp_err_t ret = spi_device_transmit(ctx->device_handles[device_index], &trans);
    
    // Update statistics
    if (ret == ESP_OK) {
        ctx->transfer_count++;
    } else {
        ctx->error_count++;
        LOGGER_LOG_ERROR(TAG, "Transfer failed (device=%d, len=%d): %s",
                        device_index, length, esp_err_to_name(ret));
    }
    
    // Release mutex
    xSemaphoreGive(ctx->mutex);
    
    return ret;
}

// ============================================================================
// QUERY FUNCTIONS
// ============================================================================

uint8_t spi_bus_get_device_count(spi_bus_handle_t handle)
{
    if (handle == NULL) {
        return 0;
    }
    
    struct spi_bus_context_t *ctx = (struct spi_bus_context_t *)handle;
    return ctx->config.num_devices;
}

bool spi_bus_is_valid(spi_bus_handle_t handle)
{
    if (handle == NULL) {
        return false;
    }
    
    struct spi_bus_context_t *ctx = (struct spi_bus_context_t *)handle;
    return ctx->initialized;
}