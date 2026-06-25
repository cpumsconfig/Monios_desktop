#ifndef _GUI_H_
#define _GUI_H_

#include "stdbool.h"
#include "stdint.h"

typedef struct {
    bool initialized;
    bool wm_ready;
    bool app_framework_ready;
    uint32_t widgets_registered;
    uint32_t windows;
    uint32_t focused;
    char status[64];
} gui_info_t;

void gui_init(void);
void gui_refresh(void);
const gui_info_t *gui_info(void);
const char *gui_status(void);

#endif
