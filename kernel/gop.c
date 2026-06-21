#include "gop.h"
#include "common.h"
#include "graphics.h"

static gop_info_t g_gop_info;

static void gop_copy_text(char *dst, uint32_t size, const char *src)
{
    uint32_t index = 0;

    if (size == 0) {
        return;
    }
    while (src != NULL && src[index] != '\0' && index + 1 < size) {
        dst[index] = src[index];
        index++;
    }
    dst[index] = '\0';
}

static void gop_refresh(void)
{
    g_gop_info.available = graphics_active();
    g_gop_info.framebuffer = graphics_framebuffer_address();
    g_gop_info.width = graphics_framebuffer_width();
    g_gop_info.height = graphics_framebuffer_height();
    g_gop_info.pitch_bytes = graphics_framebuffer_pitch_bytes();
    gop_copy_text(g_gop_info.backend, sizeof(g_gop_info.backend), graphics_backend_name());
    if (g_gop_info.available) {
        strcpy(g_gop_info.status, "gop: framebuffer active");
    } else {
        strcpy(g_gop_info.status, "gop: framebuffer idle");
    }
}

void gop_init(void)
{
    memset(&g_gop_info, 0, sizeof(g_gop_info));
    gop_refresh();
}

const gop_info_t *gop_info(void)
{
    gop_refresh();
    return &g_gop_info;
}

const char *gop_status(void)
{
    gop_refresh();
    return g_gop_info.status;
}
