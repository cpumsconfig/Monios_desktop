#include "tpm.h"
#include "common.h"
#include "pci.h"
#include "cpu.h"

static tpm_info_t g_tpm_info;
static uint8_t g_tpm_buffer[TPM_MAX_BUFFER];

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
    g_tpm_info.base_address = 0;
    g_tpm_info.command_count = 0;
    strcpy(g_tpm_info.vendor_name, "unknown");
    strcpy(g_tpm_info.status, "tpm: not detected");

    /* TODO: detect TPM via ACPI or PCI */
    /* Check for TPM 2.0 CRB interface */
    /* Check for TPM 1.2 TIS interface */
}

bool tpm_is_present(void)
{
    return g_tpm_info.present;
}

bool tpm_is_ready(void)
{
    return g_tpm_info.ready;
}

int32_t tpm_send_command(const uint8_t *cmd, uint32_t cmd_len, uint8_t *resp, uint32_t *resp_len)
{
    if (!g_tpm_info.present || cmd == NULL || resp == NULL || resp_len == NULL) {
        return -1;
    }
    if (cmd_len > TPM_MAX_BUFFER || *resp_len > TPM_MAX_BUFFER) {
        return -1;
    }

    g_tpm_info.command_count++;

    /* TODO: implement actual TPM command transmission */
    /* This depends on the interface type (TIS, CRB, SPI, I2C) */

    strcpy(g_tpm_info.status, "tpm: command stub");
    return -1;
}

int32_t tpm_get_random(uint8_t *buf, uint32_t len)
{
    uint8_t cmd[12];
    uint8_t resp[TPM_MAX_BUFFER];
    uint32_t resp_len = sizeof(resp);
    int32_t ret;

    if (buf == NULL || len == 0 || len > 256) {
        return -1;
    }

    /* TPM2_GetRandom command */
    cmd[0] = 0x80; /* TPM_ST_NO_SESSIONS */
    cmd[1] = 0x01;
    cmd[2] = 0x00; /* command size (12 bytes) */
    cmd[3] = 0x00;
    cmd[4] = 0x00;
    cmd[5] = 0x0C;
    cmd[6] = 0x00; /* TPM_CC_GET_RANDOM */
    cmd[7] = 0x00;
    cmd[8] = 0x01;
    cmd[9] = 0x7B;
    cmd[10] = (uint8_t) (len >> 8); /* bytesRequested */
    cmd[11] = (uint8_t) len;

    ret = tpm_send_command(cmd, sizeof(cmd), resp, &resp_len);
    if (ret != 0) {
        return ret;
    }

    /* TODO: parse response and copy random bytes to buf */

    return (int32_t) len;
}

int32_t tpm_pcr_read(uint32_t pcr_index, uint8_t *digest, uint32_t *digest_len)
{
    (void) pcr_index;
    (void) digest;
    (void) digest_len;
    if (!g_tpm_info.present) {
        return -1;
    }
    /* TODO: implement TPM2_PCR_Read */
    strcpy(g_tpm_info.status, "tpm: pcr_read stub");
    return -1;
}

int32_t tpm_pcr_extend(uint32_t pcr_index, const uint8_t *digest, uint32_t digest_len)
{
    (void) pcr_index;
    (void) digest;
    (void) digest_len;
    if (!g_tpm_info.present) {
        return -1;
    }
    /* TODO: implement TPM2_PCR_Extend */
    strcpy(g_tpm_info.status, "tpm: pcr_extend stub");
    return -1;
}

int32_t tpm_self_test(bool full_test)
{
    (void) full_test;
    if (!g_tpm_info.present) {
        return -1;
    }
    /* TODO: implement TPM2_SelfTest */
    strcpy(g_tpm_info.status, "tpm: self_test stub");
    return -1;
}

int32_t tpm_get_capability(uint32_t cap, uint32_t property, uint32_t count, uint8_t *resp, uint32_t *resp_len)
{
    (void) cap;
    (void) property;
    (void) count;
    (void) resp;
    (void) resp_len;
    if (!g_tpm_info.present) {
        return -1;
    }
    /* TODO: implement TPM2_GetCapability */
    strcpy(g_tpm_info.status, "tpm: get_capability stub");
    return -1;
}

const tpm_info_t *tpm_info(void)
{
    return &g_tpm_info;
}

const char *tpm_status(void)
{
    return g_tpm_info.status;
}
