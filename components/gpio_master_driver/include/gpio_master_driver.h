#ifndef GPIO_MASTER_DRIVER_H
#define GPIO_MASTER_DRIVER_H

#include "esp_err.h"

esp_err_t gpio_master_driver_init(void);

esp_err_t gpio_master_set_level(int gpio_num, int level);
esp_err_t gpio_master_get_level(int gpio_num, int *level);
esp_err_t gpio_master_set_pin_mode(int gpio_num, int mode, int pull_up, int pull_down);
esp_err_t gpio_master_deinit(void);

#endif // GPIO_MASTER_DRIVER_H