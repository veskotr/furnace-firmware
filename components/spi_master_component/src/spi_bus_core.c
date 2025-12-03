/**
 * @file spi_bus_core.c
 * @brief SPI bus lifecycle management (init/shutdown)
 * 
 * Responsible for:
 * - Bus initialization
 * - Resource allocation
 * - Bus shutdown
 * - Resource cleanup
 */

#include "spi_bus_internal.h"
#include "logger_component.h"
#include <stdlib.h>
#include <string.h>

static const char *TAG = "SPI_BUS_CORE";

const char* spi_internal_get_tag(void)
{
    return TAG;
}

// ============================================================================
// VALIDATION
// ============================================================================

esp_err_t spi_internal_validate_config(const spi_driver_bus_config_t *config)
{
    if (config == NULL) {
        LOGGER_LOG_ERROR(TAG, "Config is NULL");
        return ESP_ERR_INVALID_ARG;
    }
    
    if (config->num_devices == 0 || config->num_devices > 9) {
        LOGGER_LOG_ERROR(TAG, "Invalid number of devices: %d (must be 1-9)", 
                        config->num_devices);
        return ESP_ERR_INVALID_ARG;
    }
    
    if (config->cs_pins == NULL) {
        LOGGER_LOG_ERROR(TAG, "CS pins array is NULL");
        return ESP_ERR_INVALID_ARG;
    }
    
    if (config->max_transfer_size <= 0) {
        LOGGER_LOG_ERROR(TAG, "Invalid max transfer size: %d", 
                        config->max_transfer_size);
        return ESP_ERR_INVALID_ARG;
    }
    
    if (config->clock_speed_hz <= 0 || config->clock_speed_hz > 80000000) {
        LOGGER_LOG_ERROR(TAG, "Invalid clock speed: %d Hz", config->clock_speed_hz);
        return ESP_ERR_INVALID_ARG;
    }
    
    if (config->mode < 0 || config->mode > 3) {
        LOGGER_LOG_ERROR(TAG, "Invalid SPI mode: %d (must be 0-3)", config->mode);
        return ESP_ERR_INVALID_ARG;
    }
    
    return ESP_OK;
}

// ============================================================================
// DEVICE MANAGEMENT
// ============================================================================

esp_err_t spi_internal_add_device(struct spi_bus_context_t *ctx, uint8_t index)
{
    if (ctx == NULL || index >= ctx->config.num_devices) {
        return ESP_ERR_INVALID_ARG;
    }
    
    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = ctx->config.clock_speed_hz,
        .mode = ctx->config.mode,
        .spics_io_num = ctx->config.cs_pins[index],
        .queue_size = 1,
        .flags = 0,
        .pre_cb = NULL,
        .post_cb = NULL
    };
    
    esp_err_t ret = spi_bus_add_device(ctx->config.host, &devcfg, 
                                       &ctx->device_handles[index]);
    
    if (ret != ESP_OK) {
        LOGGER_LOG_ERROR(TAG, "Failed to add device %d: %s", 
                        index, esp_err_to_name(ret));
    }
    
    return ret;
}

void spi_internal_remove_all_devices(struct spi_bus_context_t *ctx)
{
    if (ctx == NULL || ctx->device_handles == NULL) {
        return;
    }
    
    for (uint8_t i = 0; i < ctx->config.num_devices; i++) {
        if (ctx->device_handles[i] != NULL) {
            spi_bus_remove_device(ctx->device_handles[i]);
            ctx->device_handles[i] = NULL;
        }
    }
}

// ============================================================================
// INITIALIZATION
// ============================================================================

esp_err_t spi_bus_init(const spi_driver_bus_config_t *config, spi_bus_handle_t *out_handle)
{
    LOGGER_LOG_INFO(TAG, "Initializing SPI bus (host=%d, devices=%d)...",
                    config ? config->host : -1,
                    config ? config->num_devices : 0);
    
    if (out_handle == NULL) {
        LOGGER_LOG_ERROR(TAG, "Output handle pointer is NULL");
        return ESP_ERR_INVALID_ARG;
    }
    
    *out_handle = NULL;  // Initialize to NULL
    
    // Validate configuration
    esp_err_t ret = spi_internal_validate_config(config);
    if (ret != ESP_OK) {
        return ret;
    }
    
    // Allocate context
    struct spi_bus_context_t *ctx = calloc(1, sizeof(struct spi_bus_context_t));
    if (ctx == NULL) {
        LOGGER_LOG_ERROR(TAG, "Failed to allocate context (%d bytes)", 
                        sizeof(struct spi_bus_context_t));
        return ESP_ERR_NO_MEM;
    }
    
    // Copy configuration
    ctx->config = *config;
    
    // Allocate device handles array
    ctx->device_handles = calloc(config->num_devices, sizeof(spi_device_handle_t));
    if (ctx->device_handles == NULL) {
        LOGGER_LOG_ERROR(TAG, "Failed to allocate device handles array");
        free(ctx);
        return ESP_ERR_NO_MEM;
    }
    LOGGER_LOG_INFO(TAG, "✓ Memory allocated");
    
    // Create mutex for thread-safe access
    ctx->mutex = xSemaphoreCreateMutex();
    if (ctx->mutex == NULL) {
        LOGGER_LOG_ERROR(TAG, "Failed to create mutex");
        free(ctx->device_handles);
        free(ctx);
        return ESP_FAIL;
    }
    LOGGER_LOG_INFO(TAG, "✓ Mutex created");
    
    // Initialize SPI bus hardware
    spi_bus_config_t buscfg = {
        .miso_io_num = config->miso_io,
        .mosi_io_num = config->mosi_io,
        .sclk_io_num = config->sclk_io,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = config->max_transfer_size,
    };
    
    ret = spi_bus_initialize(config->host, &buscfg, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK) {
        LOGGER_LOG_ERROR(TAG, "Failed to initialize SPI bus: %s", 
                        esp_err_to_name(ret));
        vSemaphoreDelete(ctx->mutex);
        free(ctx->device_handles);
        free(ctx);
        return ret;
    }
    LOGGER_LOG_INFO(TAG, "✓ SPI bus initialized (MISO=%d, MOSI=%d, SCLK=%d)",
                    config->miso_io, config->mosi_io, config->sclk_io);
    
    // Add all devices to the bus
    for (uint8_t i = 0; i < config->num_devices; i++) {
        ret = spi_internal_add_device(ctx, i);
        if (ret != ESP_OK) {
            LOGGER_LOG_ERROR(TAG, "Failed to add device %d (CS=%d)", 
                           i, config->cs_pins[i]);
            
            // Cleanup already added devices
            spi_internal_remove_all_devices(ctx);
            spi_bus_free(config->host);
            vSemaphoreDelete(ctx->mutex);
            free(ctx->device_handles);
            free(ctx);
            return ret;
        }
    }
    LOGGER_LOG_INFO(TAG, "✓ %d devices added to bus", config->num_devices);
    
    // Mark as initialized
    ctx->initialized = true;
    *out_handle = ctx;
    
    LOGGER_LOG_INFO(TAG, "SPI bus initialized successfully");
    return ESP_OK;
}

// ============================================================================
// SHUTDOWN
// ============================================================================

esp_err_t spi_bus_shutdown(spi_bus_handle_t handle)
{
    if (handle == NULL) {
        LOGGER_LOG_WARN(TAG, "Shutdown called with NULL handle (no-op)");
        return ESP_OK;
    }
    
    struct spi_bus_context_t *ctx = (struct spi_bus_context_t *)handle;
    
    LOGGER_LOG_INFO(TAG, "Shutting down SPI bus (host=%d)...", ctx->config.host);
    
    if (!ctx->initialized) {
        LOGGER_LOG_WARN(TAG, "Bus not initialized, cleaning up memory only");
        free(ctx);
        return ESP_OK;
    }
    
    // Remove all devices (reverse order of init)
    spi_internal_remove_all_devices(ctx);
    LOGGER_LOG_INFO(TAG, "✓ Devices removed");
    
    // Free SPI bus
    esp_err_t ret = spi_bus_free(ctx->config.host);
    if (ret != ESP_OK) {
        LOGGER_LOG_WARN(TAG, "Failed to free SPI bus: %s", esp_err_to_name(ret));
        // Continue cleanup anyway
    } else {
        LOGGER_LOG_INFO(TAG, "✓ Bus freed");
    }
    
    // Delete mutex
    if (ctx->mutex != NULL) {
        vSemaphoreDelete(ctx->mutex);
        ctx->mutex = NULL;
    }
    LOGGER_LOG_INFO(TAG, "✓ Mutex deleted");
    
    // Free memory (reverse order of allocation)
    free(ctx->device_handles);
    ctx->device_handles = NULL;
    
    free(ctx);
    
    LOGGER_LOG_INFO(TAG, "SPI bus shut down successfully");
    return ESP_OK;
}