#ifndef _MUQSS_H_
#define _MUQSS_H_

#include "stdint.h"

typedef struct {
    uint32_t tracked_tasks;
    uint32_t dispatches;
    uint32_t active_score;
    uint32_t last_task;
} muqss_info_t;

void muqss_init(void);
void muqss_register_task(uint32_t task_id, uint32_t period_ticks);
void muqss_note_run(uint32_t task_id, uint32_t period_ticks, uint64_t now_ticks);
const muqss_info_t *muqss_info(void);
const char *muqss_status(void);

#endif
