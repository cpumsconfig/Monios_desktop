#ifndef _TERMINAL_H_
#define _TERMINAL_H_

#include "stdbool.h"
#include "stdint.h"

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
const terminal_info_t *terminal_info(void);

#endif
