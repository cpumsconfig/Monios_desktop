#include "common.h"
#include "pci.h"
#include "storage_ext.h"

static storage_ext_info_t g_storage_info;

static bool storage_probe_cb(const pci_device_info_t *info, void *ctx)
{
    storage_ext_info_t *st = (storage_ext_info_t *) ctx;

    if (info->class_code != 0x01) {
        return true;
    }
    if (info->subclass == 0x01) {
        st->ide_controllers++;
    } else if (info->subclass == 0x06) {
        st->sata_controllers++;
    } else if (info->subclass == 0x08) {
        st->nvme_controllers++;
    } else if (info->subclass == 0x00) {
        st->scsi_controllers++;
    } else if (info->subclass == 0x04) {
        st->raid_controllers++;
    } else {
        st->other_storage++;
    }
    return true;
}

void storage_ext_init(void)
{
    memset(&g_storage_info, 0, sizeof(g_storage_info));
    g_storage_info.initialized = true;
    pci_enumerate(storage_probe_cb, &g_storage_info);
    strcpy(g_storage_info.status, "storagex: pci storage inventory ready");
}

const storage_ext_info_t *storage_ext_info(void)
{
    return &g_storage_info;
}

const char *storage_ext_status(void)
{
    return g_storage_info.status;
}
