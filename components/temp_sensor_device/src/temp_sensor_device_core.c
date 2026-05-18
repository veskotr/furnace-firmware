#include "modbus_master.h"
#include "temp_sensor_device.h"
#include "temp_sensor_device_internal.h"
#include "sdkconfig.h"
#include "utils.h"
#include "ms9024.h"

static const char* TAG = "TEMP_SENSOR_DEVICE";

static temp_sensor_device_t ctx_pool[CONFIG_TEMP_SENSOR_DEVICE_MAX_DEVICES] = {0};

static esp_err_t temp_sensor_update(void* ctx);

static esp_err_t temp_sensor_init(void* ctx);
static esp_err_t temp_sensor_read(void* ctx, void* data_out);
static esp_err_t temp_sensor_write(void* ctx, const device_write_cmd_t* cmd);

static device_ops_t device_ops = {
    .init = temp_sensor_init,
    .update = temp_sensor_update,
    .read = temp_sensor_read,
    .write = temp_sensor_write,
    .shutdown = NULL
};

esp_err_t temp_sensor_create(temp_sensor_device_t** device)
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
            ctx_pool[i].device_id = 0; /* populated from reg 127 during init */
            ctx_pool[i].last_temperature = 0.0f;
            ctx_pool[i].modbus_address = CONFIG_TEMP_SENSOR_MODBUS_START_ADDRESS + i;
            ctx_pool[i].modbus_register = MS9024_REG_PV;
            CHECK_ERR_LOG_RET(
                device_manager_create_device(&ctx_pool[i], &device_ops, "temp_sensor", DEVICE_TYPE_TEMP_SENSOR, &
                    ctx_pool
                    [i].device_handle),
                "Failed to create temp sensor device");
            LOGGER_LOG_INFO(TAG, "Temp sensor device created with ID %d", ctx_pool[i].id);
            *device = &ctx_pool[i];
            return ESP_OK;
        }
    }
    return ESP_ERR_NO_MEM;
}

esp_err_t temp_sensor_destroy(temp_sensor_device_t* device)
{
    if (device == NULL || !device->allocated || !device->valid)
    {
        return ESP_ERR_INVALID_ARG;
    }
    device->allocated = false;
    device->valid = false;

    CHECK_ERR_LOG_RET(device_manager_destroy(device->device_handle),
                      "Failed to destroy temp sensor device");

    return ESP_OK;
}

esp_err_t temp_sensor_set_device_state(const temp_sensor_device_t* device, const device_state_t new_state)
{
    if (device == NULL || !device->allocated || !device->valid)
    {
        return ESP_ERR_INVALID_ARG;
    }

    CHECK_ERR_LOG_RET(device_manager_set_device_state(device->device_handle, new_state),
                      "Failed to set temp sensor device state");

    return ESP_OK;
}

esp_err_t temp_sensor_read_device(const temp_sensor_device_t* device, void* data_out)
{
    if (device == NULL || !device->allocated || !device->valid)
    {
        return ESP_ERR_INVALID_ARG;
    }

    CHECK_ERR_LOG_RET_FMT(device_manager_read_device(device->device_handle, data_out),
                          "Failed to read from temp sensor device with ID %d", device->id);

    return ESP_OK;
}

uint16_t temp_sensor_get_id(const temp_sensor_device_t* device)
{
    if (device == NULL || !device->allocated || !device->valid)
    {
        return 0;
    }
    return device->device_id;
}

esp_err_t temp_sensor_write_device(const temp_sensor_device_t* device, const device_write_cmd_t* cmd)
{
    if (device == NULL || !device->allocated || !device->valid)
    {
        return ESP_ERR_INVALID_ARG;
    }

    CHECK_ERR_LOG_RET_FMT(device_manager_write_device(device->device_handle, cmd),
                          "Failed to write to temp sensor device with ID %d", device->id);

    return ESP_OK;
}

static esp_err_t temp_sensor_read(void* ctx, void* data_out)
{
    const temp_sensor_device_t* device_ctx = (temp_sensor_device_t*)ctx;

    if (device_ctx == NULL || !device_ctx->allocated || !device_ctx->valid || data_out == NULL)
    {
        return ESP_ERR_INVALID_STATE;
    }

    *(float*)data_out = device_ctx->last_temperature;
    return ESP_OK;
}

static esp_err_t temp_sensor_init(void* ctx)
{
    temp_sensor_device_t* device_ctx = (temp_sensor_device_t*)ctx;
    if (device_ctx == NULL || !device_ctx->allocated || !device_ctx->valid)
    {
        return ESP_ERR_INVALID_STATE;
    }

    /* Read the preset device ID from reg 127. Never written by our code.
     *
     * NOTE: previously this function ran ms9024_read_float(PV) and on failure
     * (including transient Modbus timeouts during bus warm-up) called
     * ms9024_repair_from_good_unit(), which writes hardcoded values to regs
     * 27, 30, 129. The "good" values were captured from one specific unit
     * and applying them blindly can corrupt the configuration of a healthy
     * slave (see ms9024_healthy_registers.txt). Auto-repair is disabled here.
     * Use temp_sensor_write_device + TEMP_SENSOR_REPAIR_FORM_GOOD_UNIT to
     * trigger it manually on a sensor you know needs it. */
    uint16_t device_id = 0;
    const esp_err_t id_err = ms9024_read_uint16(device_ctx->modbus_address,
                                                MS9024_REG_DEVICE_ID, &device_id);
    if (id_err == ESP_OK)
    {
        device_ctx->device_id = device_id;
        LOGGER_LOG_INFO(TAG, "Sensor at addr %d → preset device ID = %u",
                        device_ctx->modbus_address, device_id);
    }
    else
    {
        LOGGER_LOG_WARN(TAG, "Failed to read device ID (reg %d) from sensor at addr %d: %s",
                        MS9024_REG_DEVICE_ID, device_ctx->modbus_address,
                        esp_err_to_name(id_err));
    }

    return ESP_OK;
}

static esp_err_t temp_sensor_update(void* ctx)
{
    temp_sensor_device_t* device_ctx = (temp_sensor_device_t*)ctx;
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

static esp_err_t temp_sensor_write(void* ctx, const device_write_cmd_t* cmd)
{
    temp_sensor_device_t* device_ctx = (temp_sensor_device_t*)ctx;
    if (device_ctx == NULL || !device_ctx->allocated || !device_ctx->valid)
    {
        return ESP_ERR_INVALID_STATE;
    }

    if (cmd == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    switch (cmd->cmd_id)
    {
    case TEMP_SENSOR_DEVICE_COMMAND_SET_REGISTER_VALUE:
        {
            if (cmd->params == NULL)
            {
                return ESP_ERR_INVALID_ARG;
            }
            temp_sensor_device_set_register_value_cmd_params_t* params =
                (temp_sensor_device_set_register_value_cmd_params_t*)cmd->params;

            CHECK_ERR_LOG_RET_FMT(
                ms9024_write_and_verify(device_ctx->modbus_address, params->register_address, params->value,
                    "temp sensor register"),
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
