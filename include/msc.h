#ifndef _MSC_H_
#define _MSC_H_

#include "stdbool.h"
#include "stdint.h"

/* =========================================================================
 * USB Mass Storage Class (MSC) Bulk-Only Transport (BBB) + SCSI transparent
 * command set.  This header documents the on-wire protocol structures shared
 * by drivers/usb/msc.c and the block-device glue in usb_ext.c.
 * ========================================================================= */

/* --- BBB command block wrapper (31 bytes, packed) ---------------------- */
#define MSC_CBW_SIGNATURE      0x43425355u   /* "USBC" little-endian */
#define MSC_CSW_SIGNATURE      0x53425355u   /* "USBS" little-endian */

typedef struct __attribute__((packed)) {
    uint32_t dCBWSignature;       /* MSC_CBW_SIGNATURE */
    uint32_t dCBWTag;             /* echo tag from CSW */
    uint32_t dCBWDataTransferLength;
    uint8_t  bmFlags;             /* 0x80 = device-to-host */
    uint8_t  bLun;
    uint8_t  bCBWLength;          /* 1..15 */
    uint8_t  CBWCB[16];           /* SCSI CDB */
} msc_cbw_t;

typedef struct __attribute__((packed)) {
    uint32_t dCSWSignature;       /* MSC_CSW_SIGNATURE */
    uint32_t dCSWTag;
    uint32_t dCSWDataResidue;
    uint8_t  bCSWStatus;          /* 0 = good */
} msc_csw_t;

#define MSC_FLAG_DEVICE_TO_HOST 0x80u

/* --- SCSI opcodes (transparent command set, subclass 0x06) ------------- */
#define SCSI_TEST_UNIT_READY   0x00u
#define SCSI_REQUEST_SENSE     0x03u
#define SCSI_INQUIRY           0x12u
#define SCSI_READ_CAPACITY_10  0x25u
#define SCSI_READ_10           0x28u
#define SCSI_WRITE_10          0x2au
#define SCSI_READ_12           0xa8u
#define SCSI_WRITE_12          0xaau

/* SCSI sense / inquiry payload sizes */
#define SCSI_INQUIRY_LEN       36u
#define SCSI_READ_CAPACITY_LEN 8u
#define SCSI_REQUEST_SENSE_LEN 18u

/* Result status returned by a BBB transaction. */
typedef enum {
    MSC_TRANS_OK = 0,
    MSC_TRANS_STALL,
    MSC_TRANS_TIMEOUT,
    MSC_TRANS_ERROR
} msc_status_t;

/* Per-device runtime state (one per enumerated U盘). */
typedef struct {
    bool     present;
    uint8_t  slot_id;
    uint8_t  lun;
    uint16_t vendor_id;
    uint16_t product_id;
    uint32_t tag;                  /* monotonic CBW tag */
    uint32_t sector_size;
    uint64_t sector_count;
    uint8_t  inquiry_vendor[8];
    uint8_t  inquiry_product[16];
    bool     write_protected;
    /* Transport hooks supplied by the xHCI roothub layer once a live
     * bulk endpoint pair is wired up.  Until then the blockdev layer
     * reports zero capacity and short reads (graceful no-op). */
    msc_status_t (*send_cbw)(uint8_t slot, const msc_cbw_t *cbw);
    msc_status_t (*transfer)(uint8_t slot, void *buf, uint32_t len, bool dir_in);
    msc_status_t (*recv_csw)(uint8_t slot, msc_csw_t *csw);
} msc_device_t;

/* --- Public driver entry points --------------------------------------- */
void msc_init(void);

/* Build a 16-byte SCSI CDB for the given opcode. */
void msc_build_inquiry(uint8_t *cdb);
void msc_build_read_capacity(uint8_t *cdb);
void msc_build_read10(uint8_t *cdb, uint64_t lba, uint32_t count);
void msc_build_write10(uint8_t *cdb, uint64_t lba, uint32_t count);
void msc_build_test_unit_ready(uint8_t *cdb);

/* High-level SCSI helpers (issue CBW + data phase + CSW). */
msc_status_t msc_scsi_inquiry(msc_device_t *dev, void *data, uint32_t len);
msc_status_t msc_scsi_read_capacity(msc_device_t *dev, uint32_t *last_lba, uint32_t *sector_size);
msc_status_t msc_scsi_read10(msc_device_t *dev, uint64_t lba, uint32_t count, void *data);
msc_status_t msc_scsi_write10(msc_device_t *dev, uint64_t lba, uint32_t count, const void *data);
msc_status_t msc_scsi_test_unit_ready(msc_device_t *dev);

/* Block-device read/write shims used by blockdev.c registration. */
bool msc_blockdev_read(uint64_t lba, uint32_t count, void *buffer);
bool msc_blockdev_write(uint64_t lba, uint32_t count, const void *buffer);

#endif /* _MSC_H_ */
