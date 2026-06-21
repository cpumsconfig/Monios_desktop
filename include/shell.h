#ifndef _SHELL_H_
#define _SHELL_H_

#include "keyboard.h"
#include "stdint.h"

typedef enum {
    SHELL_PRIV_R3 = 3,
    SHELL_PRIV_R0 = 0
} shell_privilege_t;

void shell_init(void);
void shell_handle_key_event(const key_event_t *event);
void shell_handle_navigation_key(key_event_type_t type);
void shell_handle_special_key(key_event_type_t type);
void shell_resume_text_mode(void);
shell_privilege_t shell_privilege(void);
const char *shell_current_cwd(void);
bool shell_exec_path(const char *path);
bool shell_output_capture_active(void);
void shell_output_capture_write(const char *buffer, uint32_t size);

// Environment variable functions
void shell_env_set(const char *name, const char *value);
const char *shell_env_get(const char *name);
void shell_env_unset(const char *name);
void shell_env_list(void);

#endif
