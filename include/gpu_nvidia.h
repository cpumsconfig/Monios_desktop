#ifndef _GPU_NVIDIA_H_
#define _GPU_NVIDIA_H_

#include "stdbool.h"
#include "stdint.h"

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
} gpu_nvidia_info_t;

bool gpu_nvidia_probe(bool framebuffer_ready);
void gpu_nvidia_invalidate(void);
bool gpu_nvidia_ready(bool framebuffer_ready, bool nvidia_detected);
const gpu_nvidia_info_t *gpu_nvidia_info(void);
const char *gpu_nvidia_status(void);

#endif
