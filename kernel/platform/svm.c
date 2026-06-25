#include "common.h"
#include "cpu.h"
#include "kernel.h"

/* AMD SVM (Secure Virtual Machine) 支持 */

#define SVM_MSR_VM_CR               0xC0010114
#define SVM_MSR_VM_HSAVE_PA         0xC0010117

#define SVM_VM_CR_SVM_DISABLE       (1 << 4)
#define SVM_VM_CR_SVME_DISABLE      (1 << 12)

typedef struct {
    bool present;
    bool enabled;
    uint32_t vmcb_size;
    uint64_t vm_cr;
} svm_info_t;

static svm_info_t g_svm_info;

bool svm_supported(void)
{
    const cpu_info_t *cpu = cpu_current_info();
    return cpu->has_svm;
}

bool svm_init(void)
{
    const cpu_info_t *cpu = cpu_current_info();
    uint64_t vm_cr;
    uint64_t efer;

    if (!cpu->has_svm) {
        g_svm_info.present = false;
        return false;
    }

    g_svm_info.present = true;

    /* 读取 VM_CR MSR */
    vm_cr = cpu_read_msr(SVM_MSR_VM_CR);
    g_svm_info.vm_cr = vm_cr;

    /* 检查 SVM 是否被禁用 */
    if (vm_cr & SVM_VM_CR_SVM_DISABLE) {
        log_write("svm: disabled by BIOS");
        g_svm_info.enabled = false;
        return false;
    }

    /* 启用 EFER 中的 SVME 位 */
    efer = cpu_read_msr(IA32_EFER_MSR);
    efer |= EFER_SVME;
    cpu_write_msr(IA32_EFER_MSR, efer);

    g_svm_info.enabled = true;
    log_write("svm: AMD SVM virtualization enabled");

    return true;
}

const svm_info_t *svm_info(void)
{
    return &g_svm_info;
}

bool svm_is_enabled(void)
{
    return g_svm_info.enabled;
}
