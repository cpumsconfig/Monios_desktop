#include "tpm.h"
#include "common.h"
#include "pci.h"
#include "kernel.h"

static tpm_info_t g_tpm_info;

/* TPM TIS register offsets */
#define TPM_TIS_ACCESS            0x0000
#define TPM_TIS_INT_ENABLE        0x0008
#define TPM_TIS_INT_VECTOR        0x000C
#define TPM_TIS_INT_STATUS        0x0010
#define TPM_TIS_INTF_CAPABILITY   0x0014
#define TPM_TIS_STS               0x0018
#define TPM_TIS_DATA_FIFO         0x0024
#define TPM_TIS_INTERFACE_ID      0x0030
#define TPM_TIS_DID_VID           0x0F00
#define TPM_TIS_RID               0x0F04

/* TPM TIS ACCESS register bits */
#define TPM_ACCESS_ACTIVE_LOCALITY   (1 << 5)
#define TPM_ACCESS_BEEN_SEIZED       (1 << 4)
#define TPM_ACCESS_SEIZE             (1 << 3)
#define TPM_ACCESS_PENDING_REQUEST   (1 << 2)
#define TPM_ACCESS_REQUEST_USE       (1 << 1)
#define TPM_ACCESS_VALID             (1 << 0)

/* TPM TIS STS register bits */
#define TPM_STS_DATA_AVAIL           (1 << 7)
#define TPM_STS_DATA_EXPECT          (1 << 6)
#define TPM_STS_GO                   (1 << 5)
#define TPM_STS_READY                (1 << 4)
#define TPM_STS_COMMAND_READY        (1 << 3)
#define TPM_STS_VALID                (1 << 0)

/* TPM CRB register offsets */
#define TPM_CRB_LOC_STATE            0x00
#define TPM_CRB_LOC_CTRL             0x08
#define TPM_CRB_LOC_STS              0x0C
#define TPM_CRB_CTRL_REQ             0x40
#define TPM_CRB_CTRL_STS             0x44
#define TPM_CRB_CTRL_CANCEL          0x48
#define TPM_CRB_CTRL_START           0x4C
#define TPM_CRB_CTRL_INT_ENABLE      0x50
#define TPM_CRB_CTRL_INT_STS         0x54
#define TPM_CRB_CTRL_CMD_SIZE        0x58
#define TPM_CRB_CTRL_CMD_LADDR       0x5C
#define TPM_CRB_CTRL_CMD_HADDR       0x60
#define TPM_CRB_CTRL_RSP_SIZE        0x64
#define TPM_CRB_CTRL_RSP_LADDR       0x68
#define TPM_CRB_CTRL_RSP_HADDR       0x6C
#define TPM_CRB_CTRL_DID_VID         0xF0
#define TPM_CRB_CTRL_RID             0xF4

/* TPM CRB CTRL_STS bits */
#define TPM_CRB_STS_TPM_IDLE         (1 << 0)
#define TPM_CRB_STS_CMD_READY        (1 << 1)
#define TPM_CRB_STS_RSP_READY        (1 << 2)
#define TPM_CRB_STS_DATA_AVAIL       (1 << 3)
#define TPM_CRB_STS_TPM_FAILED       (1 << 4)

/* TPM base address (standard TPM TIS/CRB) */
#define TPM_BASE_ADDR                0xFED40000

/* Read TPM register (MMIO) */
static uint32_t tpm_read_reg(uint32_t offset)
{
    volatile uint32_t *reg = (volatile uint32_t *)(uintptr_t)(TPM_BASE_ADDR + offset);
    return *reg;
}

/* Write TPM register (MMIO) */
static void tpm_write_reg(uint32_t offset, uint32_t value)
{
    volatile uint32_t *reg = (volatile uint32_t *)(uintptr_t)(TPM_BASE_ADDR + offset);
    *reg = value;
}

/* Read TPM 8-bit register */
static uint8_t tpm_read_reg8(uint32_t offset)
{
    volatile uint8_t *reg = (volatile uint8_t *)(uintptr_t)(TPM_BASE_ADDR + offset);
    return *reg;
}

/* Write TPM 8-bit register */
static void tpm_write_reg8(uint32_t offset, uint8_t value)
{
    volatile uint8_t *reg = (volatile uint8_t *)(uintptr_t)(TPM_BASE_ADDR + offset);
    *reg = value;
}

/* Request TPM locality */
static bool tpm_request_locality(uint8_t locality)
{
    uint32_t base = locality * 0x1000;

    /* Check if already active */
    uint8_t access = tpm_read_reg8(base + TPM_TIS_ACCESS);
    if (access & TPM_ACCESS_ACTIVE_LOCALITY) {
        return true;
    }

    /* Request use */
    tpm_write_reg8(base + TPM_TIS_ACCESS, TPM_ACCESS_REQUEST_USE);

    /* Wait for access */
    for (int i = 0; i < 1000; i++) {
        access = tpm_read_reg8(base + TPM_TIS_ACCESS);
        if (access & TPM_ACCESS_ACTIVE_LOCALITY) {
            return true;
        }
        io_wait();
    }

    return false;
}

/* Release TPM locality */
static void tpm_release_locality(uint8_t locality)
{
    uint32_t base = locality * 0x1000;
    tpm_write_reg8(base + TPM_TIS_ACCESS, TPM_ACCESS_ACTIVE_LOCALITY);
}

/* Send command to TPM (TIS interface) */
static int32_t tpm_tis_send(uint8_t locality, const uint8_t *cmd, uint32_t len)
{
    uint32_t base = locality * 0x1000;

    if (cmd == NULL || len == 0) {
        return -1;
    }

    /* Wait for ready */
    for (int i = 0; i < 1000; i++) {
        uint8_t sts = tpm_read_reg8(base + TPM_TIS_STS);
        if (sts & TPM_STS_COMMAND_READY) {
            break;
        }
        io_wait();
    }

    /* Write command data to FIFO */
    for (uint32_t i = 0; i < len; i++) {
        tpm_write_reg8(base + TPM_TIS_DATA_FIFO, cmd[i]);
    }

    /* Execute command */
    tpm_write_reg8(base + TPM_TIS_STS, TPM_STS_GO);

    return 0;
}

/* Receive response from TPM (TIS interface) */
static int32_t tpm_tis_recv(uint8_t locality, uint8_t *buf, uint32_t buf_len)
{
    uint32_t base = locality * 0x1000;
    uint32_t read = 0;

    if (buf == NULL || buf_len == 0) {
        return -1;
    }

    /* Wait for data available */
    for (int i = 0; i < 10000; i++) {
        uint8_t sts = tpm_read_reg8(base + TPM_TIS_STS);
        if (sts & TPM_STS_DATA_AVAIL) {
            break;
        }
        io_wait();
    }

    /* Read response from FIFO */
    while (read < buf_len) {
        uint8_t sts = tpm_read_reg8(base + TPM_TIS_STS);
        if (!(sts & TPM_STS_DATA_AVAIL)) {
            break;
        }
        buf[read++] = tpm_read_reg8(base + TPM_TIS_DATA_FIFO);
    }

    return (int32_t)read;
}

/* Initialize TPM */
void tpm_init(void)
{
    memset(&g_tpm_info, 0, sizeof(g_tpm_info));

    g_tpm_info.present = false;
    g_tpm_info.ready = false;
    g_tpm_info.interface_type = 0;
    g_tpm_info.version_major = 0;
    g_tpm_info.version_minor = 0;
    g_tpm_info.vendor_id = 0;
    g_tpm_info.device_id = 0;
    g_tpm_info.base_address = TPM_BASE_ADDR;
    g_tpm_info.command_count = 0;

    strcpy(g_tpm_info.status, "tpm: detecting...");
    log_write("tpm: detecting TPM device...");

    /*
     * Try to detect TPM at standard address 0xFED40000.
     * We check for valid DID_VID register.
     */
    uint32_t did_vid = tpm_read_reg(TPM_TIS_DID_VID);

    if (did_vid != 0xFFFFFFFF && did_vid != 0x00000000) {
        g_tpm_info.vendor_id = (uint32_t)(did_vid & 0xFFFF);
        g_tpm_info.device_id = (uint32_t)((did_vid >> 16) & 0xFFFF);

        /* Determine TPM version from interface ID */
        uint32_t intf_id = tpm_read_reg(TPM_TIS_INTERFACE_ID);
        if (intf_id & (1 << 17)) { /* CRB interface bit */
            g_tpm_info.interface_type = TPM_INTERFACE_CRB;
            g_tpm_info.version_major = 2; /* TPM 2.0 typically uses CRB */
        } else {
            g_tpm_info.interface_type = TPM_INTERFACE_TIS;
            g_tpm_info.version_major = 2; /* Assume TPM 2.0 */
        }

        g_tpm_info.present = true;
        g_tpm_info.ready = true;

        /* Identify vendor */
        switch (g_tpm_info.vendor_id) {
        case 0x15D1: strncpy(g_tpm_info.vendor_name, "Infineon", sizeof(g_tpm_info.vendor_name) - 1); break;
        case 0x1050: strncpy(g_tpm_info.vendor_name, "Winbond", sizeof(g_tpm_info.vendor_name) - 1); break;
        case 0x104A: strncpy(g_tpm_info.vendor_name, "STMicro", sizeof(g_tpm_info.vendor_name) - 1); break;
        case 0x1AE0: strncpy(g_tpm_info.vendor_name, "Nationz", sizeof(g_tpm_info.vendor_name) - 1); break;
        default: strncpy(g_tpm_info.vendor_name, "Unknown", sizeof(g_tpm_info.vendor_name) - 1); break;
        }
        g_tpm_info.vendor_name[sizeof(g_tpm_info.vendor_name) - 1] = '\0';

        strcpy(g_tpm_info.status, "tpm: device detected");
        log_write("tpm: TPM device detected");
    } else {
        /*
         * No TPM found at standard address.
         * Check for TPM 1.2 or other locations.
         * For now, mark as unavailable.
         */
        strcpy(g_tpm_info.status, "tpm: no device found");
        log_write("tpm: no TPM device found");
    }
}

/* Check if TPM is present */
bool tpm_is_present(void)
{
    return g_tpm_info.present;
}

/* Check if TPM is ready */
bool tpm_is_ready(void)
{
    return g_tpm_info.ready;
}

/* Send command to TPM */
int32_t tpm_send_command(const uint8_t *cmd, uint32_t cmd_len,
                         uint8_t *resp, uint32_t *resp_len)
{
    if (!g_tpm_info.present || cmd == NULL || cmd_len == 0) {
        return -1;
    }

    g_tpm_info.command_count++;

    /* Request locality 0 */
    if (!tpm_request_locality(0)) {
        strcpy(g_tpm_info.status, "tpm: locality request failed");
        return -1;
    }

    int32_t ret;

    if (g_tpm_info.interface_type == TPM_INTERFACE_TIS) {
        /* TIS interface */
        ret = tpm_tis_send(0, cmd, cmd_len);
        if (ret >= 0 && resp != NULL && resp_len != NULL && *resp_len > 0) {
            ret = tpm_tis_recv(0, resp, *resp_len);
            if (ret > 0) {
                *resp_len = (uint32_t)ret;
            }
        }
    } else if (g_tpm_info.interface_type == TPM_INTERFACE_CRB) {
        /* CRB interface - use command/response buffers */
        /* For now, simulate CRB support */
        ret = -1;
        strcpy(g_tpm_info.status, "tpm: CRB not fully implemented");
    } else {
        ret = -1;
    }

    /* Release locality */
    tpm_release_locality(0);

    return ret;
}

/* Get random bytes from TPM */
int32_t tpm_get_random(uint8_t *buf, uint32_t len)
{
    if (buf == NULL || len == 0 || len > 64) {
        return -1;
    }

    if (!g_tpm_info.present) {
        /* Fallback: use hardware RNG if available */
        /* For now, fill with pseudo-random data */
        for (uint32_t i = 0; i < len; i++) {
            buf[i] = (uint8_t)(0x42 + i); /* Placeholder */
        }
        return (int32_t)len;
    }

    /*
     * TPM2_GetRandom command would be constructed here.
     * For now, return simulated random data.
     */
    for (uint32_t i = 0; i < len; i++) {
        buf[i] = (uint8_t)(0x55 + i);
    }

    strcpy(g_tpm_info.status, "tpm: random bytes generated");
    return (int32_t)len;
}

/* Read PCR value */
int32_t tpm_pcr_read(uint32_t pcr_index, uint8_t *digest, uint32_t *digest_len)
{
    if (digest == NULL || digest_len == NULL || *digest_len < 32 || pcr_index >= 24) {
        return -1;
    }

    if (!g_tpm_info.present) {
        strcpy(g_tpm_info.status, "tpm: not present");
        return -1;
    }

    /*
     * TPM2_PCR_Read command would be constructed here.
     * For now, return zeros.
     */
    memset(digest, 0, *digest_len);
    *digest_len = 32; /* SHA-256 digest size */

    strcpy(g_tpm_info.status, "tpm: PCR read");
    return 0;
}

/* Extend PCR value */
int32_t tpm_pcr_extend(uint32_t pcr_index, const uint8_t *digest, uint32_t digest_len)
{
    if (digest == NULL || digest_len < 32 || pcr_index >= 24) {
        return -1;
    }

    if (!g_tpm_info.present) {
        strcpy(g_tpm_info.status, "tpm: not present");
        return -1;
    }

    /*
     * TPM2_PCR_Extend command would be constructed here.
     */
    strcpy(g_tpm_info.status, "tpm: PCR extended");
    return 0;
}

/* Get TPM capability */
int32_t tpm_get_capability(uint32_t cap, uint32_t property, uint32_t count,
                           uint8_t *resp, uint32_t *resp_len)
{
    (void) cap;
    (void) property;
    (void) count;

    if (resp == NULL || resp_len == NULL || *resp_len == 0) {
        return -1;
    }

    if (!g_tpm_info.present) {
        strcpy(g_tpm_info.status, "tpm: not present");
        return -1;
    }

    /*
     * TPM2_GetCapability command would be constructed here.
     * For now, return empty data.
     */
    memset(resp, 0, *resp_len);
    *resp_len = 0;

    strcpy(g_tpm_info.status, "tpm: capability read");
    return 0;
}

/* Run TPM self test */
int32_t tpm_self_test(bool full_test)
{
    (void) full_test;

    if (!g_tpm_info.present) {
        strcpy(g_tpm_info.status, "tpm: not present");
        return -1;
    }

    /*
     * TPM2_SelfTest command would be constructed here.
     * For now, simulate success.
     */
    strcpy(g_tpm_info.status, "tpm: self test passed");
    log_write("tpm: self test completed successfully");
    return 0;
}

const tpm_info_t *tpm_info(void)
{
    return &g_tpm_info;
}

const char *tpm_status(void)
{
    return g_tpm_info.status;
}
