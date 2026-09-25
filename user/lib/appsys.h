#ifndef _APPSYS_H_
#define _APPSYS_H_

#include "stdbool.h"
#include "stdint.h"
#include "driver_status.h"

#define APP_ABI_VERSION      5U
#define STDIN_FILENO         0U
#define STDOUT_FILENO        1U
#define STDERR_FILENO        2U
#define PATH_MAX_LEN         256

#define APP_SUBSYSTEM_UNKNOWN 0U
#define APP_SUBSYSTEM_NATIVE  1U
#define APP_SUBSYSTEM_WINDOWS 2U
#define APP_SUBSYSTEM_CONSOLE 3U

#define APP_IMAGE_FLAG_CONSOLE      0x00000001U
#define APP_IMAGE_FLAG_GUI          0x00000002U
#define APP_IMAGE_FLAG_DRIVER       0x00000004U
#define APP_IMAGE_FLAG_SIGNED       0x00000008U
#define APP_IMAGE_FLAG_NEEDS_R0     0x00000010U
#define APP_IMAGE_FLAG_NEEDS_R2     0x00000020U
#define APP_IMAGE_FLAG_CERT_PRESENT 0x00000040U
#define APP_IMAGE_FLAG_CERT_VALID   0x00000080U
#define APP_IMAGE_FLAG_CERT_REQUIRED 0x00000100U
#define APP_IMAGE_FLAG_RESOURCE_TABLE 0x00000200U
#define APP_IMAGE_FLAG_ICON_RESOURCE 0x00000400U
#define APP_IMAGE_FLAG_MANIFEST_RESOURCE 0x00000800U

#define APP_PRIV_R0 0U
#define APP_PRIV_R2 2U
#define APP_PRIV_R3 3U

#define APP_INSTALLER_CHUNK_MAX (512U * 256U)
#define APP_INSTALLER_COPY_TARGET_MAX (64U * 1024U * 1024U)
#define APP_INSTALLER_MAX_TARGETS 6U
#define APP_INSTALLER_TARGET_KIND_DISK 0U
#define APP_INSTALLER_TARGET_KIND_PART 1U
#define APP_INSTALLER_TARGET_KIND_UEFI_ESP 2U
#define APP_INSTALLER_TARGET_WHOLE_DISK 0xFFU

typedef struct {
    uint8_t kind;
    uint8_t partition_index;
    uint8_t active;
    uint8_t partition_type;
    uint32_t start_lba;
    uint32_t sector_count;
} app_installer_target_info_t;

typedef struct {
    uint8_t disk_present;
    uint8_t reserved[3];
    uint32_t disk_sector_count;
    char disk_model[41];
    uint32_t target_count;
    app_installer_target_info_t targets[APP_INSTALLER_MAX_TARGETS];
} app_installer_target_list_t;

typedef struct {
    const char *source_path;
    uint32_t source_offset;
    uint32_t disk_lba;
    uint32_t byte_count;
    uint32_t bytes_written;
} app_installer_write_request_t;

typedef struct {
    const void *data;
    uint32_t disk_lba;
    uint32_t byte_count;
    uint32_t bytes_written;
} app_installer_buffer_write_request_t;

typedef struct {
    const char *target_path;
    const void *data;
    uint32_t target_lba;
    uint32_t byte_count;
    uint32_t bytes_written;
} app_installer_target_write_request_t;

typedef struct {
    const char *source_path;
    uint32_t source_offset;
    const char *target_path;
    uint32_t target_lba;
    uint32_t byte_count;
    uint32_t bytes_written;
} app_installer_target_copy_request_t;

typedef struct {
    const char *source_path;
    uint32_t source_offset;
    void *data;
    uint32_t byte_count;
    uint32_t bytes_read;
} app_installer_media_read_request_t;

typedef struct {
    const char *source_path;
    int32_t file_size;
    uint8_t exists;
    uint8_t reserved[3];
} app_installer_media_stat_request_t;

typedef struct {
    uint32_t abi_version;
    uint32_t image_flags;
    uint32_t privilege_level;
    uint32_t subsystem;
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
    uint32_t process_count;
    int32_t current_pid;
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
    uint32_t smp_firmware_processors;
    uint32_t smp_firmware_enabled_processors;
} app_system_status_t;

const app_launch_info_t *app_launch_info(void);
void app_runtime_set_launch_info(const app_launch_info_t *info);
uint64_t app_ticks(void);
void app_sleep_ticks(uint32_t ticks);
void app_log(const char *text);
int app_getcwd(char *buffer, uint32_t size);
int app_get_mouse(app_mouse_snapshot_t *snapshot);
int app_get_system_status(app_system_status_t *status);
int app_http_get_url(const char *url, char *buffer, uint32_t buffer_size);
int app_file_read(const char *path, void *buffer, uint32_t size);
int app_file_write(const char *path, const void *buffer, uint32_t size);
int app_file_size(const char *path);
bool app_file_exists(const char *path);
bool app_file_is_dir(const char *path);
bool app_file_delete(const char *path);
bool app_file_mkdir(const char *path);
bool app_file_rmdir(const char *path);
int app_file_list_dir(const char *path, char *buffer, uint32_t size);
void app_enter_graphics_mode(void);
int app_audio_play_pcm(const void *data, uint32_t byte_count, uint32_t sample_rate,
                       uint16_t channels, uint16_t bits_per_sample);
int app_graphics_fill_rect(uint16_t x, uint16_t y, uint16_t width, uint16_t height, uint32_t color);
int app_graphics_draw_text(uint16_t x, uint16_t y, const char *text, uint32_t color);
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
bool app_registry_get(const char *key, char *value, uint32_t value_size);
bool app_registry_set(const char *key, const char *value);
bool app_default_app_get(const char *extension, char *app_path, uint32_t app_path_size);
bool app_default_app_set(const char *extension, const char *app_path);
bool app_defer_exec(const char *path);
bool app_driver_load(const char *path);
bool app_driver_unload(const char *name, bool force);
bool app_driver_query(driver_status_snapshot_t *snapshot);
bool app_installer_boot_media(void);
int app_installer_list_targets(app_installer_target_list_t *list);
int app_installer_write_disk(const char *source_path, uint32_t source_offset, uint32_t disk_lba, uint32_t byte_count);
int app_installer_write_buffer(const void *data, uint32_t disk_lba, uint32_t byte_count);
int app_installer_write_target_file(const char *target_path, const void *data, uint32_t target_lba, uint32_t byte_count);
int app_installer_copy_target_file(const char *source_path, uint32_t source_offset, uint32_t byte_count, const char *target_path, uint32_t target_lba);
int app_installer_read_media(const char *source_path, uint32_t source_offset, void *data, uint32_t byte_count);
int app_installer_media_size(const char *source_path);
void app_installer_reboot(void);
uint64_t app_backup_ctl(uint32_t op, uint64_t a, uint64_t b, uint64_t c, uint64_t d);
void app_exit(int code);

#endif
