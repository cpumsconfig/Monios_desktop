#ifndef _INPUT_H_
#define _INPUT_H_

#include "stdbool.h"

typedef struct {
    bool keyboard_ready;
    bool mouse_ready;
    bool usb_legacy_compat;
} input_status_t;

void init_input(void);
const input_status_t *input_status(void);

#endif
