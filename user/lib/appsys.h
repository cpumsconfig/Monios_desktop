#ifndef _APPSYS_H_
#define _APPSYS_H_

#include "stdbool.h"
#include "stdint.h"

#define APP_ABI_VERSION      1U
#define STDIN_FILENO         0U
#define STDOUT_FILENO        1U
#define STDERR_FILENO        2U
#define PATH_MAX_LEN         256

#define APP_IMAGE_FLAG_CONSOLE      0x00000001U
#define APP_IMAGE_FLAG_GUI          0x00000002U
#define APP_IMAGE_FLAG_DRIVER       0x00000004U
#define APP_IMAGE_FLAG_SIGNED       0x00000008U
#define APP_IMAGE_FLAG_NEEDS_R0     0x00000010U
#define APP_IMAGE_FLAG_NEEDS_R2     0x00000020U

#define APP_PRIV_R0 0U
#define APP_PRIV_R2 2U
#define APP_PRIV_R3 3U

typedef struct {
    uint32_t abi_version;
    uint32_t image_flags;
    uint32_t privilege_level;
    uint32_t reserved;
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
int app_socket_udp_open(uint16_t local_port);
int app_socket_close(int handle);
int app_socket_sendto(int handle, const char *dst_host, uint16_t dst_port, const void *payload, uint16_t payload_len);
int app_socket_recvfrom(int handle, char *src_ip, uint16_t *src_port, void *buffer, uint16_t buffer_size);
int app_futex_wait(uint64_t address, uint32_t expected, uint32_t timeout_ticks);
int app_futex_wake(uint64_t address, uint32_t count);
int app_ipc_create(const char *name);
int app_ipc_send(int port_id, const char *text);
int app_ipc_recv(int port_id, char *buffer, uint32_t buffer_size);
int app_signal_send(int pid, uint32_t signo);
uint32_t app_signal_pending(int pid);
uint32_t app_signal_take(int pid);
void app_signal_clear(int pid);
bool app_request_r2(const char *reason);
bool app_request_r0(const char *reason);
void app_exit(int code);

#endif
