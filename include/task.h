#ifndef _TASK_H_
#define _TASK_H_

#include "stdbool.h"
#include "stdint.h"
#include "tcb.h"

typedef tcb_entry_t task_fn_t;

void task_init(void);
int task_create(const char *name, task_fn_t fn, void *arg, uint32_t period_ticks, bool run_immediately);
void task_run_ready(void);
uint32_t task_count(void);
void task_shutdown_all(void);
bool task_scheduler_stopping(void);

#endif
