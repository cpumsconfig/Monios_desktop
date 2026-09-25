#include "common.h"
#include "interrupt.h"
#include "kernel.h"
#include "keyboard.h"

#define PS2_DATA_PORT 0x60
#define PS2_STATUS_PORT 0x64
#define PS2_STATUS_OUTPUT_FULL 0x01
#define PS2_STATUS_INPUT_FULL 0x02
#define PS2_KEYBOARD_CMD_LEDS 0xED
#define PS2_KEYBOARD_ACK 0xFA
#define KEYBOARD_WAIT_LIMIT 100000U

static bool extended_prefix;
static bool pause_prefix_e1;
static uint8_t pause_sequence_index;
static keyboard_status_t kb_status;
static bool g_kbd_present;
static bool g_ctrl_alt_del_pending;

#define PS2_KEYBOARD_CMD_RESET      0xFF
#define PS2_KEYBOARD_CMD_SCS        0xF0
#define PS2_KEYBOARD_CMD_ENABLE     0xF4
#define PS2_KEYBOARD_CMD_DISABLE    0xF5
#define PS2_KEYBOARD_ACK2           0xFA
#define PS2_KEYBOARD_BAT_OK         0xAA
#define PS2_WAIT_LONG               200000U

#define KEYBOARD_EVENT_QUEUE_SIZE 64

static volatile key_event_t g_key_events[KEYBOARD_EVENT_QUEUE_SIZE];
static volatile uint32_t g_key_event_head;
static volatile uint32_t g_key_event_tail;

static bool keyboard_wait_input_ready(void)
{
    for (uint32_t i = 0; i < KEYBOARD_WAIT_LIMIT; i++) {
        if ((inb(PS2_STATUS_PORT) & PS2_STATUS_INPUT_FULL) == 0) {
            return true;
        }
    }
    return false;
}

static bool keyboard_wait_output_ready(void)
{
    for (uint32_t i = 0; i < KEYBOARD_WAIT_LIMIT; i++) {
        if ((inb(PS2_STATUS_PORT) & PS2_STATUS_OUTPUT_FULL) != 0) {
            return true;
        }
    }
    return false;
}

static bool keyboard_write_data(uint8_t value)
{
    if (!keyboard_wait_input_ready()) {
        return false;
    }
    outb(PS2_DATA_PORT, value);
    return true;
}

static void keyboard_consume_ack(void)
{
    if (keyboard_wait_output_ready() && inb(PS2_DATA_PORT) != PS2_KEYBOARD_ACK) {
        /* Ignore non-ACK bytes here; the next hardware IRQ will resync. */
    }
}

static void keyboard_update_leds(void)
{
    uint8_t leds = (kb_status.scroll_lock_on ? 0x01U : 0U) |
                   (kb_status.num_lock_on ? 0x02U : 0U) |
                   (kb_status.caps_lock_on ? 0x04U : 0U);

    if (!keyboard_write_data(PS2_KEYBOARD_CMD_LEDS)) {
        return;
    }
    keyboard_consume_ack();
    if (!keyboard_write_data(leds)) {
        return;
    }
    keyboard_consume_ack();
}

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
    kb_status.scroll_lock_on = false;
    kb_status.insert_mode = false;
    kb_status.last_function = 0;
    extended_prefix = false;
    pause_prefix_e1 = false;
    pause_sequence_index = 0;
    g_key_event_head = 0;
    g_key_event_tail = 0;
    keyboard_update_leds();
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
        keyboard_update_leds();
        extended_prefix = false;
        return;
    }

    if (!release && scancode == 0x45) {
        kb_status.num_lock_on = !kb_status.num_lock_on;
        keyboard_update_leds();
        keyboard_emit_event(KEY_EVENT_NUM, 0);
        extended_prefix = false;
        return;
    }

    if (!release && scancode == 0x46) {
        kb_status.scroll_lock_on = !kb_status.scroll_lock_on;
        keyboard_update_leds();
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
        case 0x49: keyboard_emit_event(KEY_EVENT_PAGE_UP, 0); break;
        case 0x4B: keyboard_emit_event(KEY_EVENT_LEFT, 0); break;
        case 0x4D: keyboard_emit_event(KEY_EVENT_RIGHT, 0); break;
        case 0x4F: keyboard_emit_event(KEY_EVENT_END, 0); break;
        case 0x50: keyboard_emit_event(KEY_EVENT_DOWN, 0); break;
        case 0x51: keyboard_emit_event(KEY_EVENT_PAGE_DOWN, 0); break;
        case 0x53:
            if (kb_status.ctrl_down && kb_status.alt_down) {
                g_ctrl_alt_del_pending = true;
            }
            keyboard_emit_event(KEY_EVENT_DELETE, 0);
            break;
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

/* --- USB HID boot-protocol keyboard injection ----------------------- */
/* USB HID boot keyboard report (8 bytes):
 *   byte0   modifier bitmask (LCtrl=0x01 LShift=0x02 LAlt=0x04 LGui=0x08
 *                              RCtrl=0x10 RShift=0x20 RAlt=0x40 RGui=0x80)
 *   byte1   reserved (typically 0)
 *   byte2-7 up to 6 simultaneous HID usage IDs (0 = none)
 * Key-down edges are diffed against the previous report so we emit one
 * event per newly-pressed key.  Usage IDs 0x04..0x1D map to a..z,
 * 0x1E..0x26 to 1..9, 0x27 = 0, 0x2A = Enter, 0x29 = Esc, 0x28 = Tab,
 * 0x3A..0x45 = F1..F12. */
static const char usb_hid_keycode_map[][2] = {
    {0x04,'a'},{0x05,'b'},{0x06,'c'},{0x07,'d'},{0x08,'e'},{0x09,'f'},
    {0x0A,'g'},{0x0B,'h'},{0x0C,'i'},{0x0D,'j'},{0x0E,'k'},{0x0F,'l'},
    {0x10,'m'},{0x11,'n'},{0x12,'o'},{0x13,'p'},{0x14,'q'},{0x15,'r'},
    {0x16,'s'},{0x17,'t'},{0x18,'u'},{0x19,'v'},{0x1A,'w'},{0x1B,'x'},
    {0x1C,'y'},{0x1D,'z'},
    {0x1E,'1'},{0x1F,'2'},{0x20,'3'},{0x21,'4'},{0x22,'5'},{0x23,'6'},
    {0x24,'7'},{0x25,'8'},{0x26,'9'},{0x27,'0'},
    {0x2C,' '},{0x2A,'\n'},{0x2B,'\t'},{0x2A,'\b'},
};

static uint8_t g_usb_kbd_prev[6];

void keyboard_inject_usb_boot(const uint8_t *report, uint32_t len)
{
    uint8_t modifier;
    uint32_t i, j;
    bool down;
    char ch;
    key_event_type_t type;

    if (report == NULL || len < 8u) {
        return;
    }
    modifier = report[0];
    kb_status.shift_down = (modifier & 0x23u) != 0u;
    kb_status.ctrl_down  = (modifier & 0x11u) != 0u;
    kb_status.alt_down   = (modifier & 0x44u) != 0u;
    kb_status.win_down   = (modifier & 0x88u) != 0u;

    for (i = 0; i < 6u; i++) {
        uint8_t code = report[2u + i];
        down = false;
        for (j = 0; j < 6u; j++) {
            if (g_usb_kbd_prev[j] == code) { down = true; break; }
        }
        if (code == 0u || down) {
            continue;
        }
        /* Newly-pressed key: translate and emit. */
        type = KEY_EVENT_CHAR;
        ch = 0;
        if (code >= 0x3Au && code <= 0x45u) {
            type = (key_event_type_t)(KEY_EVENT_F1 + (code - 0x3Au));
        } else if (code == 0x29) {
            type = KEY_EVENT_ESC;
        } else {
            for (j = 0; j < (sizeof(usb_hid_keycode_map)/sizeof(usb_hid_keycode_map[0])); j++) {
                if (usb_hid_keycode_map[j][0] == code) {
                    ch = usb_hid_keycode_map[j][1];
                    if (kb_status.shift_down && ch >= 'a' && ch <= 'z') {
                        ch = (char)(ch - 'a' + 'A');
                    }
                    break;
                }
            }
        }
        keyboard_emit_event(type, ch);
    }
    for (i = 0; i < 6u; i++) {
        g_usb_kbd_prev[i] = report[2u + i];
    }
}

/* ------------------------------------------------------------------ */
/*  Probe / LED / scancode-set / public read-write interface           */
/* ------------------------------------------------------------------ */

static bool keyboard_read_data_timeout(uint8_t *out, uint32_t limit)
{
    for (uint32_t i = 0; i < limit; i++) {
        if ((inb(PS2_STATUS_PORT) & PS2_STATUS_OUTPUT_FULL) != 0) {
            *out = inb(PS2_DATA_PORT);
            return true;
        }
    }
    return false;
}

bool keyboard_probe(void)
{
    uint8_t reply;

    /* Drain any stale output. */
    while ((inb(PS2_STATUS_PORT) & PS2_STATUS_OUTPUT_FULL) != 0) {
        (void) inb(PS2_DATA_PORT);
    }

    if (!keyboard_write_data(PS2_KEYBOARD_CMD_RESET)) {
        log_write("keyboard: not found");
        return false;
    }
    if (!keyboard_read_data_timeout(&reply, PS2_WAIT_LONG) || reply != PS2_KEYBOARD_ACK2) {
        log_write("keyboard: not found (no ack)");
        return false;
    }
    if (!keyboard_read_data_timeout(&reply, PS2_WAIT_LONG) || reply != PS2_KEYBOARD_BAT_OK) {
        log_write("keyboard: not found (bat failed)");
        return false;
    }
    g_kbd_present = true;
    log_write("keyboard: ps/2 keyboard ready");
    return true;
}

bool keyboard_present(void)
{
    return g_kbd_present;
}

bool keyboard_set_scancode_set(uint8_t set)
{
    if (set > 3u) {
        return false;
    }
    if (!keyboard_write_data(PS2_KEYBOARD_CMD_SCS)) {
        return false;
    }
    keyboard_consume_ack();
    if (!keyboard_write_data(set)) {
        return false;
    }
    keyboard_consume_ack();
    return true;
}

bool keyboard_send_command(uint8_t command)
{
    if (!keyboard_write_data(command)) {
        return false;
    }
    keyboard_consume_ack();
    return true;
}

void keyboard_set_leds(bool caps_lock, bool num_lock, bool scroll_lock)
{
    kb_status.caps_lock_on = caps_lock;
    kb_status.num_lock_on = num_lock;
    kb_status.scroll_lock_on = scroll_lock;
    keyboard_update_leds();
}

/* Non-blocking read: copy buffered printable characters into buf.
 * Returns the number of characters produced (0 = none pending). */
int keyboard_read(char *buf, uint32_t max_len)
{
    uint32_t produced = 0;
    char ch;

    if (buf == NULL || max_len == 0) {
        return 0;
    }
    while (produced < max_len && keyboard_read_char(&ch)) {
        buf[produced++] = ch;
    }
    return (int) produced;
}

bool keyboard_ctrl_alt_del_pending(void)
{
    bool pending = g_ctrl_alt_del_pending;
    g_ctrl_alt_del_pending = false;
    return pending;
}

const char *keyboard_status_text(void)
{
    return g_kbd_present ? "keyboard: ready" : "keyboard: not found";
}
