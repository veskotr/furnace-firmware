#ifndef UTILS_H
#define UTILS_H
#include "logger_component.h"
#include "esp_err.h"

// Check error and log a message (continue)
#define CHECK_ERR_LOG(expr, msg)                                         \
    do                                                                   \
    {                                                                    \
        esp_err_t _ret = (expr);                                         \
        if (_ret != ESP_OK)                                              \
        {                                                                \
            LOGGER_LOG_ERROR(TAG, "%s: %s", msg, esp_err_to_name(_ret)); \
        }                                                                \
    } while (0)

// Check error, log a message, and call an action (continue)
#define CHECK_ERR_LOG_CALL(expr, action, msg)                            \
    do                                                                   \
    {                                                                    \
        esp_err_t _ret = (expr);                                         \
        if (_ret != ESP_OK)                                              \
        {                                                                \
            LOGGER_LOG_ERROR(TAG, "%s: %s", msg, esp_err_to_name(_ret)); \
            action;                                                      \
        }                                                                \
    } while (0)

// Check error, log a message, and return the error
#define CHECK_ERR_LOG_RET(expr, msg)                                     \
    do                                                                   \
    {                                                                    \
        esp_err_t _ret = (expr);                                         \
        if (_ret != ESP_OK)                                              \
        {                                                                \
            LOGGER_LOG_ERROR(TAG, "%s: %s", msg, esp_err_to_name(_ret)); \
            return _ret;                                                 \
        }                                                                \
    } while (0)

// Check error, log a message, call an action, and return the error
#define CHECK_ERR_LOG_CALL_RET(expr, action, msg)                        \
    do                                                                   \
    {                                                                    \
        esp_err_t _ret = (expr);                                         \
        if (_ret != ESP_OK)                                              \
        {                                                                \
            LOGGER_LOG_ERROR(TAG, "%s: %s", msg, esp_err_to_name(_ret)); \
            action;                                                      \
            return _ret;                                                 \
        }                                                                \
    } while (0)
#define CHECK_ERR_LOG_RET_FMT(expr, fmt, ...)                                      \
    do                                                                             \
    {                                                                              \
        esp_err_t _ret = (expr);                                                   \
        if (_ret != ESP_OK)                                                        \
        {                                                                          \
            LOGGER_LOG_ERROR(TAG, fmt ": %s", __VA_ARGS__, esp_err_to_name(_ret)); \
            return _ret;                                                           \
        }                                                                          \
    } while (0)
#define CHECK_ERR_LOG_FMT(expr, fmt, ...)                                          \
    do                                                                             \
    {                                                                              \
        esp_err_t _ret = (expr);                                                   \
        if (_ret != ESP_OK)                                                        \
        {                                                                          \
            LOGGER_LOG_ERROR(TAG, fmt ": %s", __VA_ARGS__, esp_err_to_name(_ret)); \
        }                                                                          \
    } while (0)
#endif // UTILS_H