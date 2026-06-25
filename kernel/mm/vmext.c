#include "common.h"
#include "lazyalloc.h"
#include "mmu.h"
#include "vma.h"
#include "vmext.h"

static vmext_info_t g_vmext_info;

void vmext_refresh(void)
{
    g_vmext_info.paging_active = mmu_is_active() != 0;
    g_vmext_info.vma_ready = true;
    g_vmext_info.lazy_ready = true;
    g_vmext_info.vma_regions = vma_count();
    g_vmext_info.lazy_regions = lazyalloc_count();
    g_vmext_info.pml4_phys = mmu_get_pml4_phys();
    strcpy(g_vmext_info.status, g_vmext_info.paging_active ? "vmext: paging/vma/lazyalloc active" : "vmext: paging not active");
}

void vmext_init(void)
{
    memset(&g_vmext_info, 0, sizeof(g_vmext_info));
    g_vmext_info.initialized = true;
    vmext_refresh();
}

const vmext_info_t *vmext_info(void)
{
    vmext_refresh();
    return &g_vmext_info;
}

const char *vmext_status(void)
{
    vmext_refresh();
    return g_vmext_info.status;
}
