#include "gpio_master_driver.h"
#include "driver/gpio.h"
#include "esp_err.h"
#include "sdkconfig.h"
#include "logger_component.h"
#include "utils.h"
#include "freertos/semphr.h"

static const char *TAG = "GPIO_MASTER_DRIVER";

static SemaphoreHandle_t gpio_mutex = NULL;

esp_err_t gpio_master_driver_init(void)
{
    if (gpio_mutex == NULL)
    {
        gpio_mutex = xSemaphoreCreateMutex();
        if (gpio_mutex == NULL)
        {
            LOGGER_LOG_ERROR(TAG, "Failed to create GPIO mutex");
            return ESP_FAIL;
        }
    }

    LOGGER_LOG_INFO(TAG, "GPIO Master Driver initialized");
    return ESP_OK;
}

esp_err_t gpio_master_set_pin_mode(int gpio_num, int mode, int pull_up, int pull_down)
{
    if (gpio_mutex == NULL)
    {
        LOGGER_LOG_ERROR(TAG, "GPIO Master Driver not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << gpio_num),
        .mode = mode,
        .pull_up_en = pull_up ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE,
        .pull_down_en = pull_down ? GPIO_PULLDOWN_ENABLE : GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE};

    xSemaphoreTake(gpio_mutex, portMAX_DELAY);
    esp_err_t ret = gpio_config(&io_conf);
    xSemaphoreGive(gpio_mutex);

    return ret;
}

esp_err_t gpio_master_set_level(int gpio_num, int level)
{
    if (gpio_mutex == NULL)
    {
        LOGGER_LOG_ERROR(TAG, "GPIO Master Driver not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    xSemaphoreTake(gpio_mutex, portMAX_DELAY);
    esp_err_t ret = gpio_set_level(gpio_num, level);
    xSemaphoreGive(gpio_mutex);

    return ret;
}

esp_err_t gpio_master_get_level(int gpio_num, int *level)
{
    if (gpio_mutex == NULL)
    {
        LOGGER_LOG_ERROR(TAG, "GPIO Master Driver not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    xSemaphoreTake(gpio_mutex, portMAX_DELAY);
    int gpio_level = gpio_get_level(gpio_num);
    xSemaphoreGive(gpio_mutex);

    if (gpio_level < 0)
    {
        return ESP_FAIL;
    }

    *level = gpio_level;
    return ESP_OK;
}

esp_err_t gpio_master_deinit(void)
{
    if (gpio_mutex != NULL)
    {
        vSemaphoreDelete(gpio_mutex);
        gpio_mutex = NULL;
    }

    LOGGER_LOG_INFO(TAG, "GPIO Master Driver deinitialized");
    return ESP_OK;
}