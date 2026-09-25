#ifndef _MONIOS_DLL_H_
#define _MONIOS_DLL_H_

#include "appsys.h"
#include "stdint.h"
#include "icons_data.h"

#define MONIOS_DLL_ABI_VERSION 1U
#define MONIOS_HTTP_RESPONSE_MAX (32U * 1024U)

#if !defined(MONIOS_DLL_BUILD) && defined(__GNUC__)
#define MONIOS_DLL_API __attribute__((dllimport))
#else
#define MONIOS_DLL_API
#endif

MONIOS_DLL_API uint32_t monios_abi_version(void);
MONIOS_DLL_API uint64_t monios_syscall0(uint64_t nr);
MONIOS_DLL_API uint64_t monios_syscall1(uint64_t nr, uint64_t arg0);
MONIOS_DLL_API uint64_t monios_syscall2(uint64_t nr, uint64_t arg0, uint64_t arg1);
MONIOS_DLL_API uint64_t monios_syscall3(uint64_t nr, uint64_t arg0, uint64_t arg1, uint64_t arg2);
MONIOS_DLL_API uint64_t monios_get_ticks(void);
MONIOS_DLL_API void monios_log_string(const char *text);
MONIOS_DLL_API int32_t monios_console_set_title(const char *title);
MONIOS_DLL_API int32_t monios_getcwd(char *buffer, uint32_t size);
MONIOS_DLL_API int32_t monios_get_mouse(app_mouse_snapshot_t *snapshot);
MONIOS_DLL_API int32_t monios_get_system_status(app_system_status_t *status);
MONIOS_DLL_API int32_t monios_http_get_url(const char *url, char *buffer, uint32_t buffer_size);
MONIOS_DLL_API int32_t monios_handle_write(uint64_t handle, const void *buffer, uint32_t size);
MONIOS_DLL_API int32_t monios_handle_read(uint64_t handle, void *buffer, uint32_t size);
MONIOS_DLL_API int32_t monios_file_read(const char *path, void *buffer, uint32_t size);
MONIOS_DLL_API int32_t monios_file_write(const char *path, const void *buffer, uint32_t size);
MONIOS_DLL_API int32_t monios_file_size(const char *path);
MONIOS_DLL_API int32_t monios_file_exists(const char *path);
MONIOS_DLL_API int32_t monios_file_is_dir(const char *path);
MONIOS_DLL_API int32_t monios_file_delete(const char *path);
MONIOS_DLL_API int32_t monios_file_mkdir(const char *path);
MONIOS_DLL_API int32_t monios_file_rmdir(const char *path);
MONIOS_DLL_API int32_t monios_file_list_dir(const char *path, char *buffer, uint32_t size);
MONIOS_DLL_API int32_t monios_audio_play_pcm(const void *data, uint32_t byte_count,
                                             uint32_t sample_rate, uint16_t channels,
                                             uint16_t bits_per_sample);
MONIOS_DLL_API int32_t monios_socket_udp_open(uint16_t local_port);
MONIOS_DLL_API int32_t monios_socket_close(int32_t handle);
MONIOS_DLL_API int32_t monios_socket_sendto(int32_t handle, const char *dst_host,
                                            uint16_t dst_port, const void *payload,
                                            uint16_t payload_len);
MONIOS_DLL_API int32_t monios_socket_recvfrom(int32_t handle, char *src_ip,
                                              uint16_t *src_port, void *buffer,
                                              uint16_t buffer_size);
MONIOS_DLL_API int32_t monios_socket_tcp_open(uint16_t local_port);
MONIOS_DLL_API int32_t monios_socket_tcp_connect(int32_t handle, const char *dst_host,
                                                uint16_t dst_port);
MONIOS_DLL_API int32_t monios_socket_tcp_send(int32_t handle, const void *data, uint16_t len);
MONIOS_DLL_API int32_t monios_socket_tcp_recv(int32_t handle, void *buffer, uint16_t buffer_size);
MONIOS_DLL_API int32_t monios_socket_tcp_has_data(int32_t handle);
MONIOS_DLL_API int32_t monios_socket_tcp_connected(int32_t handle);
MONIOS_DLL_API int32_t monios_futex_wait(uint64_t address, uint32_t expected, uint32_t timeout_ticks);
MONIOS_DLL_API int32_t monios_futex_wake(uint64_t address, uint32_t count);
MONIOS_DLL_API int32_t monios_ipc_create(const char *name);
MONIOS_DLL_API int32_t monios_ipc_send(int32_t port_id, const char *text);
MONIOS_DLL_API int32_t monios_ipc_recv(int32_t port_id, char *buffer, uint32_t buffer_size);
MONIOS_DLL_API int32_t monios_signal_send(int32_t pid, uint32_t signo);
MONIOS_DLL_API uint32_t monios_signal_pending(int32_t pid);
MONIOS_DLL_API uint32_t monios_signal_take(int32_t pid);
MONIOS_DLL_API void monios_signal_clear(int32_t pid);
MONIOS_DLL_API int32_t monios_request_r2(const char *reason);
MONIOS_DLL_API int32_t monios_request_r0(const char *reason);
MONIOS_DLL_API int32_t monios_registry_get(const char *key, char *value, uint32_t value_size);
MONIOS_DLL_API int32_t monios_registry_set(const char *key, const char *value);
MONIOS_DLL_API int32_t monios_default_app_get(const char *extension, char *app_path, uint32_t app_path_size);
MONIOS_DLL_API int32_t monios_default_app_set(const char *extension, const char *app_path);
MONIOS_DLL_API int32_t monios_defer_exec(const char *path);
MONIOS_DLL_API int32_t monios_installer_boot_media(void);
MONIOS_DLL_API int32_t monios_installer_list_targets(app_installer_target_list_t *list);
MONIOS_DLL_API int32_t monios_installer_write_disk(const char *source_path,
                                                   uint32_t source_offset,
                                                   uint32_t disk_lba,
                                                   uint32_t byte_count);
MONIOS_DLL_API int32_t monios_installer_write_buffer(const void *data,
                                                     uint32_t disk_lba,
                                                     uint32_t byte_count);
MONIOS_DLL_API int32_t monios_installer_write_target_file(const char *target_path,
                                                           const void *data,
                                                           uint32_t target_lba,
                                                           uint32_t byte_count);
MONIOS_DLL_API int32_t monios_installer_copy_target_file(const char *source_path,
                                                         uint32_t source_offset,
                                                         uint32_t byte_count,
                                                         const char *target_path,
                                                         uint32_t target_lba);
MONIOS_DLL_API int32_t monios_installer_read_media(const char *source_path,
                                                   uint32_t source_offset,
                                                   void *data,
                                                   uint32_t byte_count);
MONIOS_DLL_API int32_t monios_installer_media_size(const char *source_path);
MONIOS_DLL_API void monios_installer_reboot(void);
MONIOS_DLL_API uint64_t monios_backup_ctl(uint32_t op, uint64_t a, uint64_t b, uint64_t c, uint64_t d);
MONIOS_DLL_API void monios_exit_process(int32_t code);
MONIOS_DLL_API int32_t monios_draw_icon(uint32_t icon_id, uint16_t x, uint16_t y, uint16_t size);
MONIOS_DLL_API uint32_t monios_icon_count(void);

#endif
