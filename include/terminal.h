#ifndef _TERMINAL_H_
#define _TERMINAL_H_

#include "stdbool.h"
#include "stdint.h"

#define TERMINAL_WINDOW_TITLE_MAX 256U

typedef struct {
    bool active;
    bool focused;
    uint32_t columns;
    uint32_t rows;
    uint32_t lines_written;
    char mode[16];
} terminal_info_t;

void terminal_init(void);
void terminal_set_focus(bool focused);
void terminal_note_output_line(void);
bool terminal_open_process_console(int32_t pid, const char *program_path);
void terminal_close_process_console(int32_t pid);
void terminal_finish_process_console(int32_t pid, int32_t exit_code);
bool terminal_set_process_console_title(int32_t pid, const char *title);
const terminal_info_t *terminal_info(void);

#endif
