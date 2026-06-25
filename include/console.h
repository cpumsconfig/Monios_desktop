#ifndef _CONSOLE_H_
#define _CONSOLE_H_

#include "stdint.h"

void console_clear(void);
void console_set_cursor(uint16_t row, uint16_t col);
void console_move_cursor(int32_t row_delta, int32_t col_delta);
void console_write_char(char ch);
void console_write(const char *str);
void console_write_at(uint16_t row, uint16_t col, const char *str);
void console_backspace(void);
uint16_t console_cursor_row(void);
uint16_t console_cursor_col(void);
const uint16_t *console_buffer(void);

#endif
