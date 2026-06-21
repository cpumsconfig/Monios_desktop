#include "input.h"
#include "kernel.h"
#include "keyboard.h"
#include "mouse.h"
#include "usb.h"

static input_status_t g_input_status;

void init_input(void)
{
    g_input_status.usb_legacy_compat = init_usb();

    init_keyboard();
    g_input_status.keyboard_ready = true;

    init_mouse();
    g_input_status.mouse_ready = true;

    log_write("input: ps/2 input path ready");
    if (g_input_status.usb_legacy_compat) {
        log_write("input: usb legacy emulation compatibility expected");
    } else {
        log_write("input: no usb compatibility path detected");
    }
}

const input_status_t *input_status(void)
{
    return &g_input_status;
}
