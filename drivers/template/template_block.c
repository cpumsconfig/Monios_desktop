/*
 * template_block.c - 块设备驱动模板（Task 27a）。
 *
 * 与字符设备的区别：块设备按扇区（通常 512/4096 字节）寻址，服务于文件系统。
 * 真实块设备示例参考：drivers/storage/ide.c、ahci.c、nvme.c、virtio_blk.c。
 *
 * 本骨架演示：探测磁盘参数、读写扇区的 IO 请求处理框架。
 */

#include "template.h"

/* 扇区大小（字节）。 */
#define TPL_SECTOR_SIZE   512u

monios_driver_log_fn_t template_log = 0;

typedef struct {
    template_device_t base;
    uint64_t  sector_count;   /* 总扇区数 */
    uint32_t  sector_size;    /* 通常 512 */
} tpl_block_dev_t;

static tpl_block_dev_t g_blk;
static bool g_loaded = false;

/*
 * block_read - 读扇区（模板示例，实际驱动需操作硬件/DMA）。
 * 参数 lba     : 起始扇区逻辑地址
 * 参数 count   : 要读的扇区数
 * 参数 buffer  : 目标缓冲区（内核虚拟地址）
 * 返回 true 成功。
 */
static bool block_read(uint64_t lba, uint32_t count, void *buffer)
{
    /* TODO: 向设备发出读命令，等待完成中断，把数据 DMA 到 buffer。 */
    (void)lba; (void)count; (void)buffer;
    g_blk.base.read_ops++;
    return true;
}

/*
 * block_write - 写扇区。 */
static bool block_write(uint64_t lba, uint32_t count, const void *buffer)
{
    /* TODO: 发出写命令并等待完成。 */
    (void)lba; (void)count; (void)buffer;
    g_blk.base.write_ops++;
    return true;
}

__declspec(dllexport)
bool DriverEntry(const monios_driver_runtime_t *runtime)
{
    if (runtime == 0 ||
        runtime->abi_version != MONIOS_DRIVER_ABI_VERSION ||
        runtime->log == 0) {
        return false;
    }
    template_log = runtime->log;
    TPL_LOG("template-block: DriverEntry begin");

    /* 初始化设备。 */
    g_blk.base.io_base = 0x1F0;       /* TODO: 改成实际控制器端口 */
    g_blk.base.irq     = 14;          /* IDE 主通道示例 */
    g_blk.sector_count = 0;
    g_blk.sector_size  = TPL_SECTOR_SIZE;

    /* TODO: 识别磁盘、读取容量（IDENTIFY 命令）、向块层注册磁盘。 */

    g_loaded = true;
    TPL_LOG("template-block: online");
    return true;
}

__declspec(dllexport)
void DriverTick(uint64_t now_ticks)
{
    if (!g_loaded) return;
    (void)now_ticks;
    /* TODO: 处理已完成的 IO 请求队列、超时重试。 */
}

__declspec(dllexport)
void DriverUnload(void)
{
    if (!g_loaded) return;
    TPL_LOG("template-block: offline");
    /* TODO: 注销块设备、flush 写缓存、释放 DMA 缓冲区。 */
    template_log = 0;
    g_loaded = false;
}
