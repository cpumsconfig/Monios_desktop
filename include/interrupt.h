#ifndef _INTERRUPT_H_
#define _INTERRUPT_H_

#include "stdbool.h"
#include "stdint.h"

typedef bool (*irq_handler_t)(uint8_t irq, void *ctx);

void init_interrupts(uint32_t timer_hz);
bool interrupt_register_irq_handler(uint8_t irq, irq_handler_t handler, void *ctx);
void interrupt_set_irq_enabled(uint8_t irq, bool enabled);
void timer_interrupt_dispatch(void);
uint64_t timer_ticks(void);
uint32_t timer_hz(void);
bool timer_paused(void);
void timer_toggle_paused(void);
void keyboard_interrupt_dispatch_wrapper(void);
void mouse_interrupt_dispatch_wrapper(void);
void generic_irq_interrupt_dispatch(uint8_t irq);

#endif
