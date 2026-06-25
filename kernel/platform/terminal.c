#include "common.h"
#include "graphics.h"
#include "kernel.h"
#include "terminal.h"

static terminal_info_t g_terminal_info;

void terminal_init(void)
{
    memset(&g_terminal_info, 0, sizeof(g_terminal_info));
    g_terminal_info.active = true;
    g_terminal_info.columns = 100;
    g_terminal_info.rows = 32;
    strcpy(g_terminal_info.mode, "ansi-lite");
    log_write("terminal: ansi-lite emulator ready");
}

void terminal_set_focus(bool focused)
{
    g_terminal_info.focused = focused;
}

void terminal_note_output_line(void)
{
    g_terminal_info.lines_written++;
    g_terminal_info.focused = graphics_terminal_has_focus();
}

const terminal_info_t *terminal_info(void)
{
    return &g_terminal_info;
}
