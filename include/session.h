#ifndef _SESSION_H_
#define _SESSION_H_

#include "stdbool.h"
#include "stdint.h"

#define SESSION_USER_MAX 4

typedef struct {
    char name[16];
    char home[32];
} session_user_t;

void session_init(void);
uint32_t session_user_count(void);
const session_user_t *session_user_at(uint32_t index);
const session_user_t *session_current_user(void);
bool session_login(uint32_t index);
bool session_login_name(const char *name);
bool session_validate_credentials(const char *username, const char *password);
bool session_verify_password(const char *password);
const char *session_default_desktop_app(void);
const char *session_default_logon_app(void);

#endif
