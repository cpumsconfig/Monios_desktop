#ifndef _SMP_H_
#define _SMP_H_

#include "stdbool.h"
#include "stdint.h"

#define SMP_CPU_MAX 32U

typedef struct {
    bool supported;
    bool bootstrap_only;
    bool lapic_mmio_ready;
    bool ap_startup_supported;
    uint32_t logical_processors;
    uint32_t online_processors;
    uint32_t firmware_processors;
    uint32_t firmware_enabled_processors;
    uint32_t firmware_enabled_lapic_id_count;
    uint32_t firmware_enabled_lapic_ids[SMP_CPU_MAX];
    uint32_t ap_startup_targets;
    uint32_t ap_startup_pending;
    uint32_t lapic_address;
    uint32_t bootstrap_lapic_id;
    char status[64];
} smp_info_t;

void smp_init(void);
const smp_info_t *smp_info(void);

#endif
