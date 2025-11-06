
#include "logger-component.h"

void app_main(void)
{
    logger_init();

    while(1){
        logget_send_info("APP", "Application is running");
        vTaskDelay(2000 / portTICK_PERIOD_MS);
    }

}
