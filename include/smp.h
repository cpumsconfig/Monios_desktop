#ifndef _SMP_H_
#define _SMP_H_

#include "stdbool.h"
#include "stdint.h"

typedef struct {
    bool supported;
    bool bootstrap_only;
    uint32_t logical_processors;
    uint32_t online_processors;
    char status[64];
} smp_info_t;

void smp_init(void);
const smp_info_t *smp_info(void);

#endif
