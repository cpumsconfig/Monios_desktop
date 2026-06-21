#include "common.h"
#include "interrupt.h"
#include "keyboard.h"

#define PS2_DATA_PORT 0x60
#define PS2_STATUS_PORT 0x64
#define PS2_STATUS_OUTPUT_FULL 0x01

static bool extended_prefix;
static bool pause_prefix_e1;
static uint8_t pause_sequence_index;
static keyboard_status_t kb_status;

#define KEYBOARD_EVENT_QUEUE_SIZE 64

static volatile key_event_t g_key_events[KEYBOARD_EVENT_QUEUE_SIZE];
static volatile uint32_t g_key_event_head;
static volatile uint32_t g_key_event_tail;

static const char keymap_base[128] = {
    [0x02] = '1', [0x03] = '2', [0x04] = '3', [0x05] = '4',
    [0x06] = '5', [0x07] = '6', [0x08] = '7', [0x09] = '8',
    [0x0A] = '9', [0x0B] = '0', [0x0C] = '-', [0x0D] = '=',
    [0x0E] = '\b', [0x0F] = '\t',
    [0x10] = 'q', [0x11] = 'w', [0x12] = 'e', [0x13] = 'r',
    [0x14] = 't', [0x15] = 'y', [0x16] = 'u', [0x17] = 'i',
    [0x18] = 'o', [0x19] = 'p', [0x1A] = '[', [0x1B] = ']',
    [0x1C] = '\n',
    [0x1E] = 'a', [0x1F] = 's', [0x20] = 'd', [0x21] = 'f',
    [0x22] = 'g', [0x23] = 'h', [0x24] = 'j', [0x25] = 'k',
    [0x26] = 'l', [0x27] = ';', [0x28] = '\'', [0x29] = '`',
    [0x2B] = '\\',
    [0x2C] = 'z', [0x2D] = 'x', [0x2E] = 'c', [0x2F] = 'v',
    [0x30] = 'b', [0x31] = 'n', [0x32] = 'm', [0x33] = ',',
    [0x34] = '.', [0x35] = '/', [0x39] = ' '
};

static const char keymap_shift[128] = {
    [0x02] = '!', [0x03] = '@', [0x04] = '#', [0x05] = '$',
    [0x06] = '%', [0x07] = '^', [0x08] = '&', [0x09] = '*',
    [0x0A] = '(', [0x0B] = ')', [0x0C] = '_', [0x0D] = '+',
    [0x0E] = '\b', [0x0F] = '\t',
    [0x10] = 'Q', [0x11] = 'W', [0x12] = 'E', [0x13] = 'R',
    [0x14] = 'T', [0x15] = 'Y', [0x16] = 'U', [0x17] = 'I',
    [0x18] = 'O', [0x19] = 'P', [0x1A] = '{', [0x1B] = '}',
    [0x1C] = '\n',
    [0x1E] = 'A', [0x1F] = 'S', [0x20] = 'D', [0x21] = 'F',
    [0x22] = 'G', [0x23] = 'H', [0x24] = 'J', [0x25] = 'K',
    [0x26] = 'L', [0x27] = ':', [0x28] = '"', [0x29] = '~',
    [0x2B] = '|',
    [0x2C] = 'Z', [0x2D] = 'X', [0x2E] = 'C', [0x2F] = 'V',
    [0x30] = 'B', [0x31] = 'N', [0x32] = 'M', [0x33] = '<',
    [0x34] = '>', [0x35] = '?', [0x39] = ' '
};

static char translate_scancode(uint8_t scancode)
{
    char ch = kb_status.shift_down ? keymap_shift[scancode] : keymap_base[scancode];

    if (ch >= 'a' && ch <= 'z' && kb_status.caps_lock_on) {
        ch = (char) (ch - 'a' + 'A');
    } else if (ch >= 'A' && ch <= 'Z' && kb_status.caps_lock_on && kb_status.shift_down) {
        ch = (char) (ch - 'A' + 'a');
    }
    return ch;
}

static void keyboard_emit_event(key_event_type_t type, char ch)
{
    key_event_t event;
    uint32_t head;
    uint32_t next;

    event.type = type;
    event.ch = ch;
    event.status = kb_status;

    head = g_key_event_head;
    next = (head + 1) % KEYBOARD_EVENT_QUEUE_SIZE;
    if (next == g_key_event_tail) {
        g_key_event_tail = (g_key_event_tail + 1) % KEYBOARD_EVENT_QUEUE_SIZE;
    }

    g_key_events[head] = event;
    g_key_event_head = next;
}

static void keyboard_set_function(uint8_t fn)
{
    kb_status.last_function = fn;
    keyboard_emit_event((key_event_type_t) (KEY_EVENT_F1 + fn - 1), 0);
}

static bool keyboard_handle_function_key(uint8_t scancode)
{
    switch (scancode) {
    case 0x3B: keyboard_set_function(1); return true;
    case 0x3C: keyboard_set_function(2); return true;
    case 0x3D: keyboard_set_function(3); return true;
    case 0x3E: keyboard_set_function(4); return true;
    case 0x3F: keyboard_set_function(5); return true;
    case 0x40: keyboard_set_function(6); return true;
    case 0x41: keyboard_set_function(7); return true;
    case 0x42: keyboard_set_function(8); return true;
    case 0x43: keyboard_set_function(9); return true;
    case 0x44: keyboard_set_function(10); return true;
    case 0x57: keyboard_set_function(11); return true;
    case 0x58: keyboard_set_function(12); return true;
    default:
        return false;
    }
}

static bool keyboard_handle_keypad(uint8_t scancode)
{
    switch (scancode) {
    case 0x37: keyboard_emit_event(KEY_EVENT_CHAR, '*'); return true;
    case 0x4A: keyboard_emit_event(KEY_EVENT_CHAR, '-'); return true;
    case 0x4C: if (kb_status.num_lock_on) keyboard_emit_event(KEY_EVENT_CHAR, '5'); return true;
    case 0x4E: keyboard_emit_event(KEY_EVENT_CHAR, '+'); return true;
    case 0x47: if (kb_status.num_lock_on) keyboard_emit_event(KEY_EVENT_CHAR, '7'); return true;
    case 0x48: if (kb_status.num_lock_on) keyboard_emit_event(KEY_EVENT_CHAR, '8'); else keyboard_emit_event(KEY_EVENT_UP, 0); return true;
    case 0x49: if (kb_status.num_lock_on) keyboard_emit_event(KEY_EVENT_CHAR, '9'); return true;
    case 0x4B: if (kb_status.num_lock_on) keyboard_emit_event(KEY_EVENT_CHAR, '4'); else keyboard_emit_event(KEY_EVENT_LEFT, 0); return true;
    case 0x4D: if (kb_status.num_lock_on) keyboard_emit_event(KEY_EVENT_CHAR, '6'); else keyboard_emit_event(KEY_EVENT_RIGHT, 0); return true;
    case 0x4F: if (kb_status.num_lock_on) keyboard_emit_event(KEY_EVENT_CHAR, '1'); return true;
    case 0x50: if (kb_status.num_lock_on) keyboard_emit_event(KEY_EVENT_CHAR, '2'); else keyboard_emit_event(KEY_EVENT_DOWN, 0); return true;
    case 0x51: if (kb_status.num_lock_on) keyboard_emit_event(KEY_EVENT_CHAR, '3'); return true;
    case 0x52: if (kb_status.num_lock_on) keyboard_emit_event(KEY_EVENT_CHAR, '0'); return true;
    case 0x53: if (kb_status.num_lock_on) keyboard_emit_event(KEY_EVENT_CHAR, '.'); return true;
    default:
        return false;
    }
}

void init_keyboard(void)
{
    kb_status.shift_down = false;
    kb_status.ctrl_down = false;
    kb_status.alt_down = false;
    kb_status.win_down = false;
    kb_status.caps_lock_on = false;
    kb_status.num_lock_on = true;
    kb_status.insert_mode = false;
    kb_status.last_function = 0;
    extended_prefix = false;
    pause_prefix_e1 = false;
    pause_sequence_index = 0;
    g_key_event_head = 0;
    g_key_event_tail = 0;
}

const keyboard_status_t *keyboard_status(void)
{
    return &kb_status;
}

bool keyboard_poll_event(key_event_t *event_out)
{
    uint32_t tail;

    if (g_key_event_head == g_key_event_tail) {
        return false;
    }
    tail = g_key_event_tail;
    *event_out = g_key_events[tail];
    g_key_event_tail = (tail + 1) % KEYBOARD_EVENT_QUEUE_SIZE;
    return true;
}

bool keyboard_read_char(char *ch_out)
{
    key_event_t event;

    if (ch_out == NULL) {
        return false;
    }

    while (keyboard_poll_event(&event)) {
        if (event.type == KEY_EVENT_CHAR) {
            *ch_out = event.ch;
            return true;
        }
        if (event.type == KEY_EVENT_CTRL_C) {
            *ch_out = 3;
            return true;
        }
    }

    return false;
}

void keyboard_interrupt_dispatch(void)
{
    uint8_t scancode;
    bool release;
    char ch;

    if ((inb(PS2_STATUS_PORT) & PS2_STATUS_OUTPUT_FULL) == 0) {
        return;
    }

    scancode = inb(PS2_DATA_PORT);

    if (pause_prefix_e1) {
        pause_sequence_index++;
        if (pause_sequence_index >= 5) {
            pause_prefix_e1 = false;
            pause_sequence_index = 0;
            keyboard_emit_event(KEY_EVENT_PAUSE, 0);
        }
        return;
    }

    if (scancode == 0xE1) {
        pause_prefix_e1 = true;
        pause_sequence_index = 0;
        return;
    }

    if (scancode == 0xE0) {
        extended_prefix = true;
        return;
    }

    release = (scancode & 0x80u) != 0;
    scancode &= 0x7Fu;

    if (scancode == 0x2A || scancode == 0x36) {
        kb_status.shift_down = !release;
        extended_prefix = false;
        return;
    }

    if (!release && scancode == 0x3A) {
        kb_status.caps_lock_on = !kb_status.caps_lock_on;
        extended_prefix = false;
        return;
    }

    if (!release && scancode == 0x45) {
        kb_status.num_lock_on = !kb_status.num_lock_on;
        keyboard_emit_event(KEY_EVENT_NUM, 0);
        extended_prefix = false;
        return;
    }

    if (!extended_prefix && scancode == 0x1D) {
        kb_status.ctrl_down = !release;
        return;
    }

    if (!extended_prefix && scancode == 0x38) {
        kb_status.alt_down = !release;
        return;
    }

    if (release) {
        if (extended_prefix && scancode == 0x1D) {
            kb_status.ctrl_down = false;
        } else if (extended_prefix && scancode == 0x38) {
            kb_status.alt_down = false;
        } else if (extended_prefix && (scancode == 0x5B || scancode == 0x5C)) {
            kb_status.win_down = false;
        }
        extended_prefix = false;
        return;
    }

    if (extended_prefix) {
        if (scancode == 0x1D) {
            kb_status.ctrl_down = true;
            keyboard_emit_event(KEY_EVENT_CTRL, 0);
            extended_prefix = false;
            return;
        }
        if (scancode == 0x38) {
            kb_status.alt_down = true;
            keyboard_emit_event(KEY_EVENT_ALT, 0);
            extended_prefix = false;
            return;
        }
        if (scancode == 0x5B || scancode == 0x5C) {
            kb_status.win_down = true;
            keyboard_emit_event(KEY_EVENT_WIN, 0);
            extended_prefix = false;
            return;
        }
        if (scancode == 0x5D) {
            keyboard_emit_event(KEY_EVENT_MENU, 0);
            extended_prefix = false;
            return;
        }
        if (scancode == 0x5E) {
            keyboard_emit_event(KEY_EVENT_POWER, 0);
            extended_prefix = false;
            return;
        }

        switch (scancode) {
        case 0x1C: keyboard_emit_event(KEY_EVENT_CHAR, '\n'); break;
        case 0x35: keyboard_emit_event(KEY_EVENT_CHAR, '/'); break;
        case 0x47: keyboard_emit_event(KEY_EVENT_HOME, 0); break;
        case 0x48: keyboard_emit_event(KEY_EVENT_UP, 0); break;
        case 0x4B: keyboard_emit_event(KEY_EVENT_LEFT, 0); break;
        case 0x4D: keyboard_emit_event(KEY_EVENT_RIGHT, 0); break;
        case 0x4F: keyboard_emit_event(KEY_EVENT_END, 0); break;
        case 0x50: keyboard_emit_event(KEY_EVENT_DOWN, 0); break;
        case 0x53: keyboard_emit_event(KEY_EVENT_DELETE, 0); break;
        default: break;
        }
        extended_prefix = false;
        return;
    }

    if (keyboard_handle_function_key(scancode)) {
        return;
    }
    if (keyboard_handle_keypad(scancode)) {
        return;
    }
    if (scancode == 0x01) {
        keyboard_emit_event(KEY_EVENT_ESC, 0);
        return;
    }
    if (scancode == 0x0F) {
        keyboard_emit_event(KEY_EVENT_TAB, 0);
        return;
    }
    if (scancode == 0x1D) {
        kb_status.ctrl_down = true;
        keyboard_emit_event(KEY_EVENT_CTRL, 0);
        return;
    }
    if (scancode == 0x38) {
        kb_status.alt_down = true;
        keyboard_emit_event(KEY_EVENT_ALT, 0);
        return;
    }

    ch = translate_scancode(scancode);
    if (ch == '\0') {
        return;
    }
    if (kb_status.ctrl_down && (ch == 'c' || ch == 'C')) {
        keyboard_emit_event(KEY_EVENT_CTRL_C, ch);
        return;
    }
    keyboard_emit_event(KEY_EVENT_CHAR, ch);
}
