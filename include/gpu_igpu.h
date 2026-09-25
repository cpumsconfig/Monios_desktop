#ifndef _GPU_IGPU_H_
#define _GPU_IGPU_H_

#include "stdbool.h"
#include "stdint.h"

typedef enum {
    GPU_IGPU_VENDOR_NONE = 0,
    GPU_IGPU_VENDOR_INTEL = 1,
    GPU_IGPU_VENDOR_AMD = 2
} gpu_igpu_vendor_t;

typedef struct {
    bool detected;
    bool framebuffer_compatible;
    bool native_backend_ready;
    bool mmio_ready;
    bool memory_enabled;
    bool bus_master_enabled;
    bool power_management_capable;
    bool msi_capable;
    bool msix_capable;
    gpu_igpu_vendor_t vendor;
    uint16_t vendor_id;
    uint16_t device_id;
    uint16_t command;
    uint8_t bus;
    uint8_t slot;
    uint8_t func;
    uint8_t irq;
    uint8_t power_state;
    uint64_t mmio_base;
    char name[32];
    char status[64];
} gpu_igpu_info_t;

bool gpu_igpu_probe(bool framebuffer_ready);
void gpu_igpu_invalidate(void);
const gpu_igpu_info_t *gpu_igpu_info(void);
const char *gpu_igpu_status(void);

#endif
