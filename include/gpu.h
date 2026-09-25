#ifndef _GPU_H_
#define _GPU_H_

#include "stdbool.h"
#include "stdint.h"

typedef struct {
    bool initialized;
    bool framebuffer_ready;
    bool acceleration_ready;
    bool opengl_ready;
    bool vulkan_ready;
    bool nvidia_detected;
    bool nvidia_ready;
    bool nvidia_mmio_ready;
    bool nvidia_memory_enabled;
    bool nvidia_bus_master_enabled;
    bool nvidia_power_management;
    bool nvidia_msi_capable;
    bool nvidia_msix_capable;
    uint16_t nvidia_vendor_id;
    uint16_t nvidia_device_id;
    uint64_t nvidia_mmio_base;
    uint8_t nvidia_power_state;
    bool igpu_detected;
    bool igpu_ready;
    bool igpu_mmio_ready;
    bool igpu_memory_enabled;
    bool igpu_bus_master_enabled;
    bool igpu_power_management;
    bool igpu_msi_capable;
    bool igpu_msix_capable;
    uint16_t igpu_vendor_id;
    uint16_t igpu_device_id;
    uint64_t igpu_mmio_base;
    uint8_t igpu_power_state;
    uint32_t width;
    uint32_t height;
    uint32_t bpp;
    uint32_t pitch;
    uint32_t submit_count;
    uint32_t present_count;
    char backend[24];
    char opengl_version[24];
    char vulkan_version[24];
    char nvidia_name[32];
    char igpu_name[32];
    char status[64];
} gpu_info_t;

void gpu_init(void);
void gpu_refresh(void);
void gpu_rescan(void);
const gpu_info_t *gpu_info(void);
const char *gpu_status(void);

/* ============================================================
 *  GPU 渲染加速（命令队列 + 矩形填充/位图搬移 + 硬件光标平面 + 双缓冲）
 *  - 渲染命令先入队，gpu_accel_flush() 批量执行。
 *  - 双缓冲开启时，渲染写入离屏 back buffer，gpu_accel_present() 提交到前台。
 *  - 硬件光标：维护 32x32 RGBA 光标平面，present 时合成到画面最上层。
 *
 *  与 kernel/ui/graphics.c 的集成点（graphics.c 由 UI 组负责）：
 *   - 在 graphics_user_fill_rect() 内部优先调用 gpu_accel_fill()，再 flush。
 *   - 在 graphics_user_present() 内部调用 gpu_accel_present()。
 *   - 鼠标移动时调用 gpu_accel_cursor_set(x,y,true)。
 * ============================================================ */

#define GPU_ACCEL_CTL_STATUS     0u   /* arg1: user gpu_accel_info_t* */
#define GPU_ACCEL_CTL_FLUSH      1u   /* flush 命令队列 */
#define GPU_ACCEL_CTL_PRESENT    2u   /* present back buffer -> front */
#define GPU_ACCEL_CTL_SET_DOUBLE 3u   /* arg1: 0/1 开关双缓冲 */
#define GPU_ACCEL_CTL_CURSOR     4u   /* arg1=x arg2=y(高16位) | visible(低位) */

typedef struct {
    bool initialized;
    bool double_buffer;
    bool cursor_visible;
    uint32_t width;
    uint32_t height;
    uint32_t commands_queued;
    uint32_t commands_flushed;
    uint32_t fill_count;
    uint32_t blit_count;
    uint32_t present_count;
    uint32_t pending;
    uint16_t cursor_x;
    uint16_t cursor_y;
    char status[64];
} gpu_accel_info_t;

void gpu_accel_init(void);
void gpu_accel_fill(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint32_t color);
void gpu_accel_blit(uint16_t dst_x, uint16_t dst_y, uint16_t w, uint16_t h,
                    uint16_t src_x, uint16_t src_y);
void gpu_accel_flush(void);
void gpu_accel_present(void);
void gpu_accel_cursor_set(uint16_t x, uint16_t y, bool visible);
const gpu_accel_info_t *gpu_accel_info(void);
const char *gpu_accel_status(void);

/* syscall 61 handler（syscall.c dispatch 尚未接入，此处仅实现并导出） */
uint64_t gpu_accel_ctl(uint64_t cmd, uint64_t arg1, uint64_t arg2);

/* ============================================================
 *  GPU-accelerated video decode framework (syscall 99 VIDEO_CTL)
 *
 *  On QEMU virtual GPU there is no fixed-function video decoder, so the
 *  "decode" path is a software YUV->RGB fallback: raw YUV420P frames are
 *  colour-converted to 0x00RRGGBB for the framebuffer. Real MPEG-2/H.264
 *  bitstream decode is a documented stub hook.
 * ============================================================ */
#define VIDEO_CTL_INIT     0u
#define VIDEO_CTL_CLOSE    1u
#define VIDEO_CTL_STATUS   2u

#define VIDEO_CODEC_RAW_YUV420P 0u
#define VIDEO_CODEC_MPEG2       1u
#define VIDEO_CODEC_H264        2u

typedef struct {
    uint32_t cmd;
    uint32_t codec;
    uint32_t width;
    uint32_t height;
    uint32_t fps;
    uint32_t ready;
    uint32_t frames_decoded;
    char     status[64];
} video_ctl_request_t;

bool gpu_video_decode_init(uint32_t width, uint32_t height, uint32_t codec);
bool gpu_video_decode_frame(const uint8_t *bitstream, uint32_t size, uint8_t *rgb_out);
void gpu_video_decode_close(void);

void gpu_yuv420_to_rgb888(const uint8_t *y_plane, const uint8_t *u_plane,
                          const uint8_t *v_plane, uint32_t width, uint32_t height,
                          uint32_t y_stride, uint32_t uv_stride,
                          uint32_t rgb_stride, uint8_t *rgb_out);

int32_t video_ctl(uint64_t arg0, uint64_t arg1, uint64_t arg2);

/* ============================================================
 *  Standard GPU driver interface: probe / set_mode / double buffer /
 *  hardware cursor / framebuffer read-write.
 * ============================================================ */
bool gpu_probe(void);
bool gpu_set_mode(uint32_t width, uint32_t height, uint32_t bpp);
void gpu_set_refresh_rate(uint32_t hz);
uint32_t gpu_get_refresh_rate(void);
void gpu_swap_buffers(void);
void *gpu_get_back_buffer(void);
void gpu_set_cursor(uint16_t x, uint16_t y, const uint32_t *cursor_data);
void gpu_show_cursor(bool visible);
void gpu_move_cursor(uint16_t x, uint16_t y);
uint32_t gpu_read(void *dst, uint32_t offset, uint32_t size);
uint32_t gpu_write(const void *src, uint32_t offset, uint32_t size);
const char *gpu_driver_status(void);

#endif
