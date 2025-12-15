#ifndef EVENT_MANAGER_H
#define EVENT_MANAGER_H

#include "esp_err.h"
#include "esp_event.h"
#include <stdint.h>

// ============================================================================
// PUBLIC API - Event Registration & Publishing
// ============================================================================

/**
 * @brief Initialize the global event manager
 * @return ESP_OK on success
 */
esp_err_t event_manager_init(void);

/**
 * @brief Shutdown the global event manager
 * @return ESP_OK on success
 */
esp_err_t event_manager_shutdown(void);

/**
 * @brief Subscribe to an event
 * 
 * @param event_base Event base (e.g., COORDINATOR_EVENT)
 * @param event_id Event ID within that base
 * @param handler Callback function
 * @param handler_arg Argument passed to handler
 * @return ESP_OK on success
 */
esp_err_t event_manager_subscribe(
    esp_event_base_t event_base,
    int32_t event_id,
    esp_event_handler_t handler,
    void *handler_arg);

/**
 * @brief Unsubscribe from an event
 */
esp_err_t event_manager_unsubscribe(
    esp_event_base_t event_base,
    int32_t event_id,
    esp_event_handler_t handler);

/**
 * @brief Post an event to the event loop
 * 
 * @param event_base Event base
 * @param event_id Event ID
 * @param event_data Pointer to event data (can be NULL)
 * @param event_data_size Size of event data in bytes
 * @param ticks_to_wait Max ticks to wait if queue is full
 * @return ESP_OK on success
 */
esp_err_t event_manager_post(
    esp_event_base_t event_base,
    int32_t event_id,
    void *event_data,
    size_t event_data_size,
    TickType_t ticks_to_wait);

/**
 * @brief Convenience wrapper - post with immediate timeout
 */
esp_err_t event_manager_post_immediate(
    esp_event_base_t event_base,
    int32_t event_id,
    void *event_data,
    size_t event_data_size);

/**
 * @brief Convenience wrapper - post with blocking timeout
 */
esp_err_t event_manager_post_blocking(
    esp_event_base_t event_base,
    int32_t event_id,
    void *event_data,
    size_t event_data_size);

/**
 * @brief Get the event loop handle (if needed for advanced usage)
 * Should be rarely needed - components should use event_manager_post() instead
 */
esp_event_loop_handle_t event_manager_get_loop(void);

#endif // EVENT_MANAGER_H