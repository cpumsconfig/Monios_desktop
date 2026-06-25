#include "bsod.h"
#include "common.h"
#include "exec.h"
#include "graphics.h"
#include "kernel.h"
#include "crash_dump.h"
#include "ftrace.h"

#define VGA_WIDTH 80
#define VGA_HEIGHT 25
#define BSOD_ATTR 0x1F
#define VGA_TEXT_BUFFER ((volatile uint16_t *) 0xB8000)
#define BSOD_REBOOT_COMMAND 0xFE

static void bsod_clear(void)
{
    for (uint16_t row = 0; row < VGA_HEIGHT; row++) {
        for (uint16_t col = 0; col < VGA_WIDTH; col++) {
            VGA_TEXT_BUFFER[row * VGA_WIDTH + col] = ((uint16_t) BSOD_ATTR << 8) | ' ';
        }
    }
}

static void bsod_write_at(uint16_t row, uint16_t col, const char *str)
{
    while (*str && row < VGA_HEIGHT && col < VGA_WIDTH) {
        VGA_TEXT_BUFFER[row * VGA_WIDTH + col] = ((uint16_t) BSOD_ATTR << 8) | (uint8_t) *str++;
        col++;
    }
}

static void u64_to_dec(char *buf, uint64_t value)
{
    char temp[21];
    uint32_t i = 0;
    uint32_t j = 0;

    if (value == 0) {
        buf[0] = '0';
        buf[1] = '\0';
        return;
    }

    while (value > 0) {
        temp[i++] = (char) ('0' + (value % 10));
        value /= 10;
    }
    while (i > 0) {
        buf[j++] = temp[--i];
    }
    buf[j] = '\0';
}

static void u64_to_hex16(char out[17], uint64_t value)
{
    static const char hex[] = "0123456789ABCDEF";

    for (uint8_t i = 0; i < 16; i++) {
        out[i] = hex[(value >> ((15 - i) * 4)) & 0xF];
    }
    out[16] = '\0';
}

static const char *bsod_exception_text(uint8_t vector)
{
    switch (vector) {
    case 0x00: return "divide error";
    case 0x01: return "debug exception";
    case 0x02: return "non-maskable interrupt";
    case 0x03: return "breakpoint";
    case 0x04: return "overflow";
    case 0x05: return "bound range exceeded";
    case 0x06: return "invalid opcode";
    case 0x07: return "device not available";
    case 0x08: return "double fault";
    case 0x0A: return "invalid tss";
    case 0x0B: return "segment not present";
    case 0x0C: return "stack fault";
    case 0x0D: return "general protection fault";
    case 0x0E: return "page fault";
    case 0x10: return "x87 floating point fault";
    case 0x11: return "alignment check";
    case 0x12: return "machine check";
    case 0x13: return "simd floating point fault";
    case 0x14: return "virtualization fault";
    case 0x15: return "control protection fault";
    default: return "unhandled processor exception";
    }
}

static const char *bsod_current_process_name(void)
{
    const exec_launch_info_t *info = exec_current_launch_info();

    if (info != NULL && info->program_path != NULL && info->program_path[0] != '\0') {
        return info->program_path;
    }
    return "kernel";
}

static void bsod_delay(void)
{
    for (volatile uint32_t i = 0; i < 12000000; i++) {
    }
}

static void bsod_reboot(void)
{
    outb(0x64, BSOD_REBOOT_COMMAND);
    for (;;) {
        asm volatile ("hlt");
    }
}

static void bsod_collect_and_reboot(const char *process, const char *code, const char *text)
{
    for (uint32_t progress = 0; progress <= 100; progress += 10) {
        if (graphics_active()) {
            graphics_draw_bsod(process, code, text, progress);
        }
        bsod_delay();
    }

    serial_write("MONIOS DEBUG ERROR\r\n");
    serial_write("process:");
    serial_write(process);
    serial_write("\r\nERROR CODE:");
    serial_write(code);
    serial_write("\r\nERROR TEXT:");
    serial_write(text);
    serial_write("\r\nCollection process:100%\r\n");
    serial_write("rebooting\r\n");
    bsod_reboot();
}

void bsod_panic(const char *title, const char *detail)
{
    char code[17];
    const char *process = bsod_current_process_name();
    const char *text = detail != NULL ? detail : title;

    asm volatile ("cli");

    u64_to_hex16(code, 0xB500000000000001ULL);
    if (graphics_active()) {
        graphics_draw_bsod(process, code, text, 0);
    } else {
        bsod_clear();
        bsod_write_at(2, 2, "MONIOS DEBUG ERROR");
        bsod_write_at(4, 2, "process:");
        bsod_write_at(4, 10, process);
        bsod_write_at(6, 2, "ERROR CODE:");
        bsod_write_at(6, 13, code);
        bsod_write_at(8, 2, "ERROR TEXT:");
        bsod_write_at(8, 13, text);
    }
    serial_write("BSOD: ");
    serial_write(title);
    serial_write("\r\n");
    if (detail != NULL) {
        serial_write(detail);
        serial_write("\r\n");
    }
    {
        uint64_t cr2; asm volatile ("mov %%cr2, %0" : "=r"(cr2));
        crash_dump_capture(process, 0, 0, (uint64_t)detail, 0, 0, 0);
        ftrace_dump_serial();
        crash_dump_flush_serial();
        crash_dump_write_disk();
    }
    bsod_collect_and_reboot(process, code, text);
}

void bsod_unhandled_interrupt(void)
{
    bsod_panic("UNHANDLED INTERRUPT", "The kernel trapped into the default IDT handler.");
}

void bsod_exception_panic(const bsod_exception_info_t *info)
{
    char code[17];
    uint64_t packed_code = 0xE000000000000000ULL;
    const char *process = bsod_current_process_name();
    const char *text;

    asm volatile ("cli");

    if (info != NULL) {
        packed_code |= ((uint64_t) info->vector << 48);
        packed_code |= (info->error_code & 0x0000FFFFFFFFFFFFULL);
        text = bsod_exception_text(info->vector);
    } else {
        text = "processor exception";
    }
    u64_to_hex16(code, packed_code);

    if (graphics_active()) {
        graphics_draw_bsod(process, code, text, 0);
    } else {
        bsod_clear();
        bsod_write_at(2, 2, "MONIOS DEBUG ERROR");
        bsod_write_at(4, 2, "process:");
        bsod_write_at(4, 10, process);
        bsod_write_at(6, 2, "ERROR CODE:");
        bsod_write_at(6, 13, code);
        bsod_write_at(8, 2, "ERROR TEXT:");
        bsod_write_at(8, 13, text);
    }

    serial_write("BSOD: processor exception\r\n");
    serial_write("process:");
    serial_write(process);
    serial_write("\r\nERROR CODE:");
    serial_write(code);
    serial_write("\r\nERROR TEXT:");
    serial_write(text);
    serial_write("\r\n");
    ftrace_dump_serial();
    crash_dump_flush_serial();
    crash_dump_write_disk();
    bsod_collect_and_reboot(process, code, text);
}

void bsod_out_of_memory(uint64_t size)
{
    char msg[40] = "Allocation failed: ";
    char digits[21];
    uint32_t i = 19;

    u64_to_dec(digits, size);
    while (digits[i - 19] != '\0') {
        msg[i] = digits[i - 19];
        i++;
    }
    msg[i] = '\0';
    bsod_panic("OUT OF MEMORY", msg);
}
