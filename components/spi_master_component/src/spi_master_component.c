#include "spi_master_component.h"
#include "driver/spi_master.h"
#include "sdkconfig.h"

// Component tag
static const char *TAG = "SPI_MASTER";

// SPI device handles
spi_device_handle_t spi_slaves[CONFIG_SPI_MAX_NUM_SLAVES];

// Mutex for SPI bus access
static SemaphoreHandle_t spi_mutex = NULL;

static uint8_t _number_of_slaves;

// Chip select pins for slaves
static int cs_pins[CONFIG_SPI_MAX_NUM_SLAVES] = {
    CONFIG_SPI_SLAVE1_CS,
    CONFIG_SPI_SLAVE2_CS,
    CONFIG_SPI_SLAVE3_CS,
    CONFIG_SPI_SLAVE4_CS,
    CONFIG_SPI_SLAVE5_CS,
    CONFIG_SPI_SLAVE6_CS,
    CONFIG_SPI_SLAVE7_CS,
    CONFIG_SPI_SLAVE8_CS,
    CONFIG_SPI_SLAVE9_CS};

// Component initialized flag
static bool spi_initialized = false;

// ----------------------------
// Configuration
// ----------------------------
typedef struct
{
    int miso_io;
    int mosi_io;
    int sclk_io;
    int max_transfer_size;
    int clock_speed_hz;
    int mode;
    int queue_size;
} SpiConfig_t;

static const SpiConfig_t spi_config = {
    .miso_io = CONFIG_SPI_BUS_MISO,
    .mosi_io = CONFIG_SPI_BUS_MOSI,
    .sclk_io = CONFIG_SPI_BUS_SCK,
    .max_transfer_size = 32,
    .clock_speed_hz = CONFIG_SPI_CLOCK_SPEED_HZ,
    .mode = CONFIG_SPI_BUS_MODE,
    .queue_size = 1};
// ----------------------------
// Helpers
// ----------------------------
static esp_err_t add_spi_slave(int index)
{
    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = spi_config.clock_speed_hz,
        .mode = spi_config.mode,
        .spics_io_num = cs_pins[index],
        .queue_size = spi_config.queue_size};
    return spi_bus_add_device(HSPI_HOST, &devcfg, &spi_slaves[index]);
}

// ----------------------------
// Public API
// ----------------------------
esp_err_t init_spi(uint8_t number_of_slaves)
{
    if (number_of_slaves < CONFIG_SPI_MAX_NUM_SLAVES)

        return ESP_ERR_INVALID_ARG;

    _number_of_slaves = number_of_slaves;

    if (spi_initialized)
    {
        return ESP_OK;
    }

    if (spi_mutex == NULL)
    {
        spi_mutex = xSemaphoreCreateMutex();
    }

    esp_err_t ret;

    spi_bus_config_t buscfg = {
        .miso_io_num = spi_config.miso_io,
        .mosi_io_num = spi_config.mosi_io,
        .sclk_io_num = spi_config.sclk_io,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 32,
    };

    ret = spi_bus_initialize(HSPI_HOST, &buscfg, SPI_DMA_CH_AUTO);

    if (ret != ESP_OK)
    {
        logger_send_error(TAG, "Failed to initialize SPI bus: %s", esp_err_to_name(ret));
        return ret;
    }

    for (int i = 0; i < number_of_slaves; i++)
    {
        ret = add_spi_slave(i);
        if (ret != ESP_OK)
        {
            logger_send_error(TAG, "Failed to add SPI slave %d: %s", i, esp_err_to_name(ret));
            return ret;
        }
    }

    spi_initialized = true;

    return ESP_OK;
}

esp_err_t spi_transfer(int slave_index, uint8_t *tx, uint8_t *rx, size_t len)
{
    if (slave_index >= _number_of_slaves)
        return ESP_ERR_INVALID_ARG;

    if (xSemaphoreTake(spi_mutex, portMAX_DELAY) != pdTRUE)
        return ESP_ERR_TIMEOUT;

    spi_transaction_t t = {
        .length = len * 8,
        .tx_buffer = tx,
        .rx_buffer = rx};
    esp_err_t ret = spi_device_transmit(spi_slaves[slave_index], &t);

    xSemaphoreGive(spi_mutex);
    return ret;
}

esp_err_t shutdown_spi(void)
{
    if (!spi_initialized)
        return ESP_OK;

    for (int i = 0; i < CONFIG_SPI_MAX_NUM_SLAVES; i++)
    {
        if (spi_slaves[i])
        {
            spi_bus_remove_device(spi_slaves[i]);
            spi_slaves[i] = NULL;
        }
    }

    spi_bus_free(HSPI_HOST);

    if (spi_mutex)
    {
        vSemaphoreDelete(spi_mutex);
        spi_mutex = NULL;
    }

    spi_initialized = false;

    return ESP_OK;
}