#include "common.h"
#include "gop.h"
#include "graphics.h"
#include "gpu.h"
#include "gpu_backend.h"
#include "gpu_igpu.h"
#include "gpu_nvidia.h"
#include "kernel.h"
#include "memory.h"
#include "string.h"

static gpu_info_t g_gpu_info;

void gpu_refresh(void)
{
    const gop_info_t *gop = gop_info();

    g_gpu_info.framebuffer_ready = graphics_active() || gop->available;
    g_gpu_info.opengl_ready = gpu_opengl_probe(g_gpu_info.framebuffer_ready);
    g_gpu_info.vulkan_ready = gpu_vulkan_probe(g_gpu_info.framebuffer_ready);
    g_gpu_info.nvidia_detected = gpu_nvidia_probe(g_gpu_info.framebuffer_ready);
    g_gpu_info.nvidia_ready = gpu_nvidia_ready(g_gpu_info.framebuffer_ready,
                                                g_gpu_info.nvidia_detected);
    g_gpu_info.nvidia_mmio_ready = gpu_nvidia_info()->mmio_ready;
    g_gpu_info.nvidia_memory_enabled = gpu_nvidia_info()->memory_enabled;
    g_gpu_info.nvidia_bus_master_enabled = gpu_nvidia_info()->bus_master_enabled;
    g_gpu_info.nvidia_power_management = gpu_nvidia_info()->power_management_capable;
    g_gpu_info.nvidia_msi_capable = gpu_nvidia_info()->msi_capable;
    g_gpu_info.nvidia_msix_capable = gpu_nvidia_info()->msix_capable;
    g_gpu_info.nvidia_vendor_id = gpu_nvidia_info()->vendor_id;
    g_gpu_info.nvidia_device_id = gpu_nvidia_info()->device_id;
    g_gpu_info.nvidia_mmio_base = gpu_nvidia_info()->mmio_base;
    g_gpu_info.nvidia_power_state = gpu_nvidia_info()->power_state;
    strcpy(g_gpu_info.nvidia_name,
           g_gpu_info.nvidia_detected ? gpu_nvidia_info()->name : "unavailable");
    g_gpu_info.igpu_detected = gpu_igpu_probe(g_gpu_info.framebuffer_ready);
    g_gpu_info.igpu_ready = g_gpu_info.igpu_detected &&
                            gpu_igpu_info()->framebuffer_compatible &&
                            gpu_igpu_info()->mmio_ready &&
                            gpu_igpu_info()->memory_enabled;
    g_gpu_info.igpu_mmio_ready = gpu_igpu_info()->mmio_ready;
    g_gpu_info.igpu_memory_enabled = gpu_igpu_info()->memory_enabled;
    g_gpu_info.igpu_bus_master_enabled = gpu_igpu_info()->bus_master_enabled;
    g_gpu_info.igpu_power_management = gpu_igpu_info()->power_management_capable;
    g_gpu_info.igpu_msi_capable = gpu_igpu_info()->msi_capable;
    g_gpu_info.igpu_msix_capable = gpu_igpu_info()->msix_capable;
    g_gpu_info.igpu_vendor_id = gpu_igpu_info()->vendor_id;
    g_gpu_info.igpu_device_id = gpu_igpu_info()->device_id;
    g_gpu_info.igpu_mmio_base = gpu_igpu_info()->mmio_base;
    g_gpu_info.igpu_power_state = gpu_igpu_info()->power_state;
    strcpy(g_gpu_info.igpu_name,
           g_gpu_info.igpu_detected ? gpu_igpu_info()->name : "unavailable");
    g_gpu_info.acceleration_ready = g_gpu_info.opengl_ready || g_gpu_info.vulkan_ready;
    g_gpu_info.width = graphics_framebuffer_width();
    g_gpu_info.height = graphics_framebuffer_height();
    g_gpu_info.bpp = 32;
    g_gpu_info.pitch = graphics_framebuffer_pitch_bytes();
    g_gpu_info.submit_count = graphics_gpu_submit_count();
    g_gpu_info.present_count = graphics_gpu_present_count();
    strcpy(g_gpu_info.backend, graphics_backend_name());
    strcpy(g_gpu_info.opengl_version,
           g_gpu_info.opengl_ready ? "OpenGL 1.1 shim" : "unavailable");
    strcpy(g_gpu_info.vulkan_version,
           g_gpu_info.vulkan_ready ? "Vulkan 1.0 shim" : "unavailable");
    strcpy(g_gpu_info.status,
           g_gpu_info.nvidia_ready ? gpu_nvidia_status() :
           (g_gpu_info.igpu_ready ? gpu_igpu_status() :
            (g_gpu_info.nvidia_detected ? gpu_nvidia_status() :
             (g_gpu_info.igpu_detected ? gpu_igpu_status() :
              (g_gpu_info.framebuffer_ready ? "gpu: opengl/vulkan shim ready" :
              "gpu: framebuffer pending")))));
}

void gpu_rescan(void)
{
    gpu_nvidia_invalidate();
    gpu_igpu_invalidate();
    gpu_refresh();
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

/* ============================================================
 *  GPU 渲染加速：命令队列 / fill / blit / 硬件光标 / 双缓冲
 * ============================================================ */

#define GPU_ACCEL_CMD_MAX   64U
#define GPU_CURSOR_SIZE     32U

enum {
    GPU_CMD_NOP = 0,
    GPU_CMD_FILL = 1,
    GPU_CMD_BLIT = 2
};

typedef struct {
    uint8_t op;
    uint16_t x, y, w, h;
    uint16_t sx, sy;
    uint32_t color;
} gpu_accel_cmd_t;

static gpu_accel_cmd_t g_cmds[GPU_ACCEL_CMD_MAX];
static uint32_t g_cmd_head;   /* 下一个写入位置 */
static uint32_t g_cmd_count;
static gpu_accel_info_t g_accel;
static uint32_t *g_backbuf;   /* 双缓冲离屏表面 */
static uint32_t g_backbuf_pixels;
static uint32_t g_cursor_plane[GPU_CURSOR_SIZE * GPU_CURSOR_SIZE];

static uint32_t gpu_stride_u32(void)
{
    uint32_t pitch = graphics_framebuffer_pitch_bytes();

    if (pitch == 0U) {
        pitch = g_gpu_info.width * 4U;
    }
    return pitch / 4U;
}

static uint32_t *gpu_render_target(void)
{
    if (g_accel.double_buffer && g_backbuf != NULL) {
        return g_backbuf;
    }
    return (uint32_t *) (uintptr_t) graphics_framebuffer_address();
}

static void gpu_render_fill(uint32_t *fb, uint32_t stride,
                            uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                            uint32_t color)
{
    uint32_t W = g_gpu_info.width;
    uint32_t H = g_gpu_info.height;
    uint32_t yy;

    if (fb == NULL || W == 0U || H == 0U) {
        return;
    }
    if ((uint32_t) x >= W || (uint32_t) y >= H) {
        return;
    }
    if ((uint32_t) x + w > W) w = (uint16_t) (W - x);
    if ((uint32_t) y + h > H) h = (uint16_t) (H - y);

    for (yy = 0; yy < h; yy++) {
        uint32_t row = (uint32_t) y + yy;
        uint32_t col;

        for (col = 0; col < w; col++) {
            fb[row * stride + x + col] = color;
        }
    }
    g_accel.fill_count++;
}

static void gpu_render_blit(uint32_t *fb, uint32_t stride,
                            uint16_t dx, uint16_t dy, uint16_t w, uint16_t h,
                            uint16_t sx, uint16_t sy)
{
    uint32_t W = g_gpu_info.width;
    uint32_t H = g_gpu_info.height;
    int32_t ydir;
    int32_t xdir;

    if (fb == NULL || W == 0U || H == 0U) {
        return;
    }
    if ((uint32_t) dx >= W || (uint32_t) dy >= H) {
        return;
    }
    if ((uint32_t) dx + w > W) w = (uint16_t) (W - dx);
    if ((uint32_t) dy + h > H) h = (uint16_t) (H - dy);

    /* 选择遍历方向避免重叠区域被覆盖 */
    ydir = (dy > sy) ? -1 : 1;
    xdir = (dx > sx) ? -1 : 1;

    for (uint32_t i = 0; i < h; i++) {
        uint32_t yy = (ydir > 0) ? (uint32_t) dy + i : (uint32_t) dy + (h - 1U - i);
        uint32_t syy = (ydir > 0) ? (uint32_t) sy + i : (uint32_t) sy + (h - 1U - i);

        for (uint32_t j = 0; j < w; j++) {
            uint32_t xx = (xdir > 0) ? (uint32_t) dx + j : (uint32_t) dx + (w - 1U - j);
            uint32_t sxx = (xdir > 0) ? (uint32_t) sx + j : (uint32_t) sx + (w - 1U - j);

            if (sxx < W && syy < H) {
                fb[yy * stride + xx] = fb[syy * stride + sxx];
            }
        }
    }
    g_accel.blit_count++;
}

static void gpu_build_default_cursor(void)
{
    /* 简单箭头光标（带 alpha：0 表示透明） */
    for (uint32_t i = 0; i < GPU_CURSOR_SIZE * GPU_CURSOR_SIZE; i++) {
        g_cursor_plane[i] = 0x00000000;
    }
    for (uint32_t y = 0; y < GPU_CURSOR_SIZE; y++) {
        for (uint32_t x = 0; x < GPU_CURSOR_SIZE; x++) {
            /* 三角形箭头轮廓 */
            if (x <= y && y < GPU_CURSOR_SIZE - 4U && x < 18U) {
                uint32_t edge = (x == 0U || y == x || y == x + 1U || y > 26U) ? 0xFF000000u : 0xFFFFFFFFu;
                g_cursor_plane[y * GPU_CURSOR_SIZE + x] = edge;
            }
        }
    }
}

void gpu_accel_init(void)
{
    memset(&g_accel, 0, sizeof(g_accel));
    memset(g_cmds, 0, sizeof(g_cmds));
    g_cmd_head = 0;
    g_cmd_count = 0;
    g_backbuf = NULL;
    g_backbuf_pixels = 0;
    g_accel.initialized = true;
    g_accel.double_buffer = false;
    g_accel.cursor_visible = false;
    strcpy(g_accel.status, "gpuaccel: ready");
    gpu_build_default_cursor();
}

void gpu_accel_fill(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint32_t color)
{
    gpu_accel_cmd_t *cmd;

    if (!g_accel.initialized) {
        return;
    }
    if (g_cmd_count >= GPU_ACCEL_CMD_MAX) {
        gpu_accel_flush();
    }
    cmd = &g_cmds[g_cmd_head % GPU_ACCEL_CMD_MAX];
    cmd->op = GPU_CMD_FILL;
    cmd->x = x; cmd->y = y; cmd->w = w; cmd->h = h;
    cmd->color = color;
    g_cmd_head++;
    g_cmd_count++;
    g_accel.commands_queued++;
}

void gpu_accel_blit(uint16_t dst_x, uint16_t dst_y, uint16_t w, uint16_t h,
                    uint16_t src_x, uint16_t src_y)
{
    gpu_accel_cmd_t *cmd;

    if (!g_accel.initialized) {
        return;
    }
    if (g_cmd_count >= GPU_ACCEL_CMD_MAX) {
        gpu_accel_flush();
    }
    cmd = &g_cmds[g_cmd_head % GPU_ACCEL_CMD_MAX];
    cmd->op = GPU_CMD_BLIT;
    cmd->x = dst_x; cmd->y = dst_y; cmd->w = w; cmd->h = h;
    cmd->sx = src_x; cmd->sy = src_y;
    g_cmd_head++;
    g_cmd_count++;
    g_accel.commands_queued++;
}

void gpu_accel_flush(void)
{
    uint32_t stride = gpu_stride_u32();
    uint32_t *fb = gpu_render_target();

    if (!g_accel.initialized) {
        return;
    }
    for (uint32_t i = 0; i < g_cmd_count; i++) {
        gpu_accel_cmd_t *cmd = &g_cmds[i];

        if (cmd->op == GPU_CMD_FILL) {
            gpu_render_fill(fb, stride, cmd->x, cmd->y, cmd->w, cmd->h, cmd->color);
        } else if (cmd->op == GPU_CMD_BLIT) {
            gpu_render_blit(fb, stride, cmd->x, cmd->y, cmd->w, cmd->h, cmd->sx, cmd->sy);
        }
        g_accel.commands_flushed++;
    }
    g_accel.pending = g_cmd_count;
    g_cmd_count = 0;
    g_cmd_head = 0;
    strcpy(g_accel.status, "gpuaccel: flushed");
}

static void gpu_accel_ensure_backbuf(void)
{
    uint32_t pixels = g_gpu_info.width * g_gpu_info.height;

    if (pixels == 0U) {
        return;
    }
    if (g_backbuf != NULL && g_backbuf_pixels == pixels) {
        return;
    }
    if (g_backbuf != NULL) {
        kfree(g_backbuf);
        g_backbuf = NULL;
        g_backbuf_pixels = 0;
    }
    g_backbuf = (uint32_t *) kmalloc((uint64_t) pixels * 4U);
    if (g_backbuf != NULL) {
        g_backbuf_pixels = pixels;
        memset(g_backbuf, 0, (uint64_t) pixels * 4U);
    }
}

void gpu_accel_present(void)
{
    uint32_t W = g_gpu_info.width;
    uint32_t H = g_gpu_info.height;
    uint32_t stride = gpu_stride_u32();
    uint32_t *front = (uint32_t *) (uintptr_t) graphics_framebuffer_address();

    if (!g_accel.initialized) {
        return;
    }
    gpu_accel_flush();

    if (g_accel.double_buffer && g_backbuf != NULL && front != NULL) {
        /* back -> front 行拷贝（按 stride） */
        for (uint32_t y = 0; y < H; y++) {
            memcpy(front + y * stride, g_backbuf + y * W, (uint64_t) W * 4U);
        }
    }

    /* 硬件光标平面合成（始终画在最上层） */
    if (g_accel.cursor_visible && front != NULL) {
        uint32_t cx = g_accel.cursor_x;
        uint32_t cy = g_accel.cursor_y;

        for (uint32_t dy = 0; dy < GPU_CURSOR_SIZE; dy++) {
            for (uint32_t dx = 0; dx < GPU_CURSOR_SIZE; dx++) {
                uint32_t px = cx + dx;
                uint32_t py = cy + dy;
                uint32_t c = g_cursor_plane[dy * GPU_CURSOR_SIZE + dx];

                if (px < W && py < H && (c & 0xFF000000u) != 0U) {
                    front[py * stride + px] = c;
                }
            }
        }
    }

    g_accel.present_count++;
    strcpy(g_accel.status, "gpuaccel: presented");
}

void gpu_accel_cursor_set(uint16_t x, uint16_t y, bool visible)
{
    g_accel.cursor_x = x;
    g_accel.cursor_y = y;
    g_accel.cursor_visible = visible;
}

const gpu_accel_info_t *gpu_accel_info(void)
{
    g_accel.width = g_gpu_info.width;
    g_accel.height = g_gpu_info.height;
    g_accel.pending = g_cmd_count;
    return &g_accel;
}

const char *gpu_accel_status(void)
{
    return g_accel.status;
}

uint64_t gpu_accel_ctl(uint64_t cmd, uint64_t arg1, uint64_t arg2)
{
    switch (cmd) {
    case GPU_ACCEL_CTL_STATUS: {
        gpu_accel_info();
        gpu_accel_info_t *out = (gpu_accel_info_t *) arg1;
        if (out != NULL) {
            *out = g_accel;
        }
        return 0;
    }
    case GPU_ACCEL_CTL_FLUSH:
        gpu_accel_flush();
        return 0;
    case GPU_ACCEL_CTL_PRESENT:
        gpu_accel_present();
        return 0;
    case GPU_ACCEL_CTL_SET_DOUBLE:
        g_accel.double_buffer = (arg1 != 0U);
        if (g_accel.double_buffer) {
            gpu_accel_ensure_backbuf();
        }
        return 0;
    case GPU_ACCEL_CTL_CURSOR: {
        uint16_t x = (uint16_t) (arg1 & 0xFFFFu);
        uint16_t y = (uint16_t) (arg2 & 0xFFFFu);
        bool vis = ((arg2 >> 32) & 1U) != 0U;

        gpu_accel_cursor_set(x, y, vis);
        return 0;
    }
    default:
        return (uint64_t) -1;
    }
}


/* ============================================================
 *  Video decode framework (syscall 99).
 *  QEMU has no fixed-function decoder, so raw YUV420P frames are
 *  software colour-converted to BGRA8888 for the framebuffer.
 * ============================================================ */
#include "app_memory.h"
#include "exec.h"

static bool     g_vid_ready;
static uint32_t g_vid_w;
static uint32_t g_vid_h;
static uint32_t g_vid_codec;
static uint32_t g_vid_frames;

void gpu_yuv420_to_rgb888(const uint8_t *y_plane, const uint8_t *u_plane,
                          const uint8_t *v_plane, uint32_t width, uint32_t height,
                          uint32_t y_stride, uint32_t uv_stride,
                          uint32_t rgb_stride, uint8_t *rgb_out)
{
    uint32_t row, col;
    for (row = 0U; row < height; row++) {
        const uint8_t *ys = y_plane + (uint64_t) row * y_stride;
        const uint8_t *us = u_plane + (uint64_t) (row >> 1U) * uv_stride;
        const uint8_t *vs = v_plane + (uint64_t) (row >> 1U) * uv_stride;
        uint8_t *op = rgb_out + (uint64_t) row * rgb_stride;
        for (col = 0U; col < width; col++) {
            int32_t yv = (int32_t) ys[col] - 16;
            int32_t cb = (int32_t) us[col >> 1U] - 128;
            int32_t cr = (int32_t) vs[col >> 1U] - 128;
            int32_t base = (yv * 298) >> 8;
            int32_t r = base + ((402 * cr) >> 8);
            int32_t g = base - ((240 * cr + 134 * cb) >> 8);
            int32_t b = base + ((466 * cb) >> 8);
            if (r < 0) r = 0; else if (r > 255) r = 255;
            if (g < 0) g = 0; else if (g > 255) g = 255;
            if (b < 0) b = 0; else if (b > 255) b = 255;
            op[col * 4U + 0U] = (uint8_t) b;   /* B */
            op[col * 4U + 1U] = (uint8_t) g;   /* G */
            op[col * 4U + 2U] = (uint8_t) r;   /* R */
            op[col * 4U + 3U] = 0xFFU;         /* A */
        }
    }
}

bool gpu_video_decode_init(uint32_t width, uint32_t height, uint32_t codec)
{
    if (width == 0U || height == 0U || (width & 1U) || (height & 1U)) {
        g_vid_ready = false;
        return false;
    }
    g_vid_w = width;
    g_vid_h = height;
    g_vid_codec = codec;
    g_vid_frames = 0U;
    g_vid_ready = true;
    return true;
}

bool gpu_video_decode_frame(const uint8_t *bitstream, uint32_t size, uint8_t *rgb_out)
{
    uint32_t plane_yuv;
    const uint8_t *yp, *up, *vp;
    (void) size;
    if (!g_vid_ready || bitstream == NULL || rgb_out == NULL) return false;
    /* Only raw YUV420P is supported on the virtual GPU. Real MPEG-2/H.264
     * bitstream decode would hook a fixed-function decoder here. */
    if (g_vid_codec != VIDEO_CODEC_RAW_YUV420P) return false;
    plane_yuv = g_vid_w * g_vid_h;
    yp = bitstream;
    up = bitstream + plane_yuv;
    vp = bitstream + plane_yuv + (plane_yuv / 4U);
    gpu_yuv420_to_rgb888(yp, up, vp, g_vid_w, g_vid_h,
                         g_vid_w, g_vid_w / 2U, g_vid_w * 4U, rgb_out);
    g_vid_frames++;
    return true;
}

void gpu_video_decode_close(void)
{
    g_vid_ready = false;
}

int32_t video_ctl(uint64_t arg0, uint64_t arg1, uint64_t arg2)
{
    video_ctl_request_t req;
    (void) arg1; (void) arg2;
    if (arg0 == 0U) return -1;
    if (exec_active() && !app_memory_user_range((void *) (uintptr_t) arg0, sizeof(req))) {
        return -1;
    }
    if (exec_active()) {
        if (!app_memory_copy_from_user(&req, (void *) (uintptr_t) arg0, sizeof(req))) return -1;
    } else {
        req = *(video_ctl_request_t *) (uintptr_t) arg0;
    }
    switch (req.cmd) {
    case VIDEO_CTL_INIT:
        gpu_video_decode_init(req.width, req.height, req.codec);
        break;
    case VIDEO_CTL_CLOSE:
        gpu_video_decode_close();
        break;
    default:
        break;
    }
    req.ready = g_vid_ready ? 1U : 0U;
    req.frames_decoded = g_vid_frames;
    /* report codec into status (no snprintf in this build) */
    {
        static const char st_h264[] = "H264: SW fallback stub";
        static const char st_mpg[]  = "MPEG2: SW fallback stub";
        static const char st_raw[]  = "YUV420P: software convert";
        const char *src = st_raw;
        uint32_t i;
        if (g_vid_codec == VIDEO_CODEC_H264) src = st_h264;
        else if (g_vid_codec == VIDEO_CODEC_MPEG2) src = st_mpg;
        for (i = 0U; i < sizeof(req.status) - 1U && src[i] != 0; i++) {
            req.status[i] = src[i];
        }
        req.status[i] = 0;
    }
    if (exec_active()) {
        if (!app_memory_copy_to_user((void *) (uintptr_t) arg0, &req, sizeof(req))) return -1;
    } else {
        *(video_ctl_request_t *) (uintptr_t) arg0 = req;
    }
    return 0;
}

/* ====================================================================== */
/*  Resolution switching / double buffering / hardware cursor / probe /    */
/*  framebuffer read-write — the standard GPU driver interface.            */
/* ====================================================================== */

typedef struct {
    uint32_t width;
    uint32_t height;
    uint32_t bpp;
} gpu_mode_t;

static const gpu_mode_t g_supported_modes[] = {
    { 640u,  480u,  32u },
    { 800u,  600u,  32u },
    { 1024u, 768u,  32u },
    { 1280u, 720u,  32u },
    { 1920u, 1080u, 32u }
};

static uint32_t g_gpu_refresh_rate = 60u;
static bool g_gpu_probed;
static char g_gpu_driver_status[64];

bool gpu_probe(void)
{
    if (!g_gpu_probed) {
        gpu_init();
        g_gpu_probed = true;
    }
    gpu_refresh();
    if (!g_gpu_info.framebuffer_ready) {
        strcpy(g_gpu_driver_status, "gpu: not found");
        log_write(g_gpu_driver_status);
        return false;
    }
    strcpy(g_gpu_driver_status, "gpu: framebuffer ready");
    return true;
}

bool gpu_set_mode(uint32_t width, uint32_t height, uint32_t bpp)
{
    bool supported = false;

    for (uint32_t i = 0; i < sizeof(g_supported_modes) / sizeof(g_supported_modes[0]); i++) {
        if (g_supported_modes[i].width == width &&
            g_supported_modes[i].height == height &&
            g_supported_modes[i].bpp == bpp) {
            supported = true;
            break;
        }
    }
    if (!supported) {
        return false;
    }
    /* Ask the firmware/framebuffer layer to switch resolution when it can. */
    (void) graphics_display_mode_set((uint16_t) width, (uint16_t) height);
    g_gpu_info.width = width;
    g_gpu_info.height = height;
    g_gpu_info.bpp = bpp;
    g_gpu_info.pitch = width * (bpp / 8u);
    /* Drop any cached back buffer so it re-allocates for the new size. */
    if (g_backbuf != NULL) {
        kfree(g_backbuf);
        g_backbuf = NULL;
        g_backbuf_pixels = 0;
    }
    return true;
}

void gpu_set_refresh_rate(uint32_t hz)
{
    if (hz >= 30u && hz <= 240u) {
        g_gpu_refresh_rate = hz;
    }
}

uint32_t gpu_get_refresh_rate(void)
{
    return g_gpu_refresh_rate;
}

/* Swap the back buffer to the front (present), compositing the hardware
 * cursor plane on top. */
void gpu_swap_buffers(void)
{
    gpu_accel_present();
    g_gpu_info.present_count++;
}

void *gpu_get_back_buffer(void)
{
    if (!g_accel.double_buffer) {
        gpu_accel_ensure_backbuf();
        g_accel.double_buffer = true;
    }
    return g_backbuf;
}

/* Load a 32x32 RGBA cursor bitmap into the hardware cursor plane. */
void gpu_set_cursor(uint16_t x, uint16_t y, const uint32_t *cursor_data)
{
    if (cursor_data != NULL) {
        memcpy(g_cursor_plane, cursor_data, sizeof(g_cursor_plane));
    }
    g_accel.cursor_x = x;
    g_accel.cursor_y = y;
    g_accel.cursor_visible = true;
}

void gpu_show_cursor(bool visible)
{
    g_accel.cursor_visible = visible;
}

void gpu_move_cursor(uint16_t x, uint16_t y)
{
    g_accel.cursor_x = x;
    g_accel.cursor_y = y;
}

/* Linear framebuffer read/write. offset/bytes are relative to the start of
 * the front framebuffer. Clamped to the visible surface. */
uint32_t gpu_read(void *dst, uint32_t offset, uint32_t size)
{
    uint32_t fb_addr;
    uint32_t total;
    uint32_t available;
    uint8_t *fb;

    if (dst == NULL) {
        return 0u;
    }
    fb_addr = graphics_framebuffer_address();
    if (fb_addr == 0u) {
        return 0u;
    }
    total = g_gpu_info.width * g_gpu_info.height * (g_gpu_info.bpp / 8u);
    if (offset >= total) {
        return 0u;
    }
    available = total - offset;
    if (size > available) {
        size = available;
    }
    fb = (uint8_t *) (uintptr_t) fb_addr + offset;
    memcpy(dst, fb, size);
    return size;
}

uint32_t gpu_write(const void *src, uint32_t offset, uint32_t size)
{
    uint32_t fb_addr;
    uint32_t total;
    uint32_t available;
    uint8_t *fb;

    if (src == NULL) {
        return 0u;
    }
    fb_addr = graphics_framebuffer_address();
    if (fb_addr == 0u) {
        return 0u;
    }
    total = g_gpu_info.width * g_gpu_info.height * (g_gpu_info.bpp / 8u);
    if (offset >= total) {
        return 0u;
    }
    available = total - offset;
    if (size > available) {
        size = available;
    }
    fb = (uint8_t *) (uintptr_t) fb_addr + offset;
    memcpy(fb, src, size);
    return size;
}

const char *gpu_driver_status(void)
{
    return g_gpu_driver_status[0] != '\0' ? g_gpu_driver_status : gpu_status();
}
