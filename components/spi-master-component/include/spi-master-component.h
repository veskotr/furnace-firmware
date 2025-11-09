#ifndef SPI_MASTER_COMPONENT_H
#define SPI_MASTER_COMPONENT_H

#include "esp_err.h"
#include <inttypes.h>
#include "logger-component.h"

esp_err_t init_spi(uint8_t number_of_slaves);

esp_err_t spi_transfer(int slave_index, uint8_t *tx, uint8_t *rx, size_t len);

esp_err_t shutdown_spi(void);

#endif // SPI_MASTER_COMPONENT_H