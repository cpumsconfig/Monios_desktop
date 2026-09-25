/*
 * template_char.c - 字符设备驱动模板（Task 27a）。
 *
 * 这是一个最小但完整的 Monios .sys 字符设备驱动骨架：
 *   - DriverEntry  : 内核加载驱动时调用，做初始化（探测硬件、注册设备）。
 *   - DriverUnload : 内核卸载驱动时调用，释放资源、注销设备。
 *   - DriverTick   : 可选，周期性心跳（runtime 会周期性回调）。
 *
 * 编译产物是 PE 格式的 .sys 文件，入口为 DriverEntry，需经 sign_driver_sys.py
 * 签名后才能被 driver_manager 加载。
 *
 * 真实字符设备示例参考：drivers/usb/hid.c、drivers/input/*。
 */

#include "template.h"

/* 模块级日志函数（指向 runtime 提供的实现）。 */
monios_driver_log_fn_t template_log = 0;

/* 全局设备上下文（模板只支持一个实例；复杂驱动可维护链表）。 */
static template_device_t g_dev;
static bool g_loaded = false;

/*
 * DriverEntry - 驱动入口。
 * 参数 runtime : 内核提供的运行时回调表（日志/分配内存/释放内存）。
 * 返回 true 表示加载成功，false 表示失败（内核会拒绝该驱动）。
 */
__declspec(dllexport)
bool DriverEntry(const monios_driver_runtime_t *runtime)
{
    /* 1. 校验 ABI 版本，防止加载不兼容的驱动。 */
    if (runtime == 0 ||
        runtime->abi_version != MONIOS_DRIVER_ABI_VERSION ||
        runtime->log == 0) {
        return false;
    }

    /* 2. 保存日志函数指针，后续即可用 TPL_LOG 输出。 */
    template_log = runtime->log;
    TPL_LOG("template-char: DriverEntry begin");

    /* 3. 初始化设备上下文。 */
    g_dev.io_base   = 0x200;   /* TODO: 改成你设备的实际端口/MMIO 基址 */
    g_dev.irq       = 0;       /* TODO: 注册中断后填写；0=轮询模式 */
    g_dev.opened    = false;
    g_dev.read_ops  = 0;
    g_dev.write_ops = 0;

    /* 4. TODO: 硬件探测 / 资源申请 / 注册字符设备节点。
     *    例如：读设备 ID 寄存器确认芯片存在、申请 IO 端口、注册中断处理。
     *    探测失败应 return false，让内核清理。 */

    g_loaded = true;
    TPL_LOG("template-char: online");
    return true;
}

/*
 * DriverTick - 可选的周期性回调。
 * 参数 now_ticks : 当前内核 tick 数。
 * 用于轮询硬件状态、喂看门狗、做健康检查。
 */
__declspec(dllexport)
void DriverTick(uint64_t now_ticks)
{
    if (!g_loaded) return;
    /* TODO: 周期性工作，例如超时 300 tick 做一次轮询。 */
    (void)now_ticks;
}

/*
 * DriverUnload - 驱动卸载入口。
 * 必须在这里释放所有申请的资源（端口/中断/内存），否则会泄漏。
 */
__declspec(dllexport)
void DriverUnload(void)
{
    if (!g_loaded) return;
    TPL_LOG("template-char: offline");

    /* TODO: 注销设备节点、释放 IO 端口、断开中断、释放已分配内存。 */

    template_log = 0;
    g_loaded = false;
}
