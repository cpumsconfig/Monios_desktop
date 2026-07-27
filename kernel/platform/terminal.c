#include "common.h"
#include "console.h"
#include "graphics.h"
#include "kernel.h"
#include "terminal.h"

static terminal_info_t g_terminal_info;

void terminal_init(void)
{
    memset(&g_terminal_info, 0, sizeof(g_terminal_info));
    g_terminal_info.active = true;
    g_terminal_info.columns = CONSOLE_COLUMNS;
    g_terminal_info.rows = CONSOLE_ROWS;
    strcpy(g_terminal_info.mode, "utf8-console");
    log_write("console: UTF-8 console manager ready");
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

bool terminal_open_process_console(int32_t pid, const char *program_path)
{
    if (pid < 0) {
        return false;
    }
    if (!console_create_for_pid(pid)) {
        log_write("console: instance limit reached");
        return false;
    }
    g_terminal_info.active = true;
    g_terminal_info.focused = true;
    return graphics_open_process_console_window(pid, program_path);
}

void terminal_close_process_console(int32_t pid)
{
    if (pid < 0) {
        return;
    }
    graphics_close_process_console_window(pid);
    console_destroy_for_pid(pid);
    g_terminal_info.focused = graphics_terminal_has_focus();
}

void terminal_finish_process_console(int32_t pid, int32_t exit_code)
{
    if (pid < 0) {
        return;
    }
    graphics_mark_process_console_finished(pid, exit_code);
    g_terminal_info.focused = graphics_terminal_has_focus();
}

bool terminal_set_process_console_title(int32_t pid, const char *title)
{
    if (pid < 0 || title == NULL || title[0] == '\0') {
        return false;
    }
    return graphics_set_process_console_window_title(pid, title);
}

const terminal_info_t *terminal_info(void)
{
    return &g_terminal_info;
}
