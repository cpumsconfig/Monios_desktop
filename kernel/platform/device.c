#include "ahci.h"
#include "aac.h"
#include "bios.h"
#include "bitmap.h"
#include "bluetooth.h"
#include "browser.h"
#include "buddy.h"
#include "cdrom.h"
#include "common.h"
#include "console.h"
#include "device.h"
#include "eevdf.h"
#include "extfs.h"
#include "file.h"
#include "frame.h"
#include "fs_cache.h"
#include "futex.h"
#include "gop.h"
#include "gpu.h"
#include "graphics.h"
#include "gui.h"
#include "hda.h"
#include "heap.h"
#include "hid.h"
#include "i2c.h"
#include "i3c.h"
#include "iic.h"
#include "ide.h"
#include "ipc.h"
#include "iso9660.h"
#include "ipv6.h"
#include "keyboard.h"
#include "lazyalloc.h"
#include "kernel.h"
#include "lwip.h"
#include "mcb.h"
#include "md.h"
#include "http.h"
#include "muqss.h"
#include "net.h"
#include "ntfs.h"
#include "nvme.h"
#include "od.h"
#include "opp.h"
#include "pcb.h"
#include "pcnet.h"
#include "power.h"
#include "pool.h"
#include "prsys.h"
#include "rtc.h"
#include "scheduler.h"
#include "schedopt.h"
#include "signal.h"
#include "smp.h"
#include "spi.h"
#include "storage_ext.h"
#include "tls.h"
#include "terminal.h"
#include "tpm.h"
#include "usb_ext.h"
#include "vma.h"
#include "vmext.h"
#include "wifi.h"
#include "xhci.h"

typedef struct {
    const char *name;
    const char *kind;
    bool readable;
    bool writable;
} device_entry_t;

static const device_entry_t g_devices[] = {
    { "console", "char", true, true },
    { "null", "char", true, true },
    { "zero", "char", true, false },
    { "kbd", "char", true, false },
    { "net", "net", true, false },
    { "lwip", "net", true, false },
    { "ide", "storage", true, false },
    { "ahci", "storage", true, false },
    { "nvme", "storage", true, false },
    { "cdrom", "storage", true, false },
    { "ntfs", "fs", true, false },
    { "iso9660", "fs", true, false },
    { "extfs", "fs", true, false },
    { "fscache", "fs", true, false },
    { "pcnet", "net", true, false },
    { "ipv6", "net", true, false },
    { "tls", "net", true, false },
    { "http", "net", true, false },
    { "wifi", "net", true, false },
    { "xhci", "usb", true, false },
    { "usbext", "usb", true, false },
    { "hid", "input", true, false },
    { "bth", "wireless", true, false },
    { "hda", "audio", true, false },
    { "aac", "audio", true, false },
    { "iic", "bus", true, false },
    { "i2c", "bus", true, false },
    { "i3c", "bus", true, false },
    { "spi", "bus", true, false },
    { "tpm", "security", true, false },
    { "mcb", "memory", true, false },
    { "md", "storage", true, false },
    { "opp", "power", true, false },
    { "od", "power", true, false },
    { "bios", "firmware", true, false },
    { "gop", "graphics", true, false },
    { "rtc", "clock", true, false },
    { "heap", "memory", true, false },
    { "frame", "memory", true, false },
    { "vma", "memory", true, false },
    { "lazyalloc", "memory", true, false },
    { "bitmap", "memory", true, false },
    { "buddy", "memory", true, false },
    { "scheduler", "kernel", true, false },
    { "eevdf", "kernel", true, false },
    { "muqss", "kernel", true, false },
    { "pcb", "kernel", true, false },
    { "pool", "kernel", true, false },
    { "prsys", "kernel", true, false },
    { "futex", "sync", true, false },
    { "ipc", "ipc", true, false },
    { "signal", "signal", true, false },
    { "smp", "cpu", true, false },
    { "schedopt", "cpu", true, false },
    { "vmext", "memory", true, false },
    { "term", "tty", true, false },
    { "storagex", "storage", true, false },
    { "power", "power", true, false },
    { "gpu", "graphics", true, false },
    { "gui", "gui", true, false },
    { "browser", "net", true, false },
    { "wm", "gui", true, false },
    { "fb", "graphics", true, false }
};

static const char *device_normalize(const char *name)
{
    if (name == NULL) {
        return NULL;
    }
    if (name[0] == '\\' && name[1] == '\\' && name[2] == '.' && name[3] == '\\') {
        return name + 4;
    }
    if (name[0] == '/' && name[1] == 'd' && name[2] == 'e' && name[3] == 'v' && name[4] == '/') {
        return name + 5;
    }
    return name;
}

static const device_entry_t *device_find(const char *name)
{
    const char *normalized = device_normalize(name);

    if (normalized == NULL) {
        return NULL;
    }
    for (uint32_t i = 0; i < sizeof(g_devices) / sizeof(g_devices[0]); i++) {
        if (strcmp(normalized, g_devices[i].name) == 0) {
            return &g_devices[i];
        }
    }
    return NULL;
}

static void device_append(char *buffer, uint32_t size, const char *text)
{
    uint32_t len;
    uint32_t add;

    if (buffer == NULL || size == 0 || text == NULL) {
        return;
    }
    len = (uint32_t) strlen(buffer);
    add = (uint32_t) strlen(text);
    if (len + add + 1 >= size) {
        return;
    }
    strcpy(buffer + len, text);
}

static void device_append_u32(char *buffer, uint32_t size, uint32_t value)
{
    char temp[10];
    char out[11];
    uint32_t i = 0;
    uint32_t j = 0;

    if (value == 0) {
        device_append(buffer, size, "0");
        return;
    }
    while (value > 0 && i < sizeof(temp)) {
        temp[i++] = (char) ('0' + (value % 10));
        value /= 10;
    }
    while (i > 0) {
        out[j++] = temp[--i];
    }
    out[j] = '\0';
    device_append(buffer, size, out);
}

static void device_append_i32(char *buffer, uint32_t size, int32_t value)
{
    uint32_t magnitude;

    if (value < 0) {
        device_append(buffer, size, "-");
        magnitude = (uint32_t) (-(value + 1)) + 1u;
    } else {
        magnitude = (uint32_t) value;
    }
    device_append_u32(buffer, size, magnitude);
}

static void device_append_u64(char *buffer, uint32_t size, uint64_t value)
{
    char temp[20];
    char out[21];
    uint32_t i = 0;
    uint32_t j = 0;

    if (value == 0) {
        device_append(buffer, size, "0");
        return;
    }
    while (value > 0 && i < sizeof(temp)) {
        temp[i++] = (char) ('0' + (value % 10));
        value /= 10;
    }
    while (i > 0) {
        out[j++] = temp[--i];
    }
    out[j] = '\0';
    device_append(buffer, size, out);
}

static void device_append_hex_u32(char *buffer, uint32_t size, uint32_t value)
{
    static const char hex[] = "0123456789ABCDEF";
    char out[11];

    out[0] = '0';
    out[1] = 'x';
    for (uint32_t i = 0; i < 8; i++) {
        out[2 + i] = hex[(value >> ((7 - i) * 4)) & 0xF];
    }
    out[10] = '\0';
    device_append(buffer, size, out);
}

void device_init(void)
{
    log_write("device: \\\\.\\ namespace ready");
}

bool device_exists(const char *name)
{
    return device_find(name) != NULL;
}

bool device_list(char *buffer, uint32_t size)
{
    if (buffer == NULL || size == 0) {
        return false;
    }
    buffer[0] = '\0';
    for (uint32_t i = 0; i < sizeof(g_devices) / sizeof(g_devices[0]); i++) {
        device_append(buffer, size, "\\\\.\\");
        device_append(buffer, size, g_devices[i].name);
        device_append(buffer, size, " ");
        device_append(buffer, size, g_devices[i].kind);
        device_append(buffer, size, g_devices[i].readable ? " r" : " -");
        device_append(buffer, size, g_devices[i].writable ? "w\n" : "-\n");
    }
    return true;
}

int32_t device_read(const char *name, char *buffer, uint32_t size)
{
    const device_entry_t *dev = device_find(name);

    if (dev == NULL || !dev->readable || buffer == NULL || size == 0) {
        return -1;
    }
    buffer[0] = '\0';
    if (strcmp(dev->name, "null") == 0) {
        return 0;
    }
    if (strcmp(dev->name, "zero") == 0) {
        memset(buffer, 0, size);
        return (int32_t) size;
    }
    if (strcmp(dev->name, "kbd") == 0) {
        char ch;
        if (keyboard_read_char(&ch)) {
            buffer[0] = ch;
            return 1;
        }
        return 0;
    }
    if (strcmp(dev->name, "net") == 0) {
        const net_info_t *info = net_info();
        device_append(buffer, size, net_status());
        device_append(buffer, size, "\nmac ");
        device_append(buffer, size, info->mac_text);
        device_append(buffer, size, "\nip ");
        device_append(buffer, size, info->ip_text);
        device_append(buffer, size, "\ngateway ");
        device_append(buffer, size, info->gateway_text);
        device_append(buffer, size, "\n");
        return (int32_t) strlen(buffer);
    }
    if (strcmp(dev->name, "lwip") == 0) {
        const lwip_info_t *info = lwip_info();

        device_append(buffer, size, lwip_status());
        device_append(buffer, size, "\nnetif ");
        device_append(buffer, size, info->netif_up ? "up" : "down");
        device_append(buffer, size, "\nip ");
        device_append(buffer, size, info->ip);
        device_append(buffer, size, "\nudp ");
        device_append_u32(buffer, size, info->udp_sockets);
        device_append(buffer, size, "\ndhcp ");
        device_append(buffer, size, info->dhcp_configured ? "yes" : "no");
        device_append(buffer, size, "\ndns ");
        device_append(buffer, size, info->dns_configured ? "yes" : "no");
        device_append(buffer, size, "\ntx ");
        device_append_u32(buffer, size, info->tx_packets);
        device_append(buffer, size, "\nrx ");
        device_append_u32(buffer, size, info->rx_packets);
        device_append(buffer, size, "\n");
        return (int32_t) strlen(buffer);
    }
    if (strcmp(dev->name, "ide") == 0) {
        const ide_info_t *info = ide_info();
        device_append(buffer, size, ide_status());
        if (info->present) {
            device_append(buffer, size, "\nmodel ");
            device_append(buffer, size, info->model);
        }
        device_append(buffer, size, "\n");
        return (int32_t) strlen(buffer);
    }
    if (strcmp(dev->name, "ahci") == 0) {
        const ahci_info_t *info = ahci_info();
        device_append(buffer, size, ahci_status());
        device_append(buffer, size, "\n");
        return (int32_t) strlen(buffer);
    }
    if (strcmp(dev->name, "nvme") == 0) {
        const nvme_info_t *info = nvme_info();

        device_append(buffer, size, nvme_status());
        if (info->present) {
            device_append(buffer, size, "\nmmio ");
            device_append_hex_u32(buffer, size, info->mmio_base);
            device_append(buffer, size, "\nversion ");
            device_append_hex_u32(buffer, size, info->version);
            device_append(buffer, size, "\nqueues ");
            device_append_u32(buffer, size, info->max_queue_entries);
        }
        device_append(buffer, size, "\n");
        return (int32_t) strlen(buffer);
    }
    if (strcmp(dev->name, "cdrom") == 0) {
        const cdrom_info_t *info = cdrom_info();

        device_append(buffer, size, cdrom_status());
        device_append(buffer, size, "\npresent ");
        device_append(buffer, size, info->present ? "yes" : "no");
        device_append(buffer, size, "\nready ");
        device_append(buffer, size, info->ready ? "yes" : "no");
        device_append(buffer, size, "\nsectors ");
        device_append_u32(buffer, size, info->total_sectors);
        device_append(buffer, size, "\nsector_size ");
        device_append_u32(buffer, size, info->sector_size);
        if (info->present) {
            device_append(buffer, size, "\nproduct ");
            device_append(buffer, size, info->product);
        }
        device_append(buffer, size, "\n");
        return (int32_t) strlen(buffer);
    }
    if (strcmp(dev->name, "ntfs") == 0) {
        const ntfs_info_t *info = ntfs_info();

        device_append(buffer, size, ntfs_status());
        if (info->present) {
            device_append(buffer, size, "\nlba ");
            device_append_u32(buffer, size, info->volume_lba);
            device_append(buffer, size, "\nsector ");
            device_append_u32(buffer, size, info->bytes_per_sector);
            device_append(buffer, size, "\nclustersz ");
            device_append_u32(buffer, size, (uint32_t) info->bytes_per_sector * info->sectors_per_cluster);
            device_append(buffer, size, "\nsectors ");
            device_append_u64(buffer, size, info->total_sectors);
            device_append(buffer, size, "\nmft ");
            device_append_u64(buffer, size, info->mft_lcn);
            device_append(buffer, size, "\nmft_record ");
            device_append_u32(buffer, size, info->mft_record_size);
            device_append(buffer, size, "\nindex_record ");
            device_append_u32(buffer, size, info->index_record_size);
            device_append(buffer, size, "\nmft0 ");
            device_append(buffer, size, info->mft0_readable ? "readable" : "unreadable");
        }
        device_append(buffer, size, "\n");
        return (int32_t) strlen(buffer);
    }
    if (strcmp(dev->name, "iso9660") == 0) {
        const iso9660_info_t *info = iso9660_info();

        device_append(buffer, size, iso9660_status());
        device_append(buffer, size, "\npresent ");
        device_append(buffer, size, info->present ? "yes" : "no");
        device_append(buffer, size, "\nready ");
        device_append(buffer, size, info->ready ? "yes" : "no");
        device_append(buffer, size, "\nblocks ");
        device_append_u32(buffer, size, info->total_blocks);
        device_append(buffer, size, "\nblock ");
        device_append_u32(buffer, size, info->block_size);
        if (info->volume_id[0] != '\0') {
            device_append(buffer, size, "\nvolume ");
            device_append(buffer, size, info->volume_id);
        }
        device_append(buffer, size, "\n");
        return (int32_t) strlen(buffer);
    }
    if (strcmp(dev->name, "fscache") == 0) {
        const fs_cache_info_t *info = fs_cache_info();

        device_append(buffer, size, fs_cache_status());
        device_append(buffer, size, "\nbackend ");
        device_append(buffer, size, file_backend_name());
        device_append(buffer, size, "\nslots ");
        device_append_u32(buffer, size, info->slots);
        device_append(buffer, size, "\nblock ");
        device_append_u32(buffer, size, info->block_size);
        device_append(buffer, size, "\nhits ");
        device_append_u32(buffer, size, info->hits);
        device_append(buffer, size, "\nmisses ");
        device_append_u32(buffer, size, info->misses);
        device_append(buffer, size, "\n");
        return (int32_t) strlen(buffer);
    }
    if (strcmp(dev->name, "extfs") == 0) {
        const extfs_info_t *info = extfs_info();

        device_append(buffer, size, extfs_status());
        if (info->present) {
            device_append(buffer, size, "\nlba ");
            device_append_u32(buffer, size, info->volume_lba);
            device_append(buffer, size, "\nblock ");
            device_append_u32(buffer, size, info->block_size);
            device_append(buffer, size, "\nblocks ");
            device_append_u32(buffer, size, info->blocks_count);
            device_append(buffer, size, "\ninodes ");
            device_append_u32(buffer, size, info->inodes_count);
            device_append(buffer, size, "\ninode_size ");
            device_append_u32(buffer, size, info->inode_size);
        }
        device_append(buffer, size, "\n");
        return (int32_t) strlen(buffer);
    }
    if (strcmp(dev->name, "pcnet") == 0) {
        const pcnet_info_t *info = pcnet_info();
        device_append(buffer, size, pcnet_status());
        if (info->present) {
            device_append(buffer, size, "\nmac ");
            device_append(buffer, size, info->mac_text);
        }
        device_append(buffer, size, "\n");
        return (int32_t) strlen(buffer);
    }
    if (strcmp(dev->name, "ipv6") == 0) {
        const ipv6_info_t *info = ipv6_info();

        device_append(buffer, size, ipv6_status());
        device_append(buffer, size, "\nparsed ");
        device_append_u32(buffer, size, info->parsed_count);
        device_append(buffer, size, "\nlinklocal ");
        device_append(buffer, size, info->link_local);
        device_append(buffer, size, "\n");
        return (int32_t) strlen(buffer);
    }
    if (strcmp(dev->name, "tls") == 0) {
        const tls_info_t *info = tls_info();

        device_append(buffer, size, tls_status());
        device_append(buffer, size, "\nrecord ");
        device_append(buffer, size, info->record_layer ? "yes" : "no");
        device_append(buffer, size, "\nx509 ");
        device_append(buffer, size, info->x509_parser ? "yes" : "no");
        device_append(buffer, size, "\ncrypto ");
        device_append(buffer, size, info->crypto_backend_ready ? "yes" : "pending");
        device_append(buffer, size, "\n");
        return (int32_t) strlen(buffer);
    }
    if (strcmp(dev->name, "http") == 0) {
        const http_info_t *info = http_info();

        device_append(buffer, size, http_status());
        device_append(buffer, size, "\nrequests ");
        device_append_u32(buffer, size, info->requests_built);
        device_append(buffer, size, "\nhttps ");
        device_append_u32(buffer, size, info->https_probes);
        device_append(buffer, size, "\n");
        return (int32_t) strlen(buffer);
    }
    if (strcmp(dev->name, "wifi") == 0) {
        const wifi_info_t *info = wifi_info();

        device_append(buffer, size, wifi_status());
        if (info->present) {
            device_append(buffer, size, "\nvid ");
            device_append_hex_u32(buffer, size, ((uint32_t) info->vendor_id << 16) | info->device_id);
        }
        device_append(buffer, size, "\n");
        return (int32_t) strlen(buffer);
    }
    if (strcmp(dev->name, "xhci") == 0) {
        const xhci_info_t *info = xhci_info();

        device_append(buffer, size, xhci_status());
        if (info->present) {
            device_append(buffer, size, "\nmmio ");
            device_append_hex_u32(buffer, size, info->mmio_base);
            device_append(buffer, size, "\nhci ");
            device_append_hex_u32(buffer, size, info->hci_version);
            device_append(buffer, size, "\nports ");
            device_append_u32(buffer, size, info->max_ports);
            device_append(buffer, size, "\nslots ");
            device_append_u32(buffer, size, info->max_slots);
        }
        device_append(buffer, size, "\n");
        return (int32_t) strlen(buffer);
    }
    if (strcmp(dev->name, "usbext") == 0) {
        const usb_ext_info_t *info = usb_ext_info();

        device_append(buffer, size, usb_ext_status());
        device_append(buffer, size, "\nlegacy ");
        device_append(buffer, size, info->legacy_ready ? "yes" : "no");
        device_append(buffer, size, "\nnative ");
        device_append(buffer, size, info->native_host_present ? "yes" : "no");
        device_append(buffer, size, "\nports ");
        device_append_u32(buffer, size, info->root_ports);
        device_append(buffer, size, "\n");
        return (int32_t) strlen(buffer);
    }
    if (strcmp(dev->name, "hid") == 0) {
        const hid_info_t *info = hid_info();

        device_append(buffer, size, hid_status());
        device_append(buffer, size, "\nlegacy_kbd ");
        device_append(buffer, size, info->legacy_keyboard ? "yes" : "no");
        device_append(buffer, size, "\nlegacy_mouse ");
        device_append(buffer, size, info->legacy_mouse ? "yes" : "no");
        device_append(buffer, size, "\nxhci ");
        device_append(buffer, size, info->xhci_present ? "yes" : "no");
        device_append(buffer, size, "\nkeys ");
        device_append_u32(buffer, size, info->key_events_seen);
        device_append(buffer, size, "\nmouse ");
        device_append_i32(buffer, size, info->mouse_x);
        device_append(buffer, size, ",");
        device_append_i32(buffer, size, info->mouse_y);
        device_append(buffer, size, "\n");
        return (int32_t) strlen(buffer);
    }
    if (strcmp(dev->name, "bth") == 0) {
        const bluetooth_info_t *info = bluetooth_info();

        device_append(buffer, size, bluetooth_status());
        device_append(buffer, size, "\ntransport ");
        device_append(buffer, size, info->usb_transport_ready ? "usb" : "none");
        device_append(buffer, size, "\ncontrollers ");
        device_append_u32(buffer, size, info->controllers);
        device_append(buffer, size, "\n");
        return (int32_t) strlen(buffer);
    }
    if (strcmp(dev->name, "hda") == 0) {
        device_append(buffer, size, hda_status());
        device_append(buffer, size, "\n");
        return (int32_t) strlen(buffer);
    }
    if (strcmp(dev->name, "aac") == 0) {
        const aac_info_t *info = aac_last_info();

        device_append(buffer, size, aac_status());
        if (info->valid) {
            device_append(buffer, size, "\ncodec ");
            device_append(buffer, size, info->codec);
            device_append(buffer, size, "\nrate ");
            device_append_u32(buffer, size, info->sample_rate);
            device_append(buffer, size, "\nchannels ");
            device_append_u32(buffer, size, info->channels);
            device_append(buffer, size, "\nframes ");
            device_append_u32(buffer, size, info->estimated_frames);
        }
        device_append(buffer, size, "\n");
        return (int32_t) strlen(buffer);
    }
    if (strcmp(dev->name, "iic") == 0) {
        const iic_info_t *info = iic_info();

        device_append(buffer, size, iic_status());
        device_append(buffer, size, "\nadapters ");
        device_append_u32(buffer, size, info->adapters);
        device_append(buffer, size, "\nfound ");
        device_append_u32(buffer, size, info->found_count);
        device_append(buffer, size, "\n");
        return (int32_t) strlen(buffer);
    }
    if (strcmp(dev->name, "i2c") == 0) {
        const i2c_info_t *info = i2c_info();

        device_append(buffer, size, i2c_status());
        device_append(buffer, size, "\navailable ");
        device_append(buffer, size, info->available ? "yes" : "no");
        device_append(buffer, size, "\nbuses ");
        device_append_u32(buffer, size, info->bus_count);
        device_append(buffer, size, "\nspeed ");
        device_append_u32(buffer, size, info->bus_speed);
        device_append(buffer, size, "\ntransfers ");
        device_append_u32(buffer, size, info->transfer_count);
        device_append(buffer, size, "\nerrors ");
        device_append_u32(buffer, size, info->error_count);
        device_append(buffer, size, "\n");
        return (int32_t) strlen(buffer);
    }
    if (strcmp(dev->name, "i3c") == 0) {
        const i3c_info_t *info = i3c_info();

        device_append(buffer, size, i3c_status());
        device_append(buffer, size, "\navailable ");
        device_append(buffer, size, info->available ? "yes" : "no");
        device_append(buffer, size, "\nbuses ");
        device_append_u32(buffer, size, info->bus_count);
        device_append(buffer, size, "\ndevices ");
        device_append_u32(buffer, size, info->device_count);
        device_append(buffer, size, "\nscl ");
        device_append_u32(buffer, size, info->scl_freq);
        device_append(buffer, size, "\n");
        return (int32_t) strlen(buffer);
    }
    if (strcmp(dev->name, "spi") == 0) {
        const spi_info_t *info = spi_info();

        device_append(buffer, size, spi_status());
        device_append(buffer, size, "\navailable ");
        device_append(buffer, size, info->available ? "yes" : "no");
        device_append(buffer, size, "\nbuses ");
        device_append_u32(buffer, size, info->bus_count);
        device_append(buffer, size, "\nspeed ");
        device_append_u32(buffer, size, info->current_speed);
        device_append(buffer, size, "\nmode ");
        device_append_u32(buffer, size, info->mode);
        device_append(buffer, size, "\ntransfers ");
        device_append_u32(buffer, size, info->transfer_count);
        device_append(buffer, size, "\n");
        return (int32_t) strlen(buffer);
    }
    if (strcmp(dev->name, "tpm") == 0) {
        const tpm_info_t *info = tpm_info();

        device_append(buffer, size, tpm_status());
        device_append(buffer, size, "\npresent ");
        device_append(buffer, size, info->present ? "yes" : "no");
        device_append(buffer, size, "\nready ");
        device_append(buffer, size, info->ready ? "yes" : "no");
        device_append(buffer, size, "\ninterface ");
        device_append_u32(buffer, size, info->interface_type);
        device_append(buffer, size, "\nvendor ");
        device_append(buffer, size, info->vendor_name);
        device_append(buffer, size, "\ncommands ");
        device_append_u32(buffer, size, info->command_count);
        device_append(buffer, size, "\n");
        return (int32_t) strlen(buffer);
    }
    if (strcmp(dev->name, "mcb") == 0) {
        const mcb_info_t *info = mcb_info();

        device_append(buffer, size, mcb_status());
        device_append(buffer, size, "\npresent ");
        device_append(buffer, size, info->present ? "yes" : "no");
        device_append(buffer, size, "\nchannels ");
        device_append_u32(buffer, size, info->channel_count);
        device_append(buffer, size, "\ndimms ");
        device_append_u32(buffer, size, info->dimm_count);
        device_append(buffer, size, "\nsize_mb ");
        device_append_u32(buffer, size, info->total_size_mb);
        device_append(buffer, size, "\nclock ");
        device_append_u32(buffer, size, info->clock_mhz);
        device_append(buffer, size, "\n");
        return (int32_t) strlen(buffer);
    }
    if (strcmp(dev->name, "md") == 0) {
        const md_info_t *info = md_info();

        device_append(buffer, size, md_status());
        device_append(buffer, size, "\narrays ");
        device_append_u32(buffer, size, info->array_count);
        device_append(buffer, size, "\ndevices ");
        device_append_u32(buffer, size, info->device_count);
        device_append(buffer, size, "\nsize_mb ");
        device_append_u32(buffer, size, info->total_size_mb);
        device_append(buffer, size, "\nresync ");
        device_append(buffer, size, info->resync_active ? "active" : "idle");
        device_append(buffer, size, "\n");
        return (int32_t) strlen(buffer);
    }
    if (strcmp(dev->name, "opp") == 0) {
        const opp_info_t *info = opp_info();

        device_append(buffer, size, opp_status());
        device_append(buffer, size, "\npresent ");
        device_append(buffer, size, info->present ? "yes" : "no");
        device_append(buffer, size, "\ndomains ");
        device_append_u32(buffer, size, info->domain_count);
        device_append(buffer, size, "\ngovernor ");
        device_append(buffer, size, info->governor);
        device_append(buffer, size, "\nscaling ");
        device_append(buffer, size, info->scaling_enabled ? "yes" : "no");
        device_append(buffer, size, "\n");
        return (int32_t) strlen(buffer);
    }
    if (strcmp(dev->name, "od") == 0) {
        const od_info_t *info = od_info();

        device_append(buffer, size, od_status());
        device_append(buffer, size, "\npresent ");
        device_append(buffer, size, info->present ? "yes" : "no");
        device_append(buffer, size, "\nenabled ");
        device_append(buffer, size, info->enabled ? "yes" : "no");
        device_append(buffer, size, "\ndomains ");
        device_append_u32(buffer, size, info->domain_count);
        device_append(buffer, size, "\ntdp ");
        device_append_u32(buffer, size, info->tdp_watts);
        device_append(buffer, size, "\ntemp_limit ");
        device_append_u32(buffer, size, info->temp_limit_c);
        device_append(buffer, size, "\n");
        return (int32_t) strlen(buffer);
    }
    if (strcmp(dev->name, "bios") == 0) {
        const bios_info_t *info = bios_info();

        device_append(buffer, size, bios_status());
        device_append(buffer, size, "\nmem_kb ");
        device_append_u32(buffer, size, info->conventional_kb);
        device_append(buffer, size, "\nebda ");
        device_append_hex_u32(buffer, size, info->ebda_segment);
        device_append(buffer, size, "\n");
        return (int32_t) strlen(buffer);
    }
    if (strcmp(dev->name, "gop") == 0) {
        const gop_info_t *info = gop_info();

        device_append(buffer, size, gop_status());
        device_append(buffer, size, "\nbackend ");
        device_append(buffer, size, info->backend);
        device_append(buffer, size, "\nfb ");
        device_append_hex_u32(buffer, size, info->framebuffer);
        device_append(buffer, size, "\n");
        return (int32_t) strlen(buffer);
    }
    if (strcmp(dev->name, "rtc") == 0) {
        char time_text[20];

        device_append(buffer, size, rtc_status());
        if (rtc_format_time(time_text, sizeof(time_text))) {
            device_append(buffer, size, "\n");
            device_append(buffer, size, time_text);
        }
        device_append(buffer, size, "\n");
        return (int32_t) strlen(buffer);
    }
    if (strcmp(dev->name, "heap") == 0) {
        const heap_info_t *info = heap_info();

        device_append(buffer, size, heap_status());
        device_append(buffer, size, "\nused ");
        device_append_u64(buffer, size, info->used);
        device_append(buffer, size, "\nfree ");
        device_append_u64(buffer, size, info->free_bytes);
        device_append(buffer, size, "\nallocs ");
        device_append_u32(buffer, size, info->alloc_count);
        device_append(buffer, size, "\n");
        return (int32_t) strlen(buffer);
    }
    if (strcmp(dev->name, "frame") == 0) {
        const frame_info_t *info = frame_info();

        device_append(buffer, size, frame_status());
        device_append(buffer, size, "\nused ");
        device_append_u32(buffer, size, info->used_frames);
        device_append(buffer, size, "\ntotal ");
        device_append_u32(buffer, size, info->total_frames);
        device_append(buffer, size, "\n");
        return (int32_t) strlen(buffer);
    }
    if (strcmp(dev->name, "vma") == 0) {
        device_append(buffer, size, vma_status());
        device_append(buffer, size, "\nregions ");
        device_append_u32(buffer, size, vma_count());
        device_append(buffer, size, "\n");
        return (int32_t) strlen(buffer);
    }
    if (strcmp(dev->name, "lazyalloc") == 0) {
        device_append(buffer, size, lazyalloc_status());
        device_append(buffer, size, "\nregions ");
        device_append_u32(buffer, size, lazyalloc_count());
        device_append(buffer, size, "\n");
        return (int32_t) strlen(buffer);
    }
    if (strcmp(dev->name, "bitmap") == 0) {
        const bitmap_stats_t *stats = bitmap_stats();

        device_append(buffer, size, bitmap_status());
        device_append(buffer, size, "\nallocs ");
        device_append_u32(buffer, size, stats->alloc_ops);
        device_append(buffer, size, "\nfrees ");
        device_append_u32(buffer, size, stats->free_ops);
        device_append(buffer, size, "\n");
        return (int32_t) strlen(buffer);
    }
    if (strcmp(dev->name, "buddy") == 0) {
        const buddy_info_t *info = buddy_info();

        device_append(buffer, size, buddy_status());
        device_append(buffer, size, "\nallocs ");
        device_append_u32(buffer, size, info->alloc_count);
        device_append(buffer, size, "\nfrees ");
        device_append_u32(buffer, size, info->free_count);
        device_append(buffer, size, "\n");
        return (int32_t) strlen(buffer);
    }
    if (strcmp(dev->name, "scheduler") == 0) {
        const scheduler_info_t *info = scheduler_info();

        device_append(buffer, size, scheduler_status());
        device_append(buffer, size, "\npolicy ");
        device_append(buffer, size, scheduler_policy_name(info->policy));
        device_append(buffer, size, "\ndispatches ");
        device_append_u32(buffer, size, info->dispatches);
        device_append(buffer, size, "\n");
        return (int32_t) strlen(buffer);
    }
    if (strcmp(dev->name, "eevdf") == 0) {
        const eevdf_info_t *info = eevdf_info();

        device_append(buffer, size, eevdf_status());
        device_append(buffer, size, "\ntracked ");
        device_append_u32(buffer, size, info->tracked_tasks);
        device_append(buffer, size, "\n");
        return (int32_t) strlen(buffer);
    }
    if (strcmp(dev->name, "muqss") == 0) {
        const muqss_info_t *info = muqss_info();

        device_append(buffer, size, muqss_status());
        device_append(buffer, size, "\ntracked ");
        device_append_u32(buffer, size, info->tracked_tasks);
        device_append(buffer, size, "\n");
        return (int32_t) strlen(buffer);
    }
    if (strcmp(dev->name, "pcb") == 0) {
        device_append(buffer, size, pcb_status());
        device_append(buffer, size, "\ncount ");
        device_append_u32(buffer, size, pcb_count());
        device_append(buffer, size, "\ncapacity ");
        device_append_u32(buffer, size, pcb_capacity());
        device_append(buffer, size, "\n");
        return (int32_t) strlen(buffer);
    }
    if (strcmp(dev->name, "pool") == 0) {
        const pool_stats_t *stats = pool_stats();

        device_append(buffer, size, pool_status());
        device_append(buffer, size, "\npools ");
        device_append_u32(buffer, size, stats->pools);
        device_append(buffer, size, "\nused ");
        device_append_u32(buffer, size, stats->slots_used);
        device_append(buffer, size, "\n");
        return (int32_t) strlen(buffer);
    }
    if (strcmp(dev->name, "prsys") == 0) {
        const prsys_info_t *info = prsys_info();

        device_append(buffer, size, prsys_status());
        device_append(buffer, size, "\nprocesses ");
        device_append_u32(buffer, size, info->processes);
        device_append(buffer, size, "\ntasks ");
        device_append_u32(buffer, size, info->tasks);
        device_append(buffer, size, "\n");
        return (int32_t) strlen(buffer);
    }
    if (strcmp(dev->name, "futex") == 0) {
        const futex_info_t *info = futex_info();

        device_append(buffer, size, futex_status());
        device_append(buffer, size, "\nwaiters ");
        device_append_u32(buffer, size, info->total_waiters);
        device_append(buffer, size, "\nslots ");
        device_append_u32(buffer, size, info->used_slots);
        device_append(buffer, size, "\n");
        return (int32_t) strlen(buffer);
    }
    if (strcmp(dev->name, "ipc") == 0) {
        const ipc_info_t *info = ipc_info();

        device_append(buffer, size, ipc_status());
        device_append(buffer, size, "\nports ");
        device_append_u32(buffer, size, info->port_count);
        device_append(buffer, size, "\nsent ");
        device_append_u32(buffer, size, info->sent_count);
        device_append(buffer, size, "\n");
        return (int32_t) strlen(buffer);
    }
    if (strcmp(dev->name, "signal") == 0) {
        const signal_info_t *info = signal_info();

        device_append(buffer, size, signal_status());
        device_append(buffer, size, "\nsent ");
        device_append_u32(buffer, size, info->sent_count);
        device_append(buffer, size, "\n");
        return (int32_t) strlen(buffer);
    }
    if (strcmp(dev->name, "smp") == 0) {
        const smp_info_t *info = smp_info();

        device_append(buffer, size, info->status);
        device_append(buffer, size, "\nlogical ");
        device_append_u32(buffer, size, info->logical_processors);
        device_append(buffer, size, "\nonline ");
        device_append_u32(buffer, size, info->online_processors);
        device_append(buffer, size, "\nmode ");
        device_append(buffer, size, info->bootstrap_only ? "bsp-only\n" : "ap-scheduler\n");
        return (int32_t) strlen(buffer);
    }
    if (strcmp(dev->name, "schedopt") == 0) {
        const schedopt_info_t *info = schedopt_info();

        device_append(buffer, size, schedopt_status());
        device_append(buffer, size, "\npolicy ");
        device_append(buffer, size, info->policy);
        device_append(buffer, size, "\nlogical ");
        device_append_u32(buffer, size, info->logical_processors);
        device_append(buffer, size, "\n");
        return (int32_t) strlen(buffer);
    }
    if (strcmp(dev->name, "vmext") == 0) {
        const vmext_info_t *info = vmext_info();

        device_append(buffer, size, vmext_status());
        device_append(buffer, size, "\nvma ");
        device_append_u32(buffer, size, info->vma_regions);
        device_append(buffer, size, "\nlazy ");
        device_append_u32(buffer, size, info->lazy_regions);
        device_append(buffer, size, "\npml4 ");
        device_append_hex_u32(buffer, size, (uint32_t) info->pml4_phys);
        device_append(buffer, size, "\n");
        return (int32_t) strlen(buffer);
    }
    if (strcmp(dev->name, "storagex") == 0) {
        const storage_ext_info_t *info = storage_ext_info();

        device_append(buffer, size, storage_ext_status());
        device_append(buffer, size, "\nide ");
        device_append_u32(buffer, size, info->ide_controllers);
        device_append(buffer, size, "\nsata ");
        device_append_u32(buffer, size, info->sata_controllers);
        device_append(buffer, size, "\nnvme ");
        device_append_u32(buffer, size, info->nvme_controllers);
        device_append(buffer, size, "\n");
        return (int32_t) strlen(buffer);
    }
    if (strcmp(dev->name, "power") == 0) {
        const power_info_t *info = power_info();

        device_append(buffer, size, power_status());
        device_append(buffer, size, "\nacpi ");
        device_append(buffer, size, info->acpi_ready ? "yes" : "no");
        device_append(buffer, size, "\ncpu_freq ");
        device_append(buffer, size, info->cpu_frequency_detected ? "detected" : "no");
        device_append(buffer, size, "\n");
        return (int32_t) strlen(buffer);
    }
    if (strcmp(dev->name, "gpu") == 0) {
        const gpu_info_t *info = gpu_info();

        device_append(buffer, size, gpu_status());
        device_append(buffer, size, "\nbackend ");
        device_append(buffer, size, info->backend);
        device_append(buffer, size, "\nmode ");
        device_append_u32(buffer, size, info->width);
        device_append(buffer, size, "x");
        device_append_u32(buffer, size, info->height);
        device_append(buffer, size, "x");
        device_append_u32(buffer, size, info->bpp);
        device_append(buffer, size, "\n");
        return (int32_t) strlen(buffer);
    }
    if (strcmp(dev->name, "gui") == 0) {
        const gui_info_t *info = gui_info();

        device_append(buffer, size, gui_status());
        device_append(buffer, size, "\nwidgets ");
        device_append_u32(buffer, size, info->widgets_registered);
        device_append(buffer, size, "\nwindows ");
        device_append_u32(buffer, size, info->windows);
        device_append(buffer, size, "\n");
        return (int32_t) strlen(buffer);
    }
    if (strcmp(dev->name, "browser") == 0) {
        const browser_info_t *info = browser_info();

        device_append(buffer, size, browser_status());
        device_append(buffer, size, "\nrequests ");
        device_append_u32(buffer, size, info->pages_requested);
        device_append(buffer, size, "\nlast ");
        device_append(buffer, size, info->last_url);
        device_append(buffer, size, "\n");
        return (int32_t) strlen(buffer);
    }
    if (strcmp(dev->name, "term") == 0) {
        const terminal_info_t *term = terminal_info();

        device_append(buffer, size, term->active ? "terminal active\n" : "terminal inactive\n");
        device_append(buffer, size, term->focused ? "focus yes\n" : "focus no\n");
        device_append(buffer, size, "mode ");
        device_append(buffer, size, term->mode);
        device_append(buffer, size, "\nlines ");
        device_append_u32(buffer, size, term->lines_written);
        device_append(buffer, size, "\n");
        return (int32_t) strlen(buffer);
    }
    if (strcmp(dev->name, "wm") == 0) {
        device_append(buffer, size, "windows ");
        device_append_u32(buffer, size, graphics_window_count());
        device_append(buffer, size, "\nfocused ");
        device_append_u32(buffer, size, graphics_focused_window_index());
        device_append(buffer, size, "\n");
        return (int32_t) strlen(buffer);
    }
    if (strcmp(dev->name, "fb") == 0) {
        device_append(buffer, size, graphics_active() ? "framebuffer active\n" : "framebuffer inactive\n");
        return (int32_t) strlen(buffer);
    }
    if (strcmp(dev->name, "console") == 0) {
        device_append(buffer, size, "console\n");
        return (int32_t) strlen(buffer);
    }
    return -1;
}

int32_t device_write(const char *name, const char *buffer, uint32_t size)
{
    const device_entry_t *dev = device_find(name);

    if (dev == NULL || !dev->writable || buffer == NULL) {
        return -1;
    }
    if (strcmp(dev->name, "null") == 0) {
        return (int32_t) size;
    }
    if (strcmp(dev->name, "console") == 0) {
        for (uint32_t i = 0; i < size; i++) {
            console_write_char(buffer[i]);
        }
        return (int32_t) size;
    }
    return -1;
}
