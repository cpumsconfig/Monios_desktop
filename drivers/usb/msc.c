/* =========================================================================
 * drivers/usb/msc.c
 *
 * USB Mass Storage Class driver: Bulk-Only Transport (BBB) + SCSI
 * transparent command set.  Implements INQUIRY / READ_CAPACITY / READ_10 /
 * WRITE_10 / TEST_UNIT_READY and exposes block-style read/write shims that
 * blockdev.c can register.
 *
 * The actual xHCI endpoint transfer primitives are provided by the roothub
 * layer through the function pointers in msc_device_t.  Until a live
 * bulk-out/bulk-in endpoint pair is wired up, the transport hooks are NULL
 * and every SCSI command returns MSC_TRANS_TIMEOUT; the block shims then
 * report failure gracefully (no crash, no garbage).
 * ========================================================================= */

#include "common.h"
#include "kernel.h"
#include "msc.h"
#include "string.h"

#define MSC_MAX_DEVICES  8u

static msc_device_t g_msc[MSC_MAX_DEVICES];

void msc_init(void)
{
    memset(g_msc, 0, sizeof(g_msc));
}

/* -------------------------------------------------------------------------
 * CDB builders
 * ------------------------------------------------------------------------- */
void msc_build_inquiry(uint8_t *cdb)
{
    memset(cdb, 0, 16u);
    cdb[0] = SCSI_INQUIRY;
    cdb[4] = (uint8_t) SCSI_INQUIRY_LEN;
}

void msc_build_read_capacity(uint8_t *cdb)
{
    memset(cdb, 0, 16u);
    cdb[0] = SCSI_READ_CAPACITY_10;
}

void msc_build_read10(uint8_t *cdb, uint64_t lba, uint32_t count)
{
    memset(cdb, 0, 16u);
    cdb[0] = SCSI_READ_10;
    cdb[2] = (uint8_t) (lba >> 24);
    cdb[3] = (uint8_t) (lba >> 16);
    cdb[4] = (uint8_t) (lba >> 8);
    cdb[5] = (uint8_t) (lba);
    cdb[7] = (uint8_t) (count >> 8);
    cdb[8] = (uint8_t) (count);
}

void msc_build_write10(uint8_t *cdb, uint64_t lba, uint32_t count)
{
    memset(cdb, 0, 16u);
    cdb[0] = SCSI_WRITE_10;
    cdb[2] = (uint8_t) (lba >> 24);
    cdb[3] = (uint8_t) (lba >> 16);
    cdb[4] = (uint8_t) (lba >> 8);
    cdb[5] = (uint8_t) (lba);
    cdb[7] = (uint8_t) (count >> 8);
    cdb[8] = (uint8_t) (count);
}

void msc_build_test_unit_ready(uint8_t *cdb)
{
    memset(cdb, 0, 16u);
    cdb[0] = SCSI_TEST_UNIT_READY;
}

/* -------------------------------------------------------------------------
 * Core BBB transaction: CBW -> (data phase) -> CSW.
 * ------------------------------------------------------------------------- */
static msc_status_t msc_do_transaction(msc_device_t *dev, const uint8_t *cdb,
                                       uint32_t cdb_len, void *data,
                                       uint32_t data_len, bool dir_in)
{
    msc_cbw_t cbw;
    msc_csw_t csw;

    if (dev == NULL || !dev->present) {
        return MSC_TRANS_ERROR;
    }
    if (dev->send_cbw == NULL || dev->transfer == NULL || dev->recv_csw == NULL) {
        /* No live xHCI transport yet (roothub not wired). */
        return MSC_TRANS_TIMEOUT;
    }

    memset(&cbw, 0, sizeof(cbw));
    cbw.dCBWSignature = MSC_CBW_SIGNATURE;
    cbw.dCBWTag = dev->tag++;
    cbw.dCBWDataTransferLength = data_len;
    cbw.bmFlags = dir_in ? MSC_FLAG_DEVICE_TO_HOST : 0x00u;
    cbw.bLun = dev->lun;
    cbw.bCBWLength = (uint8_t) cdb_len;
    memcpy(cbw.CBWCB, cdb, cdb_len < 16u ? cdb_len : 16u);

    if (dev->send_cbw(dev->slot_id, &cbw) != MSC_TRANS_OK) {
        return MSC_TRANS_ERROR;
    }

    if (data_len > 0u && data != NULL) {
        if (dev->transfer(dev->slot_id, data, data_len, dir_in) != MSC_TRANS_OK) {
            return MSC_TRANS_ERROR;
        }
    }

    memset(&csw, 0, sizeof(csw));
    if (dev->recv_csw(dev->slot_id, &csw) != MSC_TRANS_OK) {
        return MSC_TRANS_ERROR;
    }
    if (csw.dCSWSignature != MSC_CSW_SIGNATURE) {
        return MSC_TRANS_ERROR;
    }
    if (csw.bCSWStatus != 0u) {
        return MSC_TRANS_STALL;
    }
    return MSC_TRANS_OK;
}

/* -------------------------------------------------------------------------
 * High-level SCSI commands
 * ------------------------------------------------------------------------- */
msc_status_t msc_scsi_inquiry(msc_device_t *dev, void *data, uint32_t len)
{
    uint8_t cdb[16];

    if (len > SCSI_INQUIRY_LEN) {
        len = SCSI_INQUIRY_LEN;
    }
    msc_build_inquiry(cdb);
    return msc_do_transaction(dev, cdb, 6u, data, len, true);
}

msc_status_t msc_scsi_read_capacity(msc_device_t *dev, uint32_t *last_lba, uint32_t *sector_size)
{
    uint8_t cdb[16];
    uint8_t buf[SCSI_READ_CAPACITY_LEN];
    msc_status_t rc;

    msc_build_read_capacity(cdb);
    rc = msc_do_transaction(dev, cdb, 10u, buf, sizeof(buf), true);
    if (rc != MSC_TRANS_OK) {
        return rc;
    }
    if (last_lba != NULL) {
        *last_lba = ((uint32_t) buf[0] << 24) | ((uint32_t) buf[1] << 16) |
                    ((uint32_t) buf[2] << 8) | (uint32_t) buf[3];
    }
    if (sector_size != NULL) {
        *sector_size = ((uint32_t) buf[4] << 24) | ((uint32_t) buf[5] << 16) |
                       ((uint32_t) buf[6] << 8) | (uint32_t) buf[7];
    }
    return MSC_TRANS_OK;
}

msc_status_t msc_scsi_read10(msc_device_t *dev, uint64_t lba, uint32_t count, void *data)
{
    uint8_t cdb[16];
    uint32_t bytes;

    if (dev == NULL || dev->sector_size == 0u) {
        return MSC_TRANS_ERROR;
    }
    bytes = count * dev->sector_size;
    msc_build_read10(cdb, lba, count);
    return msc_do_transaction(dev, cdb, 10u, data, bytes, true);
}

msc_status_t msc_scsi_write10(msc_device_t *dev, uint64_t lba, uint32_t count, const void *data)
{
    uint8_t cdb[16];
    uint32_t bytes;

    if (dev == NULL || dev->sector_size == 0u || dev->write_protected) {
        return MSC_TRANS_ERROR;
    }
    bytes = count * dev->sector_size;
    msc_build_write10(cdb, lba, count);
    return msc_do_transaction(dev, cdb, 10u, (void *) data, bytes, false);
}

msc_status_t msc_scsi_test_unit_ready(msc_device_t *dev)
{
    uint8_t cdb[16];

    msc_build_test_unit_ready(cdb);
    return msc_do_transaction(dev, cdb, 6u, NULL, 0u, false);
}

/* -------------------------------------------------------------------------
 * Block-device shims: always operate on the first present MSC device.
 * A real build registers one blockdev_t per enumerated LUN.
 * ------------------------------------------------------------------------- */
static msc_device_t *msc_first_present(void)
{
    for (uint32_t i = 0; i < MSC_MAX_DEVICES; i++) {
        if (g_msc[i].present) {
            return &g_msc[i];
        }
    }
    return NULL;
}

bool msc_blockdev_read(uint64_t lba, uint32_t count, void *buffer)
{
    msc_device_t *dev = msc_first_present();

    if (dev == NULL || buffer == NULL || count == 0u) {
        return false;
    }
    return msc_scsi_read10(dev, lba, count, buffer) == MSC_TRANS_OK;
}

bool msc_blockdev_write(uint64_t lba, uint32_t count, const void *buffer)
{
    msc_device_t *dev = msc_first_present();

    if (dev == NULL || buffer == NULL || count == 0u) {
        return false;
    }
    return msc_scsi_write10(dev, lba, count, buffer) == MSC_TRANS_OK;
}
