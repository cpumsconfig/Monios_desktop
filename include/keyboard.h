#ifndef _KEYBOARD_H_
#define _KEYBOARD_H_

#include "stdbool.h"
#include "stdint.h"

typedef struct {
    bool shift_down;
    bool ctrl_down;
    bool alt_down;
    bool win_down;
    bool caps_lock_on;
    bool num_lock_on;
    bool insert_mode;
    uint8_t last_function;
} keyboard_status_t;

typedef enum {
    KEY_EVENT_NONE = 0,
    KEY_EVENT_CHAR,
    KEY_EVENT_UP,
    KEY_EVENT_DOWN,
    KEY_EVENT_LEFT,
    KEY_EVENT_RIGHT,
    KEY_EVENT_F1,
    KEY_EVENT_F2,
    KEY_EVENT_F3,
    KEY_EVENT_F4,
    KEY_EVENT_F5,
    KEY_EVENT_F6,
    KEY_EVENT_F7,
    KEY_EVENT_F8,
    KEY_EVENT_F9,
    KEY_EVENT_F10,
    KEY_EVENT_F11,
    KEY_EVENT_F12,
    KEY_EVENT_TAB,
    KEY_EVENT_CTRL,
    KEY_EVENT_ALT,
    KEY_EVENT_WIN,
    KEY_EVENT_MENU,
    KEY_EVENT_NUM,
    KEY_EVENT_PAUSE,
    KEY_EVENT_CTRL_C,
    KEY_EVENT_HOME,
    KEY_EVENT_END,
    KEY_EVENT_DELETE,
    KEY_EVENT_ESC,
    KEY_EVENT_POWER
} key_event_type_t;

typedef struct {
    key_event_type_t type;
    char ch;
    keyboard_status_t status;
} key_event_t;

void init_keyboard(void);
void keyboard_interrupt_dispatch(void);
const keyboard_status_t *keyboard_status(void);
bool keyboard_poll_event(key_event_t *event_out);
bool keyboard_read_char(char *ch_out);

#endif
