#ifndef _SCHEDOPT_H_
#define _SCHEDOPT_H_

#include "stdbool.h"
#include "stdint.h"

typedef struct {
    bool initialized;
    bool smp_supported;
    bool ap_scheduler_ready;
    bool load_balancer_ready;
    uint32_t logical_processors;
    uint32_t online_processors;
    uint32_t dispatches;
    char policy[16];
    char status[64];
} schedopt_info_t;

void schedopt_init(void);
void schedopt_refresh(void);
const schedopt_info_t *schedopt_info(void);
const char *schedopt_status(void);

#endif
