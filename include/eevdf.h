#ifndef _EEVDF_H_
#define _EEVDF_H_

#include "stdbool.h"
#include "stdint.h"

typedef struct {
    uint32_t tracked_tasks;
    uint32_t dispatches;
    uint64_t min_deadline;
    uint64_t last_deadline;
} eevdf_info_t;

void eevdf_init(void);
void eevdf_register_task(uint32_t task_id, uint32_t weight);
void eevdf_note_run(uint32_t task_id, uint32_t slice_ticks, uint64_t now_ticks);
bool eevdf_task_deadline(uint32_t task_id, uint64_t *out_deadline);
const eevdf_info_t *eevdf_info(void);
const char *eevdf_status(void);

#endif
