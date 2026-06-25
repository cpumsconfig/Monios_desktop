#ifndef _SCHEDULER_H_
#define _SCHEDULER_H_

#include "stdbool.h"
#include "stdint.h"

typedef enum {
    SCHED_POLICY_RR = 0,
    SCHED_POLICY_EEVDF,
    SCHED_POLICY_MUQSS
} scheduler_policy_t;

typedef struct {
    scheduler_policy_t policy;
    uint32_t task_count;
    uint32_t dispatches;
    uint32_t yields;
    uint32_t last_task;
} scheduler_info_t;

void scheduler_init(void);
void scheduler_register_task(uint32_t task_id, uint32_t period_ticks);
void scheduler_note_run(uint32_t task_id, uint32_t period_ticks, uint64_t now_ticks);
bool scheduler_set_policy(scheduler_policy_t policy);
const scheduler_info_t *scheduler_info(void);
const char *scheduler_status(void);
const char *scheduler_policy_name(scheduler_policy_t policy);

#endif
