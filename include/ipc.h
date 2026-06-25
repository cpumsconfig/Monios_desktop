#ifndef _IPC_H_
#define _IPC_H_

#include "stdbool.h"
#include "stdint.h"

#define IPC_PORT_NAME_MAX 16U
#define IPC_MESSAGE_MAX 128U
#define IPC_CALL_CREATE 1U
#define IPC_CALL_SEND   2U
#define IPC_CALL_RECV   3U

typedef struct {
    bool used;
    uint32_t port_id;
    char name[IPC_PORT_NAME_MAX];
    uint32_t pending;
    uint32_t sent;
    uint32_t received;
} ipc_port_t;

typedef struct {
    uint32_t port_count;
    uint32_t sent_count;
    uint32_t recv_count;
    uint32_t dropped_count;
    uint32_t max_ports;
    uint32_t queue_depth;
    uint32_t message_size;
} ipc_info_t;

typedef struct {
    char name[IPC_PORT_NAME_MAX];
    int32_t port_id;
} ipc_create_request_t;

typedef struct {
    int32_t port_id;
    char text[IPC_MESSAGE_MAX];
} ipc_send_request_t;

typedef struct {
    int32_t port_id;
    char *buffer;
    uint32_t buffer_size;
    int32_t result;
} ipc_recv_request_t;

void ipc_init(void);
int32_t ipc_port_create(const char *name);
int32_t ipc_port_find(const char *name);
bool ipc_port_close(int32_t port_id);
bool ipc_send_text(int32_t port_id, const char *text);
uint32_t ipc_broadcast_text(const char *text);
int32_t ipc_peek_text(int32_t port_id, char *buffer, uint32_t buffer_size);
int32_t ipc_recv_text(int32_t port_id, char *buffer, uint32_t buffer_size);
uint32_t ipc_capacity(void);
bool ipc_snapshot(uint32_t index, ipc_port_t *out);
const ipc_info_t *ipc_info(void);
const char *ipc_status(void);

#endif
