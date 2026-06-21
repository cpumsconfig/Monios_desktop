#ifndef _PRSYS_H_
#define _PRSYS_H_

#include "stdint.h"

typedef struct {
    uint32_t processes;
    uint32_t tasks;
    uint64_t uptime_ticks;
    uint32_t scheduler_policy;
} prsys_info_t;

void prsys_init(void);
const prsys_info_t *prsys_info(void);
const char *prsys_status(void);

#endif
