#include "nextion_ui_internal.h"

#include "nextion_transport_internal.h"
#include <stdio.h>

void nextion_show_error(const char *message)
{
    if (!message) {
        return;
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

void nextion_clear_error(void)
{
    nextion_send_cmd("errText.txt=\"\"");
    nextion_send_cmd("errTxtHead.txt=\"\"");
    nextion_send_cmd("vis errTxtHead,0");
    nextion_send_cmd("vis errText,0");
    nextion_send_cmd("vis errTxtCloseB,0");
}
