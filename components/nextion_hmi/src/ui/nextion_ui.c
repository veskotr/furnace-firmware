#include "nextion_ui_internal.h"

#include "nextion_transport_internal.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>

/* True while a buttonless "loading" dialog is on screen. The line router
 * checks this to ignore input until the long-running operation finishes. */
static bool s_loading_active = false;

void nextion_show_error(const char *message)
{
    if (!message) {
        return;
    }

    /* If a loading dialog is up, drop it so the error is visible and input
     * is unblocked again. */
    if (s_loading_active) {
        nextion_hide_loading();
    }

    nextion_send_cmd("errTxtHead.txt=\"Error\"");
    nextion_send_cmd("errText.txt=\"\"");

    char cmd[96];
    snprintf(cmd, sizeof(cmd), "errText.txt=\"%s\"", message);
    nextion_send_cmd(cmd);

    nextion_send_cmd("vis errTxtHead,1");
    nextion_send_cmd("vis errText,1");
    nextion_send_cmd("vis errTxtCloseB,1");
}

void nextion_show_success(const char *message)
{
    if (!message) {
        return;
    }

    /* A completed operation ends the loading dialog. */
    if (s_loading_active) {
        nextion_hide_loading();
    }

    nextion_send_cmd("errTxtHead.txt=\"Success\"");
    nextion_send_cmd("errText.txt=\"\"");

    char cmd[96];
    snprintf(cmd, sizeof(cmd), "errText.txt=\"%s\"", message);
    nextion_send_cmd(cmd);

    nextion_send_cmd("vis errTxtHead,1");
    nextion_send_cmd("vis errText,1");
    nextion_send_cmd("vis errTxtCloseB,1");
}

void nextion_clear_error(void)
{
    nextion_send_cmd("errText.txt=\"\"");
    nextion_send_cmd("errTxtHead.txt=\"\"");
    nextion_send_cmd("vis errTxtHead,0");
    nextion_send_cmd("vis errText,0");
    nextion_send_cmd("vis errTxtCloseB,0");
}

void nextion_show_loading(const char *message)
{
    if (!message) {
        message = "Please wait...";
    }

    char cmd[96];
    snprintf(cmd, sizeof(cmd), "confirmTxt.txt=\"%.40s\"", message);
    nextion_send_cmd(cmd);
    vTaskDelay(pdMS_TO_TICKS(20));

    /* Body + text only — no confirm/cancel buttons, so there is no way for
     * the user to dismiss it from the panel. */
    nextion_send_cmd("vis confirmBdy,1");
    vTaskDelay(pdMS_TO_TICKS(20));
    nextion_send_cmd("vis confirmTxt,1");
    vTaskDelay(pdMS_TO_TICKS(20));

    s_loading_active = true;
}

void nextion_hide_loading(void)
{
    nextion_send_cmd("vis confirmBdy,0");
    nextion_send_cmd("vis confirmTxt,0");
    s_loading_active = false;
}

bool nextion_is_loading(void)
{
    return s_loading_active;
}
