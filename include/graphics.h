#ifndef _GRAPHICS_H_
#define _GRAPHICS_H_

#include "stdbool.h"
#include "stdint.h"
#include "keyboard.h"

#define GRAPHICS_WIDTH  1024
#define GRAPHICS_HEIGHT 768

void graphics_init(void);
bool graphics_active(void);
void graphics_set_installer_mode(bool enabled);
void graphics_set_boot_animation_mode(bool enabled);
void graphics_enter_mode(void);
void graphics_leave_mode(void);
void graphics_reload_cursor_style(void);
void graphics_boot_animation(void);
void graphics_boot_update(uint32_t progress, const char *status);
bool graphics_boot_load_image(void);
void graphics_boot_finish(void);
void graphics_clear(uint8_t color);
bool graphics_user_fill_rect(uint16_t x, uint16_t y, uint16_t width, uint16_t height, uint32_t color);
bool graphics_user_draw_text(uint16_t x, uint16_t y, const char *text, uint32_t color);
void graphics_user_present(void);
uint32_t graphics_user_read_framebuffer(void *user_dst, uint16_t x, uint16_t y,
                                        uint16_t width, uint16_t height);
uint32_t graphics_user_blit(const void *src, uint16_t x, uint16_t y,
                             uint16_t width, uint16_t height, uint32_t flags);
void graphics_draw_bsod(const char *process, const char *code, const char *text, uint32_t progress);
void graphics_draw_shell(void);
void graphics_mouse_redraw(uint16_t x, uint16_t y);
void graphics_refresh_desktop_entries(void);
void graphics_activate_primary_button(void);
void graphics_open_task_manager(void);
bool graphics_open_notepad_window(void);
bool graphics_open_cube3d_window(void);
void graphics_handle_click(uint16_t x, uint16_t y);
void graphics_handle_right_click(uint16_t x, uint16_t y);
void graphics_handle_mouse_move(uint16_t x, uint16_t y, uint8_t buttons);
void graphics_handle_mouse_wheel(uint16_t x, uint16_t y, int32_t delta);
void graphics_handle_alt_f4(void);
void graphics_periodic_update(uint64_t now_ticks);
void graphics_handle_key_event(const key_event_t *event);
void graphics_notify_process_output(void);
bool graphics_terminal_has_focus(void);
bool graphics_open_process_console_window(int32_t pid, const char *title);
void graphics_close_process_console_window(int32_t pid);
void graphics_mark_process_console_finished(int32_t pid, int32_t exit_code);
bool graphics_set_process_console_window_title(int32_t pid, const char *title);
bool graphics_request_uac_elevation(const char *program_path, const char *reason, uint32_t privilege_level);
void graphics_close_all_programs(void);
void graphics_flush_gpu(void);
void graphics_shutdown(void);
void graphics_shutdown_animation(void);
uint32_t graphics_gpu_submit_count(void);
uint32_t graphics_gpu_present_count(void);
uint32_t graphics_gpu_pending_count(void);
uint32_t graphics_window_count(void);
uint32_t graphics_focused_window_index(void);
uint32_t graphics_framebuffer_address(void);
uint32_t graphics_framebuffer_pitch_bytes(void);
uint32_t graphics_framebuffer_width(void);
uint32_t graphics_framebuffer_height(void);
const char *graphics_backend_name(void);

// Double buffering functions
void graphics_set_double_buffer(bool enabled);
void graphics_set_vsync(bool enabled);
bool graphics_get_double_buffer(void);
bool graphics_get_vsync(void);
uint32_t graphics_get_fps(void);

/* Desktop Experience (UI/UX group) entry points. Kernel-side handlers for
 * syscalls 44-50; syscall.c dispatch is wired separately at integration. */
void graphics_clipboard_set_text(const char *text);
uint32_t graphics_clipboard_get_text(char *buf, uint32_t cap);
uint32_t graphics_clipboard_history_pack(char *buf, uint32_t cap);
uint32_t graphics_clipboard_history_count(void);
const char *graphics_clipboard_history_at(uint32_t index);
void graphics_notification_post(const char *title, const char *body);
void graphics_theme_set(bool dark, uint32_t accent);
void graphics_wallpaper_set(const char *path);
bool graphics_display_mode_set(uint16_t width, uint16_t height);

#endif
