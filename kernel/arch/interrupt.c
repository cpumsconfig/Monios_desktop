#include "common.h"
#include "bsod.h"
#include "crash_dump.h"
#include "exec.h"
#include "ftrace.h"
#include "graphics.h"
#include "interrupt.h"
#include "keyboard.h"
#include "kernel.h"
#include "mmu.h"
#include "mouse.h"

#define PIC1_COMMAND 0x20
#define PIC1_DATA    0x21
#define PIC2_COMMAND 0xA0
#define PIC2_DATA    0xA1
#define PIC_EOI      0x20

#define PIT_COMMAND  0x43
#define PIT_CHANNEL0 0x40
#define PIT_FREQUENCY 1193182U

#define IRQ0_VECTOR 32
#define IRQ1_VECTOR 33
#define IRQ2_VECTOR 34
#define IRQ3_VECTOR 35
#define IRQ4_VECTOR 36
#define IRQ5_VECTOR 37
#define IRQ6_VECTOR 38
#define IRQ7_VECTOR 39
#define IRQ8_VECTOR 40
#define IRQ9_VECTOR 41
#define IRQ10_VECTOR 42
#define IRQ11_VECTOR 43
#define IRQ12_VECTOR 44
#define IRQ13_VECTOR 45
#define IRQ14_VECTOR 46
#define IRQ15_VECTOR 47

#define IRQ_COUNT 16
#define IRQ_SHARED_HANDLERS 4

extern void irq0_interrupt_handler(void);
extern void irq1_interrupt_handler(void);
extern void irq2_interrupt_handler(void);
extern void irq3_interrupt_handler(void);
extern void irq4_interrupt_handler(void);
extern void irq5_interrupt_handler(void);
extern void irq6_interrupt_handler(void);
extern void irq7_interrupt_handler(void);
extern void irq8_interrupt_handler(void);
extern void irq9_interrupt_handler(void);
extern void irq10_interrupt_handler(void);
extern void irq11_interrupt_handler(void);
extern void irq12_interrupt_handler(void);
extern void irq13_interrupt_handler(void);
extern void irq14_interrupt_handler(void);
extern void irq15_interrupt_handler(void);
extern void exception3_handler(void);

typedef struct {
    uint64_t vector;
    uint64_t error_code;
    uint64_t rip;
    uint64_t cs;
    uint64_t rflags;
    uint64_t rsp;
    uint64_t ss;
} cpu_exception_frame_t;

static volatile uint64_t tick_count;
static volatile bool tick_paused;
static uint32_t tick_rate_hz;
static irq_handler_t irq_handlers[IRQ_COUNT][IRQ_SHARED_HANDLERS];
static void *irq_handler_ctx[IRQ_COUNT][IRQ_SHARED_HANDLERS];
static uint8_t pic_master_mask;
static uint8_t pic_slave_mask;

static void pic_send_eoi(uint8_t irq)
{
    if (irq >= 8) {
        outb(PIC2_COMMAND, PIC_EOI);
    }
    outb(PIC1_COMMAND, PIC_EOI);
}

static void pic_remap(uint8_t offset1, uint8_t offset2)
{
    uint8_t mask1 = inb(PIC1_DATA);
    uint8_t mask2 = inb(PIC2_DATA);

    outb(PIC1_COMMAND, 0x11);
    io_wait();
    outb(PIC2_COMMAND, 0x11);
    io_wait();
    outb(PIC1_DATA, offset1);
    io_wait();
    outb(PIC2_DATA, offset2);
    io_wait();
    outb(PIC1_DATA, 0x04);
    io_wait();
    outb(PIC2_DATA, 0x02);
    io_wait();
    outb(PIC1_DATA, 0x01);
    io_wait();
    outb(PIC2_DATA, 0x01);
    io_wait();

    outb(PIC1_DATA, mask1);
    outb(PIC2_DATA, mask2);
}

static void pic_set_mask(uint8_t master_mask, uint8_t slave_mask)
{
    pic_master_mask = master_mask;
    pic_slave_mask = slave_mask;
    outb(PIC1_DATA, master_mask);
    outb(PIC2_DATA, slave_mask);
}

void interrupt_set_irq_enabled(uint8_t irq, bool enabled)
{
    uint8_t bit;

    if (irq >= IRQ_COUNT) {
        return;
    }
    if (irq < 8) {
        bit = (uint8_t) (1u << irq);
        if (enabled) {
            pic_master_mask &= (uint8_t) ~bit;
        } else {
            pic_master_mask |= bit;
        }
        outb(PIC1_DATA, pic_master_mask);
    } else {
        bit = (uint8_t) (1u << (irq - 8u));
        if (enabled) {
            pic_slave_mask &= (uint8_t) ~bit;
            pic_master_mask &= (uint8_t) ~(1u << 2);
        } else {
            pic_slave_mask |= bit;
        }
        outb(PIC1_DATA, pic_master_mask);
        outb(PIC2_DATA, pic_slave_mask);
    }
}

static void pit_init(uint32_t hz)
{
    uint32_t divisor = PIT_FREQUENCY / hz;

    outb(PIT_COMMAND, 0x36);
    outb(PIT_CHANNEL0, (uint8_t) (divisor & 0xFF));
    outb(PIT_CHANNEL0, (uint8_t) ((divisor >> 8) & 0xFF));
}

uint64_t cpu_exception_dispatch(cpu_exception_frame_t *frame)
{
    bsod_exception_info_t info;
    bool from_user;

    if (frame == NULL) {
        bsod_exception_panic(NULL);
        return 0;
    }

    info.vector = (uint8_t) frame->vector;
    info.error_code = frame->error_code;
    info.rip = frame->rip;
    info.cs = frame->cs;
    from_user = (frame->cs & 3) == 3;
    info.rflags = frame->rflags;
    info.rsp = from_user ? frame->rsp : 0;
    info.ss = from_user ? frame->ss : 0;

    /* ── Early capture: record this exception in ftrace ────────
     * Treat the exception vector as a synthetic function call.
     * func=vector, parent=RIP so we can see the faulting location
     * in the function graph even if the system crashes immediately.
     */
    if (ftrace_is_enabled()) {
        uint32_t func_rel = (uint32_t)((uint64_t)info.vector & 0xFFFFFFFFu);
        uint32_t rip_rel  = (uint32_t)((uint64_t)info.rip  & 0xFFFFFFFFu);
        ftrace_record_entry(func_rel, rip_rel);
    }

    /* Capture CPU state to crash dump buffer before any further
     * processing.  This is the same state that GDB would read,
     * and it survives a BSOD bluescreen render. */
    crash_dump_capture("exception",
        (uint64_t)info.vector, (uint64_t)info.error_code,
        (uint64_t)info.rip, (uint64_t)frame->rsp, 0 /* rbp n/a in frame */,
        (uint64_t)info.rflags);

    if (from_user && exec_active()) {
        char msg[64] = "process exception vector=0x";
        static const char hex[] = "0123456789ABCDEF";

        msg[27] = hex[(info.vector >> 4) & 0xF];
        msg[28] = hex[info.vector & 0xF];
        msg[29] = '\0';
        log_write(msg);
        exec_abort_from_exception(info.vector, info.error_code);
        return 1;
    }

    /* ftrace_dump_serial() + crash_dump_flush_serial() are called
     * inside bsod_exception_panic(). */
    bsod_exception_panic(&info);
    return 0;
}

/* Exception vector 3 = breakpoint (INT3 / GDB stub) */
#define EXC3_VECTOR 3

void init_interrupts(uint32_t timer_hz)
{
    tick_count = 0;
    tick_paused = false;
    tick_rate_hz = timer_hz;
    memset(irq_handlers, 0, sizeof(irq_handlers));
    memset(irq_handler_ctx, 0, sizeof(irq_handler_ctx));

    idt_set_handler(EXC3_VECTOR, (uint64_t) exception3_handler, 0x8E);
    idt_set_handler(IRQ0_VECTOR, (uint64_t) irq0_interrupt_handler, 0x8E);
    idt_set_handler(IRQ1_VECTOR, (uint64_t) irq1_interrupt_handler, 0x8E);
    idt_set_handler(IRQ2_VECTOR, (uint64_t) irq2_interrupt_handler, 0x8E);
    idt_set_handler(IRQ3_VECTOR, (uint64_t) irq3_interrupt_handler, 0x8E);
    idt_set_handler(IRQ4_VECTOR, (uint64_t) irq4_interrupt_handler, 0x8E);
    idt_set_handler(IRQ5_VECTOR, (uint64_t) irq5_interrupt_handler, 0x8E);
    idt_set_handler(IRQ6_VECTOR, (uint64_t) irq6_interrupt_handler, 0x8E);
    idt_set_handler(IRQ7_VECTOR, (uint64_t) irq7_interrupt_handler, 0x8E);
    idt_set_handler(IRQ8_VECTOR, (uint64_t) irq8_interrupt_handler, 0x8E);
    idt_set_handler(IRQ9_VECTOR, (uint64_t) irq9_interrupt_handler, 0x8E);
    idt_set_handler(IRQ10_VECTOR, (uint64_t) irq10_interrupt_handler, 0x8E);
    idt_set_handler(IRQ11_VECTOR, (uint64_t) irq11_interrupt_handler, 0x8E);
    idt_set_handler(IRQ12_VECTOR, (uint64_t) irq12_interrupt_handler, 0x8E);
    idt_set_handler(IRQ13_VECTOR, (uint64_t) irq13_interrupt_handler, 0x8E);
    idt_set_handler(IRQ14_VECTOR, (uint64_t) irq14_interrupt_handler, 0x8E);
    idt_set_handler(IRQ15_VECTOR, (uint64_t) irq15_interrupt_handler, 0x8E);
    pic_remap(IRQ0_VECTOR, 40);
    pic_set_mask(0xF8, 0xEF);
    pit_init(timer_hz);
}

bool interrupt_register_irq_handler(uint8_t irq, irq_handler_t handler, void *ctx)
{
    if (irq >= IRQ_COUNT || handler == NULL) {
        return false;
    }
    for (uint8_t i = 0; i < IRQ_SHARED_HANDLERS; i++) {
        if (irq_handlers[irq][i] == NULL) {
            irq_handlers[irq][i] = handler;
            irq_handler_ctx[irq][i] = ctx;
            interrupt_set_irq_enabled(irq, true);
            return true;
        }
    }
    return false;
}

void timer_interrupt_dispatch(void)
{
    pic_send_eoi(0);
    if (!tick_paused) {
        tick_count++;
    }
    if (exec_active()) {
        kernel_run_periodic_work();
    }
}

uint64_t timer_ticks(void)
{
    return tick_count;
}

uint32_t timer_hz(void)
{
    return tick_rate_hz;
}

bool timer_paused(void)
{
    return tick_paused;
}

void timer_toggle_paused(void)
{
    tick_paused = !tick_paused;
}

void keyboard_interrupt_dispatch_wrapper(void)
{
    keyboard_interrupt_dispatch();
    pic_send_eoi(1);
}

void mouse_interrupt_dispatch_wrapper(void)
{
    mouse_interrupt_dispatch();
    pic_send_eoi(12);
}

void generic_irq_interrupt_dispatch(uint8_t irq)
{
    if (irq < IRQ_COUNT) {
        for (uint8_t i = 0; i < IRQ_SHARED_HANDLERS; i++) {
            if (irq_handlers[irq][i] != NULL) {
                (void) irq_handlers[irq][i](irq, irq_handler_ctx[irq][i]);
            }
        }
    }
    pic_send_eoi(irq);
}
