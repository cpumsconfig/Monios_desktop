#include "ipc.h"
#include "common.h"
#include "pool.h"

#define IPC_MAX_PORTS 16U
#define IPC_QUEUE_DEPTH 8U

typedef struct {
    ipc_port_t public_port;
    char queue[IPC_QUEUE_DEPTH][IPC_MESSAGE_MAX];
    uint32_t head;
    uint32_t tail;
    uint32_t count;
} ipc_port_state_t;

static ipc_port_state_t g_ports[IPC_MAX_PORTS];
static uint32_t g_port_bitmap[(IPC_MAX_PORTS + BITMAP_WORD_BITS - 1u) / BITMAP_WORD_BITS];
static pool_t g_port_pool;
static ipc_info_t g_ipc_info;
static char g_ipc_status[64];

static void ipc_copy_text(char *dst, uint32_t size, const char *src)
{
    uint32_t index = 0;

    if (size == 0) {
        return;
    }
    while (src != NULL && src[index] != '\0' && index + 1 < size) {
        dst[index] = src[index];
        index++;
    }
    dst[index] = '\0';
}

static ipc_port_state_t *ipc_find_port(int32_t port_id)
{
    for (uint32_t index = 0; index < IPC_MAX_PORTS; index++) {
        if (g_ports[index].public_port.used && (int32_t) g_ports[index].public_port.port_id == port_id) {
            return &g_ports[index];
        }
    }
    return NULL;
}

int32_t ipc_port_find(const char *name)
{
    for (uint32_t index = 0; index < IPC_MAX_PORTS; index++) {
        if (g_ports[index].public_port.used && strcmp(g_ports[index].public_port.name, name) == 0) {
            return (int32_t) g_ports[index].public_port.port_id;
        }
    }
    return -1;
}

void ipc_init(void)
{
    memset(g_ports, 0, sizeof(g_ports));
    memset(&g_ipc_info, 0, sizeof(g_ipc_info));
    g_ipc_info.max_ports = IPC_MAX_PORTS;
    g_ipc_info.queue_depth = IPC_QUEUE_DEPTH;
    g_ipc_info.message_size = IPC_MESSAGE_MAX;
    pool_init(&g_port_pool, "ipc", g_port_bitmap, IPC_MAX_PORTS);
    strcpy(g_ipc_status, "ipc: ready");
}

int32_t ipc_port_create(const char *name)
{
    int32_t existing;
    int32_t slot;
    ipc_port_state_t *port;

    if (name == NULL || name[0] == '\0') {
        strcpy(g_ipc_status, "ipc: bad name");
        return -1;
    }
    existing = ipc_port_find(name);
    if (existing >= 0) {
        return existing;
    }
    slot = pool_alloc_slot(&g_port_pool);
    if (slot < 0) {
        strcpy(g_ipc_status, "ipc: port table full");
        return -1;
    }
    port = &g_ports[slot];
    memset(port, 0, sizeof(*port));
    port->public_port.used = true;
    port->public_port.port_id = (uint32_t) slot + 1u;
    ipc_copy_text(port->public_port.name, sizeof(port->public_port.name), name);
    g_ipc_info.port_count++;
    strcpy(g_ipc_status, "ipc: port created");
    return (int32_t) port->public_port.port_id;
}

bool ipc_port_close(int32_t port_id)
{
    ipc_port_state_t *port = ipc_find_port(port_id);
    uint32_t slot;

    if (port == NULL || port_id <= 0) {
        strcpy(g_ipc_status, "ipc: bad port");
        return false;
    }
    slot = (uint32_t) port_id - 1u;
    memset(port, 0, sizeof(*port));
    if (g_ipc_info.port_count > 0) {
        g_ipc_info.port_count--;
    }
    pool_free_slot(&g_port_pool, slot);
    strcpy(g_ipc_status, "ipc: port closed");
    return true;
}

bool ipc_send_text(int32_t port_id, const char *text)
{
    ipc_port_state_t *port = ipc_find_port(port_id);

    if (port == NULL || text == NULL) {
        strcpy(g_ipc_status, "ipc: bad port");
        return false;
    }
    if (port->count >= IPC_QUEUE_DEPTH) {
        g_ipc_info.dropped_count++;
        strcpy(g_ipc_status, "ipc: queue full");
        return false;
    }
    ipc_copy_text(port->queue[port->tail], sizeof(port->queue[port->tail]), text);
    port->tail = (port->tail + 1u) % IPC_QUEUE_DEPTH;
    port->count++;
    port->public_port.pending = port->count;
    port->public_port.sent++;
    g_ipc_info.sent_count++;
    strcpy(g_ipc_status, "ipc: queued");
    return true;
}

uint32_t ipc_broadcast_text(const char *text)
{
    uint32_t delivered = 0;

    if (text == NULL) {
        strcpy(g_ipc_status, "ipc: bad broadcast");
        return 0;
    }
    for (uint32_t index = 0; index < IPC_MAX_PORTS; index++) {
        if (g_ports[index].public_port.used && ipc_send_text((int32_t) g_ports[index].public_port.port_id, text)) {
            delivered++;
        }
    }
    strcpy(g_ipc_status, delivered > 0 ? "ipc: broadcast queued" : "ipc: broadcast empty");
    return delivered;
}

int32_t ipc_peek_text(int32_t port_id, char *buffer, uint32_t buffer_size)
{
    ipc_port_state_t *port = ipc_find_port(port_id);

    if (port == NULL || buffer == NULL || buffer_size == 0) {
        strcpy(g_ipc_status, "ipc: bad peek");
        return -1;
    }
    if (port->count == 0) {
        strcpy(g_ipc_status, "ipc: queue empty");
        return 0;
    }
    ipc_copy_text(buffer, buffer_size, port->queue[port->head]);
    strcpy(g_ipc_status, "ipc: peeked");
    return (int32_t) strlen(buffer);
}

int32_t ipc_recv_text(int32_t port_id, char *buffer, uint32_t buffer_size)
{
    ipc_port_state_t *port = ipc_find_port(port_id);
    uint32_t length;

    if (port == NULL || buffer == NULL || buffer_size == 0) {
        strcpy(g_ipc_status, "ipc: bad recv");
        return -1;
    }
    if (port->count == 0) {
        strcpy(g_ipc_status, "ipc: queue empty");
        return 0;
    }
    ipc_copy_text(buffer, buffer_size, port->queue[port->head]);
    length = (uint32_t) strlen(buffer);
    port->head = (port->head + 1u) % IPC_QUEUE_DEPTH;
    port->count--;
    port->public_port.pending = port->count;
    port->public_port.received++;
    g_ipc_info.recv_count++;
    strcpy(g_ipc_status, "ipc: delivered");
    return (int32_t) length;
}

uint32_t ipc_capacity(void)
{
    return IPC_MAX_PORTS;
}

bool ipc_snapshot(uint32_t index, ipc_port_t *out)
{
    if (index >= IPC_MAX_PORTS || out == NULL) {
        return false;
    }
    *out = g_ports[index].public_port;
    return true;
}

const ipc_info_t *ipc_info(void)
{
    return &g_ipc_info;
}

const char *ipc_status(void)
{
    return g_ipc_status;
}
