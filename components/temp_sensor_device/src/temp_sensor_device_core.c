#include "modbus_master.h"
#include "temp_sensor_device.h"
#include "temp_sensor_device_internal.h"
#include "sdkconfig.h"
#include "utils.h"
#include "ms9024.h"

static const char* TAG = "TEMP_SENSOR_DEVICE";

static temp_sensor_device_ctx ctx_pool[CONFIG_TEMP_SENSOR_DEVICE_MAX_DEVICES] = {0};

static esp_err_t temp_sensor_update(void *ctx);

static esp_err_t temp_sensor_init(void *ctx);
static esp_err_t temp_sensor_read(void *ctx, void *data_out);
static esp_err_t temp_sensor_write(void *ctx, const device_write_cmd_t *cmd);

static device_ops_t device_ops = {
    .init = temp_sensor_init,
    .update = temp_sensor_update,
    .read = temp_sensor_read,
    .write = temp_sensor_write,
    .shutdown = NULL
};

esp_err_t temp_sensor_create(device_t** device)
{
    if (device == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    LOGGER_LOG_DEBUG(TAG, "Creating temp sensor device %d", CONFIG_TEMP_SENSOR_DEVICE_MAX_DEVICES);
    for (uint16_t i = 0; i < CONFIG_TEMP_SENSOR_DEVICE_MAX_DEVICES; i++)
    {
        if (!ctx_pool[i].allocated)
        {
            ctx_pool[i].allocated = true;
            ctx_pool[i].valid = true;
            ctx_pool[i].id = i;
            ctx_pool[i].last_temperature = 0.0f;
            ctx_pool[i].modbus_address = CONFIG_TEMP_SENSOR_MODBUS_START_ADDRESS;
            ctx_pool[i].modbus_register = MS9024_REG_AOUT;
            CHECK_ERR_LOG_RET(device_manager_create_device(&ctx_pool[i],&device_ops, "temp_sensor", DEVICE_TYPE_TEMP_SENSOR, device),
                              "Failed to create temp sensor device");
            LOGGER_LOG_INFO(TAG, "Temp sensor device created with ID %d", ctx_pool[i].id);
            return ESP_OK;
        }
    }
    return ESP_ERR_NO_MEM;
}

esp_err_t temp_sensor_destroy(device_t* device)
{
    CHECK_ERR_LOG_RET(device_manager_destroy(device),
        "Failed to destroy temp sensor device");

    return ESP_OK;
}

static esp_err_t temp_sensor_read(void *ctx, void *data_out)
{
    const temp_sensor_device_ctx *device_ctx = (temp_sensor_device_ctx*)ctx;

    if (device_ctx == NULL || !device_ctx->allocated || !device_ctx->valid || data_out == NULL)
    {
        return ESP_ERR_INVALID_STATE;
    }

    *(float*)data_out = device_ctx->last_temperature;
    return ESP_OK;
}

static esp_err_t temp_sensor_init(void *ctx)
{
    temp_sensor_device_ctx *device_ctx = (temp_sensor_device_ctx*)ctx;
    if (device_ctx == NULL || !device_ctx->allocated || !device_ctx->valid)
    {
        return ESP_ERR_INVALID_STATE;
    }

    ms9024_log_config(device_ctx->modbus_address,MS9024_REG_PV_RAW);

    return ESP_OK;
}

static esp_err_t temp_sensor_update(void *ctx)
{
    temp_sensor_device_ctx *device_ctx = (temp_sensor_device_ctx*)ctx;
    if (device_ctx == NULL || !device_ctx->allocated || !device_ctx->valid)
    {
        return ESP_ERR_INVALID_STATE;
    }

    /* Read temperature from Modbus register */
    float last_temperature;
    CHECK_ERR_LOG_RET_FMT(ms9024_read_float(device_ctx->modbus_address, device_ctx->modbus_register, &last_temperature),
                          "Failed to read temperature from sensor at address %d, register %d",
                          device_ctx->modbus_address, device_ctx->modbus_register);

    device_ctx->last_temperature = last_temperature;
    return ESP_OK;
}

static esp_err_t temp_sensor_write(void *ctx, const device_write_cmd_t *cmd)
{
    temp_sensor_device_ctx *device_ctx = (temp_sensor_device_ctx*)ctx;
    if (device_ctx == NULL || !device_ctx->allocated || !device_ctx->valid)
    {
        return ESP_ERR_INVALID_STATE;
    }

    if (cmd == NULL || cmd->params == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    switch (cmd->cmd_id)
    {
        case TEMP_SENSOR_DEVICE_COMMAND_SET_REGISTER_VALUE:
        {
            temp_sensor_device_set_register_value_cmd_params_t *params =
                (temp_sensor_device_set_register_value_cmd_params_t*)cmd->params;

            CHECK_ERR_LOG_RET_FMT(ms9024_write_and_verify(device_ctx->modbus_address, params->register_address, params->value, "temp sensor register"),
                                  "Failed to write register %d at address %d",
                                  params->register_address, device_ctx->modbus_address);
            break;
        }
        case TEMP_SENSOR_REPAIR_FORM_GOOD_UNIT:
        {
            CHECK_ERR_LOG_RET_FMT(ms9024_repair_from_good_unit(device_ctx->modbus_address),
                                  "Failed to repair temp sensor at address %d",
                                  device_ctx->modbus_address);
            break;
        }
        default:
            return ESP_ERR_INVALID_ARG;
    }

    return ESP_OK;
}
