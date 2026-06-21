#include "common.h"
#include "console.h"
#include "device.h"
#include "graphics.h"
#include "keyboard.h"
#include "kernel.h"
#include "net.h"
#include "smp.h"
#include "terminal.h"

typedef struct {
    const char *name;
    const char *kind;
    bool readable;
    bool writable;
} device_entry_t;

static const device_entry_t g_devices[] = {
    { "console", "char", true, true },
    { "null", "char", true, true },
    { "zero", "char", true, false },
    { "kbd", "char", true, false },
    { "net", "net", true, false },
    { "smp", "cpu", true, false },
    { "term", "tty", true, false },
    { "wm", "gui", true, false },
    { "fb", "graphics", true, false }
};

static const char *device_normalize(const char *name)
{
    if (name == NULL) {
        return NULL;
    }
    if (name[0] == '\\' && name[1] == '\\' && name[2] == '.' && name[3] == '\\') {
        return name + 4;
    }
    if (name[0] == '/' && name[1] == 'd' && name[2] == 'e' && name[3] == 'v' && name[4] == '/') {
        return name + 5;
    }
    return name;
}

static const device_entry_t *device_find(const char *name)
{
    const char *normalized = device_normalize(name);

    if (normalized == NULL) {
        return NULL;
    }
    for (uint32_t i = 0; i < sizeof(g_devices) / sizeof(g_devices[0]); i++) {
        if (strcmp(normalized, g_devices[i].name) == 0) {
            return &g_devices[i];
        }
    }
    return NULL;
}

static void device_append(char *buffer, uint32_t size, const char *text)
{
    uint32_t len;
    uint32_t add;

    if (buffer == NULL || size == 0 || text == NULL) {
        return;
    }
    len = (uint32_t) strlen(buffer);
    add = (uint32_t) strlen(text);
    if (len + add + 1 >= size) {
        return;
    }
    strcpy(buffer + len, text);
}

static void device_append_u32(char *buffer, uint32_t size, uint32_t value)
{
    char temp[10];
    char out[11];
    uint32_t i = 0;
    uint32_t j = 0;

    if (value == 0) {
        device_append(buffer, size, "0");
        return;
    }
    while (value > 0 && i < sizeof(temp)) {
        temp[i++] = (char) ('0' + (value % 10));
        value /= 10;
    }
    while (i > 0) {
        out[j++] = temp[--i];
    }
    out[j] = '\0';
    device_append(buffer, size, out);
}

void device_init(void)
{
    log_write("device: \\\\.\\ namespace ready");
}

bool device_exists(const char *name)
{
    return device_find(name) != NULL;
}

bool device_list(char *buffer, uint32_t size)
{
    if (buffer == NULL || size == 0) {
        return false;
    }
    buffer[0] = '\0';
    for (uint32_t i = 0; i < sizeof(g_devices) / sizeof(g_devices[0]); i++) {
        device_append(buffer, size, "\\\\.\\");
        device_append(buffer, size, g_devices[i].name);
        device_append(buffer, size, " ");
        device_append(buffer, size, g_devices[i].kind);
        device_append(buffer, size, g_devices[i].readable ? " r" : " -");
        device_append(buffer, size, g_devices[i].writable ? "w\n" : "-\n");
    }
    return true;
}

int32_t device_read(const char *name, char *buffer, uint32_t size)
{
    const device_entry_t *dev = device_find(name);

    if (dev == NULL || !dev->readable || buffer == NULL || size == 0) {
        return -1;
    }
    buffer[0] = '\0';
    if (strcmp(dev->name, "null") == 0) {
        return 0;
    }
    if (strcmp(dev->name, "zero") == 0) {
        memset(buffer, 0, size);
        return (int32_t) size;
    }
    if (strcmp(dev->name, "kbd") == 0) {
        char ch;
        if (keyboard_read_char(&ch)) {
            buffer[0] = ch;
            return 1;
        }
        return 0;
    }
    if (strcmp(dev->name, "net") == 0) {
        const net_info_t *info = net_info();
        device_append(buffer, size, net_status());
        device_append(buffer, size, "\nmac ");
        device_append(buffer, size, info->mac_text);
        device_append(buffer, size, "\nip ");
        device_append(buffer, size, info->ip_text);
        device_append(buffer, size, "\ngateway ");
        device_append(buffer, size, info->gateway_text);
        device_append(buffer, size, "\n");
        return (int32_t) strlen(buffer);
    }
    if (strcmp(dev->name, "smp") == 0) {
        const smp_info_t *info = smp_info();

        device_append(buffer, size, info->status);
        device_append(buffer, size, "\nlogical ");
        device_append_u32(buffer, size, info->logical_processors);
        device_append(buffer, size, "\nonline ");
        device_append_u32(buffer, size, info->online_processors);
        device_append(buffer, size, "\nmode ");
        device_append(buffer, size, info->bootstrap_only ? "bsp-only\n" : "ap-scheduler\n");
        return (int32_t) strlen(buffer);
    }
    if (strcmp(dev->name, "term") == 0) {
        const terminal_info_t *term = terminal_info();

        device_append(buffer, size, term->active ? "terminal active\n" : "terminal inactive\n");
        device_append(buffer, size, term->focused ? "focus yes\n" : "focus no\n");
        device_append(buffer, size, "mode ");
        device_append(buffer, size, term->mode);
        device_append(buffer, size, "\nlines ");
        device_append_u32(buffer, size, term->lines_written);
        device_append(buffer, size, "\n");
        return (int32_t) strlen(buffer);
    }
    if (strcmp(dev->name, "wm") == 0) {
        device_append(buffer, size, "windows ");
        device_append_u32(buffer, size, graphics_window_count());
        device_append(buffer, size, "\nfocused ");
        device_append_u32(buffer, size, graphics_focused_window_index());
        device_append(buffer, size, "\n");
        return (int32_t) strlen(buffer);
    }
    if (strcmp(dev->name, "fb") == 0) {
        device_append(buffer, size, graphics_active() ? "framebuffer active\n" : "framebuffer inactive\n");
        return (int32_t) strlen(buffer);
    }
    if (strcmp(dev->name, "console") == 0) {
        device_append(buffer, size, "console\n");
        return (int32_t) strlen(buffer);
    }
    return -1;
}

int32_t device_write(const char *name, const char *buffer, uint32_t size)
{
    const device_entry_t *dev = device_find(name);

    if (dev == NULL || !dev->writable || buffer == NULL) {
        return -1;
    }
    if (strcmp(dev->name, "null") == 0) {
        return (int32_t) size;
    }
    if (strcmp(dev->name, "console") == 0) {
        for (uint32_t i = 0; i < size; i++) {
            console_write_char(buffer[i]);
        }
        return (int32_t) size;
    }
    return -1;
}
