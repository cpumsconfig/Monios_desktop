#ifndef _TCB_H_
#define _TCB_H_

#include "stdbool.h"
#include "stdint.h"

typedef void (*tcb_entry_t)(void *arg);

typedef struct {
    bool used;
    uint32_t id;
    const char *name;
    tcb_entry_t entry;
    void *arg;
    uint32_t period_ticks;
    uint64_t next_tick;
    uint64_t last_run_tick;
    uint32_t run_count;
} tcb_t;

uint32_t tcb_capacity(void);
bool tcb_snapshot(uint32_t index, tcb_t *out);

#endif
