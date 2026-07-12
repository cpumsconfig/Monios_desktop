#ifndef _KERNEL_H_
#define _KERNEL_H_

#include "stdbool.h"
#include "stdint.h"

void kernel_main(uint64_t boot_mode);
void serial_write(const char *str);
void log_write(const char *str);
void log_write_event(const char *tag, const char *detail);
void log_write_bool_event(const char *tag, bool enabled);
void kernel_request_shutdown(void);
void kernel_request_reboot(void);
bool kernel_shutdown_requested(void);
bool kernel_reboot_requested(void);
void kernel_log_hex_u32(const char *label, uint32_t value);
void kernel_request_graphics_mode(void);
void kernel_run_periodic_work(void);
void kernel_run_exec_periodic_work(void);
uint64_t timer_ticks(void);
uint32_t timer_hz(void);

#endif
