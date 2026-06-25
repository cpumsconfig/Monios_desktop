#include "common.h"
#include "gop.h"
#include "graphics.h"
#include "gpu.h"

static gpu_info_t g_gpu_info;

void gpu_refresh(void)
{
    const gop_info_t *gop = gop_info();

    g_gpu_info.framebuffer_ready = graphics_active() || gop->available;
    g_gpu_info.acceleration_ready = true;
    g_gpu_info.width = graphics_framebuffer_width();
    g_gpu_info.height = graphics_framebuffer_height();
    g_gpu_info.bpp = 32;
    g_gpu_info.pitch = graphics_framebuffer_pitch_bytes();
    g_gpu_info.submit_count = graphics_gpu_submit_count();
    g_gpu_info.present_count = graphics_gpu_present_count();
    strcpy(g_gpu_info.backend, graphics_backend_name());
    strcpy(g_gpu_info.status,
           g_gpu_info.framebuffer_ready ? "gpu: framebuffer acceleration shim ready" : "gpu: framebuffer pending");
}

void gpu_init(void)
{
    memset(&g_gpu_info, 0, sizeof(g_gpu_info));
    g_gpu_info.initialized = true;
    gpu_refresh();
}

const gpu_info_t *gpu_info(void)
{
    gpu_refresh();
    return &g_gpu_info;
}

const char *gpu_status(void)
{
    gpu_refresh();
    return g_gpu_info.status;
}
