#include "nextion_hmi.h"

#include "sdkconfig.h"
#include "hmi_coordinator_internal.h"
#include "nextion_transport_internal.h"
#include "heating_program_models_internal.h"

#include "logger_core.h"

static const char *TAG = "nextion_hmi";

void nextion_hmi_init(void)
{
    // Initialize program model mutex before anything else
    program_models_init();

    nextion_uart_init();

    // Start coordinator (command queue + worker task) before RX task
    hmi_coordinator_init();

    nextion_rx_task_start();

    // Post initial display setup command to the coordinator queue
    hmi_coordinator_post_cmd(HMI_CMD_INIT_DISPLAY);

    LOGGER_LOG_INFO(TAG, "Nextion HMI initialized");
}
