#ifndef _CONSOLE_H_
#define _CONSOLE_H_

#include "stdbool.h"
#include "stdint.h"

#define CONSOLE_COLUMNS 80U
#define CONSOLE_ROWS 64U
#define CONSOLE_MAX_INSTANCES 16U

void console_init(void);
void console_clear(void);
void console_set_cursor(uint16_t row, uint16_t col);
void console_move_cursor(int32_t row_delta, int32_t col_delta);
void console_write_char(char ch);
void console_write_buffer(const void *buffer, uint32_t size);
void console_write(const char *str);
void console_write_at(uint16_t row, uint16_t col, const char *str);
void console_backspace(void);
uint16_t console_cursor_row(void);
uint16_t console_cursor_col(void);
const uint32_t *console_buffer(void);

bool console_create_for_pid(int32_t pid);
void console_destroy_for_pid(int32_t pid);
void console_clear_for_pid(int32_t pid);
void console_write_process_char(int32_t pid, char ch);
void console_write_process_buffer(int32_t pid, const void *buffer, uint32_t size);
const uint32_t *console_buffer_for_pid(int32_t pid);
uint16_t console_cursor_row_for_pid(int32_t pid);
uint16_t console_cursor_col_for_pid(int32_t pid);
uint32_t console_lines_for_pid(int32_t pid);

#endif
