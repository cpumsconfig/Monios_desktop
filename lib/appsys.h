#ifndef _APPSYS_H_
#define _APPSYS_H_

#include "stdbool.h"
#include "stdint.h"

#define APP_ABI_VERSION      1U
#define STDIN_FILENO         0U
#define STDOUT_FILENO        1U
#define STDERR_FILENO        2U
#define PATH_MAX_LEN         256

typedef struct {
    uint32_t abi_version;
    uint32_t argc;
    char **argv;
    char **env;
    uint32_t env_count;
    const char *cwd;
    const char *program_path;
    const char *user_name;
    uint64_t stdin_handle;
    uint64_t stdout_handle;
    uint64_t stderr_handle;
} app_launch_info_t;

typedef struct {
    int32_t x_pixels;
    int32_t y_pixels;
    int32_t wheel_delta;
    uint8_t buttons;
    uint8_t packet_size;
    uint8_t wheel_enabled;
    uint8_t reserved;
} app_mouse_snapshot_t;

typedef struct {
    uint32_t task_count;
    bool scheduler_stopping;
    bool shutdown_requested;
    bool reboot_requested;
    bool net_present;
    bool net_connected;
    uint32_t net_tx_packets;
    uint32_t net_rx_packets;
    uint32_t net_ping_requests;
    uint32_t net_ping_replies;
    bool net_dhcp_configured;
    char net_driver[16];
    char net_mac[18];
    char net_ip[16];
    char net_gateway[16];
    char net_dns[16];
    char net_status[64];
    char net_last_target[64];
    bool audio_playing;
    bool audio_paused;
    bool audio_present;
    uint8_t audio_volume;
    char audio_driver[16];
    char audio_track[64];
    uint32_t gpu_submits;
    uint32_t gpu_presents;
    uint32_t gpu_pending;
    uint32_t wm_windows;
    uint32_t wm_focused;
    bool terminal_active;
    bool terminal_focused;
    uint32_t terminal_lines;
    bool smp_supported;
    bool smp_bootstrap_only;
    uint32_t smp_logical_processors;
    uint32_t smp_online_processors;
} app_system_status_t;

const app_launch_info_t *app_launch_info(void);
void app_runtime_set_launch_info(const app_launch_info_t *info);
int app_getcwd(char *buffer, uint32_t size);
int app_get_mouse(app_mouse_snapshot_t *snapshot);
int app_get_system_status(app_system_status_t *status);
void app_enter_graphics_mode(void);
void app_open_cube3d_window(void);
int app_audio_play_file(const char *path);
int app_graphics_fill_rect(uint16_t x, uint16_t y, uint16_t width, uint16_t height, uint32_t color);
void app_graphics_present(void);
void app_exit(int code);

#endif
