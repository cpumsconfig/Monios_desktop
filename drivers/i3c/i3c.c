#include "i3c.h"
#include "common.h"
#include "i2c.h"
#include "kernel.h"
#include "pci.h"

static i3c_info_t g_i3c_info;
static i3c_device_t g_i3c_devices[I3C_MAX_DEVICES];
static uint8_t g_i3c_device_count;
static const i3c_bus_ops_t *g_i3c_bus_ops;   /* 控制器后端，可为 NULL */

/* I3C CCC command names for logging */
static const char *i3c_ccc_names[] = {
    "ENEC", "DISEC", "ENTAS0", "ENTAS1", "ENTAS2", "ENTAS3",
    "RSTDAA", "ENTDAA", "DEFSLVS", "SETMWL", "SETMRL", "ENTTM",
    "SETBUSCON", "ENDXFER", NULL, NULL,
    "ENTHDR0", "ENTHDR1", "ENTHDR2", "ENTHDR3", "ENTHDR4",
    "ENTHDR5", "ENTHDR6", "ENTHDR7", "SETXTIME", "SETAASA"
};

/* Get CCC command name */
static __attribute__((unused)) const char *i3c_ccc_name(uint8_t ccc)
{
    if (ccc < sizeof(i3c_ccc_names) / sizeof(i3c_ccc_names[0])) {
        return i3c_ccc_names[ccc] ? i3c_ccc_names[ccc] : "RESERVED";
    }
    return "UNKNOWN";
}

/* ── DAA 地址工具 ────────────────────────────────────────────────── */

bool i3c_daa_addr_valid(uint8_t addr)
{
    return addr >= I3C_DYN_ADDR_MIN && addr <= I3C_DYN_ADDR_MAX;
}

/* ENTDAA 地址字节使用奇校验：7 位地址 + 1 位校验位，
 * 使整个字节中 1 的个数为奇数。 */
uint8_t i3c_daa_addr_parity(uint8_t addr7)
{
    uint8_t v = (uint8_t) (addr7 & 0x7Fu);
    uint8_t ones = 0u;
    uint8_t b;

    for (b = 0u; b < 7u; b++) {
        if ((v >> b) & 1u) {
            ones++;
        }
    }
    return (uint8_t) (v | ((ones & 1u) ? 0x00u : 0x80u));
}

/* 该地址是否已被设备表中某个动态地址占用。 */
static bool i3c_dyn_addr_taken(uint8_t addr)
{
    for (uint8_t i = 0u; i < g_i3c_device_count; i++) {
        if (g_i3c_devices[i].dynamic_addr == addr) {
            return true;
        }
    }
    return false;
}

/* 选一个当前未占用的最低合法动态地址；没有可用的返回 0。 */
static uint8_t i3c_pick_dynamic_addr(void)
{
    for (uint16_t a = I3C_DYN_ADDR_MIN; a <= I3C_DYN_ADDR_MAX; a++) {
        if (!i3c_dyn_addr_taken((uint8_t) a)) {
            return (uint8_t) a;
        }
    }
    return 0u;
}

/* ── 后端注册 ────────────────────────────────────────────────────── */

void i3c_register_bus_ops(const i3c_bus_ops_t *ops)
{
    g_i3c_bus_ops = ops;
    if (ops != NULL) {
        log_write("i3c: controller backend registered");
    }
}

/* ── 初始化 ──────────────────────────────────────────────────────── */

/* Initialize I3C controller */
void i3c_init(void)
{
    memset(&g_i3c_info, 0, sizeof(g_i3c_info));
    memset(g_i3c_devices, 0, sizeof(g_i3c_devices));
    g_i3c_device_count = 0;

    g_i3c_info.available = false;
    g_i3c_info.bus_count = 0;
    g_i3c_info.device_count = 0;
    g_i3c_info.i2c_devices_present = false;
    g_i3c_info.scl_freq = 12500000; /* 12.5 MHz typical I3C SDR */
    g_i3c_info.transfer_count = 0;

    /*
     * I3C 控制器目前没有独立的 PCI 探测路径：本模块工作在"有 I2C 总线即可
     * 提供向下兼容访问"的前提上。真正的 I3C 时序由控制器驱动通过
     * i3c_register_bus_ops() 注入；没有后端时只支持 I2C 兼容设备。
     */
    if (i2c_info()->available) {
        g_i3c_info.available = true;
        g_i3c_info.bus_count = 1;
        g_i3c_info.i2c_devices_present = true;
        if (g_i3c_bus_ops != NULL) {
            strcpy(g_i3c_info.status, "i3c: controller ready (native backend)");
            log_write("i3c: native controller backend present");
        } else {
            strcpy(g_i3c_info.status, "i3c: i2c-compatible mode initialized");
            log_write("i3c: initialized in I2C backward compatibility mode");
        }
    } else {
        strcpy(g_i3c_info.status, "i3c: controller not detected");
        log_write("i3c: no I3C controller found");
    }
}

/* ── 设备表查询 ──────────────────────────────────────────────────── */

/* 在已枚举设备表里按地址查找（动态或静态地址都匹配）。 */
int32_t i3c_find_device(uint8_t addr, i3c_device_t *info)
{
    for (uint8_t i = 0u; i < g_i3c_device_count; i++) {
        const i3c_device_t *d = &g_i3c_devices[i];

        if ((d->dynamic_addr != 0u && d->dynamic_addr == addr) ||
            (d->static_addr != 0u && d->static_addr == addr)) {
            if (info != NULL) {
                memcpy(info, d, sizeof(i3c_device_t));
            }
            return (int32_t) i;
        }
    }
    return -1;
}

/* Probe for an I3C device at given address */
bool i3c_probe(uint8_t bus, uint8_t addr)
{
    (void) bus;

    if (!g_i3c_info.available) {
        strcpy(g_i3c_info.status, "i3c: controller unavailable");
        return false;
    }

    /* 1) 已经在 DAA/枚举表里？直接命中，不必再打扰总线。 */
    if (i3c_find_device(addr, NULL) >= 0) {
        strcpy(g_i3c_info.status, "i3c: device found (enumerated)");
        return true;
    }

    /* 2) I2C 兼容探测（地址在 I2C 合法范围内时）。 */
    if (addr <= I3C_STATIC_ADDR_MAX && i2c_probe(0, addr)) {
        strcpy(g_i3c_info.status, "i3c: device found (i2c mode)");
        return true;
    }

    /* 3) I3C 专有探测：如果已经分配了动态地址，用动态地址再试一次。
     *    真正的地址解析来自 ENTDAA，由 i3c_do_daa() 完成 —— 没有
     *    控制器后端时无法凭空探测到 I3C-only 设备。 */
    if (g_i3c_bus_ops == NULL || g_i3c_bus_ops->daa_read == NULL) {
        strcpy(g_i3c_info.status, "i3c: device not found (no DAA backend)");
        return false;
    }
    if (g_i3c_device_count == 0u) {
        /* 表还是空的：先跑一遍 DAA，再回查。 */
        (void) i3c_do_daa(0u, g_i3c_devices, I3C_MAX_DEVICES);
        if (i3c_find_device(addr, NULL) >= 0) {
            strcpy(g_i3c_info.status, "i3c: device found after DAA");
            return true;
        }
    }

    strcpy(g_i3c_info.status, "i3c: device not found");
    return false;
}

/* Read data from I3C device */
int32_t i3c_read(uint8_t bus, uint8_t addr, uint8_t *buf, uint32_t len)
{
    (void) bus;

    if (!g_i3c_info.available || buf == NULL || len == 0) {
        return -1;
    }

    g_i3c_info.transfer_count++;

    /* Use I2C read for backward compatibility */
    int32_t ret = i2c_read(0, addr, 0, buf, len);
    if (ret >= 0) {
        strcpy(g_i3c_info.status, "i3c: read complete");
        return ret;
    }

    strcpy(g_i3c_info.status, "i3c: read error");
    return -1;
}

/* Write data to I3C device */
int32_t i3c_write(uint8_t bus, uint8_t addr, const uint8_t *buf, uint32_t len)
{
    (void) bus;

    if (!g_i3c_info.available || buf == NULL || len == 0) {
        return -1;
    }

    g_i3c_info.transfer_count++;

    /* Use I2C write for backward compatibility */
    int32_t ret = i2c_write(0, addr, buf[0], buf + 1, len - 1);
    if (ret >= 0) {
        strcpy(g_i3c_info.status, "i3c: write complete");
        return ret;
    }

    strcpy(g_i3c_info.status, "i3c: write error");
    return -1;
}

/* Send CCC (Common Command Code) broadcast */
int32_t i3c_send_ccc(uint8_t bus, uint8_t ccc, const uint8_t *data, uint32_t len)
{
    (void) bus;

    if (!g_i3c_info.available) {
        return -1;
    }

    /* CCC 直写是 I3C 专有帧（广播地址 0x7E + 奇偶），必须由控制器后端发出。 */
    if (g_i3c_bus_ops != NULL && g_i3c_bus_ops->ccc_broadcast != NULL) {
        if (!g_i3c_bus_ops->ccc_broadcast(ccc, data, len)) {
            strcpy(g_i3c_info.status, "i3c: CCC broadcast failed");
            return -1;
        }
        switch (ccc) {
        case I3C_CCC_RSTDAA:
            for (uint8_t i = 0; i < g_i3c_device_count; i++) {
                g_i3c_devices[i].dynamic_addr = 0u;
            }
            strcpy(g_i3c_info.status, "i3c: RSTDAA done");
            return 0;
        case I3C_CCC_ENTDAA:
            return i3c_do_daa(0u, g_i3c_devices, I3C_MAX_DEVICES) >= 0 ? 0 : -1;
        default:
            strcpy(g_i3c_info.status, "i3c: CCC sent");
            return 0;
        }
    }

    /* 没有后端：只有纯软件可完成的 CCC 才声明成功，其余如实报不支持，
     * 不再"模拟成功"以免上层以为硬件真的响应了。 */
    switch (ccc) {
    case I3C_CCC_RSTDAA:
        /* 只清理本地设备表的动态地址；总线上的设备状态未知。 */
        for (uint8_t i = 0; i < g_i3c_device_count; i++) {
            g_i3c_devices[i].dynamic_addr = 0;
        }
        strcpy(g_i3c_info.status, "i3c: local DAA table reset (bus untouched)");
        log_write("i3c: local dynamic address table reset");
        return 0;

    case I3C_CCC_ENTDAA:
        /* 交给 DAA 引擎，它会走兼容枚举或报后端缺失。 */
        return i3c_do_daa(0u, g_i3c_devices, I3C_MAX_DEVICES) >= 0 ? 0 : -1;

    default:
        strcpy(g_i3c_info.status, "i3c: CCC needs I3C controller backend");
        return -1;
    }
}

/* ── ENTDAA ──────────────────────────────────────────────────────── */

/* 无 I3C 后端时的枚举：只用 I2C 找到的设备。
 * 关键：I2C 设备没有动态地址，dynamic_addr 必须保持 0，否则上层会拿一个
 * 伪造的地址去寻址，在真实总线上必然失败。 */
static uint32_t i3c_daa_i2c_enum(uint32_t max_devices)
{
    uint8_t i2c_addrs[128];
    uint32_t i2c_count = i2c_scan(0, i2c_addrs, 128);
    uint32_t found = 0u;

    for (uint32_t i = 0u; i < i2c_count && found < max_devices &&
                         found < I3C_MAX_DEVICES; i++) {
        i3c_device_t *dev = &g_i3c_devices[found];

        memset(dev, 0, sizeof(i3c_device_t));
        dev->static_addr = i2c_addrs[i];
        dev->dynamic_addr = 0u;      /* I2C 设备不使用动态地址 */
        dev->is_i2c = true;
        dev->bcr = 0u;
        dev->dcr = 0u;
        dev->max_read_len = 256u;
        dev->max_write_len = 256u;
        dev->has_ibi = false;
        dev->pid_hi = 0u;
        dev->pid_lo = 0u;
        found++;
    }
    return found;
}

/* Perform DAA (Dynamic Address Assignment) */
int32_t i3c_do_daa(uint8_t bus, i3c_device_t *devices, uint32_t max_devices)
{
    (void) bus;
    uint32_t found = 0u;

    if (!g_i3c_info.available || devices == NULL || max_devices == 0) {
        return -1;
    }

    memset(g_i3c_devices, 0, sizeof(g_i3c_devices));
    g_i3c_device_count = 0u;

    log_write("i3c: starting DAA (Dynamic Address Assignment)...");

    /* 1) RSTDAA 让所有设备回到未分配状态。 */
    if (g_i3c_bus_ops != NULL && g_i3c_bus_ops->ccc_broadcast != NULL) {
        (void) g_i3c_bus_ops->ccc_broadcast(I3C_CCC_RSTDAA, NULL, 0u);
    }

    /* 2) 没有 ENTDAA 后端 → 只能做 I2C 兼容枚举。 */
    if (g_i3c_bus_ops == NULL || g_i3c_bus_ops->daa_read == NULL ||
        g_i3c_bus_ops->daa_write_addr == NULL) {
        found = i3c_daa_i2c_enum(max_devices);
        g_i3c_device_count = (uint8_t) found;
        g_i3c_info.device_count = (uint8_t) found;
        g_i3c_info.i2c_devices_present = found > 0u;
        if (found > 0u) {
            strcpy(g_i3c_info.status, "i3c: i2c-compat devices registered");
            log_write("i3c: DAA skipped (no ENTDAA backend), i2c devices only");
        } else {
            strcpy(g_i3c_info.status, "i3c: no devices on bus");
            log_write("i3c: DAA found no devices");
        }
        memcpy(devices, g_i3c_devices, sizeof(i3c_device_t) * found);
        return (int32_t) found;
    }

    /* 3) 真正的 ENTDAA 循环。
     *    每一轮：读 8 字节设备信息（PID+BCR+DCR），选一个空闲动态地址，
     *    把“7 位地址 | 奇偶位”写回同一帧。已获地址的设备会自动退出仲裁，
     *    因此循环直到后端报告总线空闲为止。 */
    while (found < max_devices && found < I3C_MAX_DEVICES &&
           found < I3C_DAA_MAX_TARGETS) {
        uint8_t info[I3C_DAA_INFO_LEN];
        i3c_device_t *dev;
        uint8_t addr;
        uint8_t addr_byte;

        for (uint8_t b = 0u; b < I3C_DAA_INFO_LEN; b++) {
            info[b] = 0u;
        }
        if (!g_i3c_bus_ops->daa_read(info)) {
            break;   /* 没有更多待分配设备 */
        }

        addr = i3c_pick_dynamic_addr();
        if (addr == 0u) {
            strcpy(g_i3c_info.status, "i3c: no free dynamic address");
            log_write("i3c: DAA aborted, dynamic address space exhausted");
            break;
        }
        addr_byte = i3c_daa_addr_parity(addr);
        if (!g_i3c_bus_ops->daa_write_addr(addr_byte)) {
            strcpy(g_i3c_info.status, "i3c: DAA address write failed");
            log_write("i3c: DAA aborted, address write rejected");
            break;
        }

        dev = &g_i3c_devices[found];
        memset(dev, 0, sizeof(i3c_device_t));
        /* PID 是 48 位、小端 6 字节；BCR/DCR 各 1 字节跟在后面。 */
        dev->pid_lo = (uint32_t) info[0] | ((uint32_t) info[1] << 8) |
                      ((uint32_t) info[2] << 16) | ((uint32_t) info[3] << 24);
        dev->pid_hi = (uint16_t) info[4] | ((uint16_t) info[5] << 8);
        dev->bcr = info[6];
        dev->dcr = info[7];
        dev->dynamic_addr = addr;
        dev->static_addr = 0u;
        dev->is_i2c = false;
        dev->has_ibi = (dev->bcr & I3C_BCR_IBI) != 0u;
        dev->max_read_len = 256u;
        dev->max_write_len = 256u;
        found++;
    }

    g_i3c_device_count = (uint8_t) found;
    g_i3c_info.device_count = (uint8_t) found;

    if (found > 0u) {
        strcpy(g_i3c_info.status, "i3c: DAA complete, devices assigned");
        log_write("i3c: DAA complete - dynamic addresses assigned");
    } else {
        strcpy(g_i3c_info.status, "i3c: DAA complete, no devices");
        log_write("i3c: DAA complete - no devices found");
    }

    memcpy(devices, g_i3c_devices, sizeof(i3c_device_t) * found);
    return (int32_t) found;
}

/* Scan I3C bus for devices */
uint32_t i3c_scan(uint8_t bus, uint8_t *buffer, uint32_t capacity)
{
    (void) bus;
    uint32_t count = 0;

    if (!g_i3c_info.available || buffer == NULL || capacity == 0) {
        return 0;
    }

    log_write("i3c: starting bus scan...");

    /* 先列 I3C 设备的动态地址，再补上 I2C 兼容设备的静态地址。 */
    for (uint8_t i = 0; i < g_i3c_device_count && count < capacity; i++) {
        uint8_t a = g_i3c_devices[i].dynamic_addr;

        if (a != 0u) {
            buffer[count++] = a;
        }
    }
    for (uint8_t i = 0; i < g_i3c_device_count && count < capacity; i++) {
        uint8_t a = g_i3c_devices[i].static_addr;

        if (a != 0u && i3c_find_device(a, NULL) == (int32_t) i) {
            buffer[count++] = a;
        }
    }

    /* 表空时直接借用 I2C 扫描，避免上层拿到空结果。 */
    if (count == 0) {
        count = i2c_scan(0, buffer, capacity);
    }

    if (count > 0) {
        strcpy(g_i3c_info.status, "i3c: scan complete");
        log_write("i3c: scan complete - devices found");
    } else {
        strcpy(g_i3c_info.status, "i3c: scan empty");
    }

    return count;
}

/* Get device info by index */
int32_t i3c_get_device_info(uint8_t index, i3c_device_t *info)
{
    if (info == NULL || index >= g_i3c_device_count) {
        return -1;
    }

    memcpy(info, &g_i3c_devices[index], sizeof(i3c_device_t));
    return 0;
}

/* Set I3C bus frequency */
int32_t i3c_set_frequency(uint8_t bus, uint32_t freq_hz)
{
    (void) bus;

    if (!g_i3c_info.available) {
        return -1;
    }

    if (freq_hz == 0 || freq_hz > 12500000) { /* Max 12.5 MHz SDR */
        strcpy(g_i3c_info.status, "i3c: invalid frequency");
        return -1;
    }

    g_i3c_info.scl_freq = freq_hz;
    strcpy(g_i3c_info.status, "i3c: frequency updated");
    log_write("i3c: SCL frequency updated");
    return 0;
}

const i3c_info_t *i3c_info(void)
{
    return &g_i3c_info;
}

const char *i3c_status(void)
{
    return g_i3c_info.status;
}
