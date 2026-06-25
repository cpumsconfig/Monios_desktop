#ifndef _TPM_H_
#define _TPM_H_

#include "stdbool.h"
#include "stdint.h"

#define TPM_MAX_BUFFER        4096
#define TPM_TIMEOUT_MS        30000

/* TPM interface types */
#define TPM_INTERFACE_TIS     0x01
#define TPM_INTERFACE_CRB     0x02
#define TPM_INTERFACE_SPI     0x03
#define TPM_INTERFACE_I2C     0x04

/* TPM versions */
#define TPM_VERSION_12        0x01
#define TPM_VERSION_20        0x02

/* TPM 2.0 command codes */
#define TPM_CC_GET_CAPABILITY 0x0000017A
#define TPM_CC_SELF_TEST      0x00000143
#define TPM_CC_STARTUP        0x00000144
#define TPM_CC_SHUTDOWN       0x00000145
#define TPM_CC_GET_RANDOM     0x0000017B
#define TPM_CC_PCR_EXTEND     0x00000182
#define TPM_CC_PCR_READ       0x0000017E
#define TPM_CC_HASH           0x0000017D

/* TPM 2.0 startup types */
#define TPM_SU_CLEAR          0x0000
#define TPM_SU_STATE          0x0001

/* TPM 2.0 capability types */
#define TPM_CAP_ALGS          0x00000000
#define TPM_CAP_HANDLES       0x00000001
#define TPM_CAP_COMMANDS      0x00000002
#define TPM_CAP_PP_COMMANDS   0x00000003
#define TPM_CAP_AUDIT_COMMANDS 0x00000004
#define TPM_CAP_PCRS          0x00000005
#define TPM_CAP_TPM_PROPERTIES 0x00000006
#define TPM_CAP_PCR_PROPERTIES 0x00000007
#define TPM_CAP_ECC_CURVES    0x00000008

typedef struct {
    bool present;
    bool ready;
    uint8_t interface_type;
    uint8_t version_major;
    uint8_t version_minor;
    uint32_t vendor_id;
    uint32_t device_id;
    uint32_t base_address;
    uint32_t command_count;
    char vendor_name[16];
    char status[64];
} tpm_info_t;

void tpm_init(void);
bool tpm_is_present(void);
bool tpm_is_ready(void);
int32_t tpm_send_command(const uint8_t *cmd, uint32_t cmd_len, uint8_t *resp, uint32_t *resp_len);
int32_t tpm_get_random(uint8_t *buf, uint32_t len);
int32_t tpm_pcr_read(uint32_t pcr_index, uint8_t *digest, uint32_t *digest_len);
int32_t tpm_pcr_extend(uint32_t pcr_index, const uint8_t *digest, uint32_t digest_len);
int32_t tpm_self_test(bool full_test);
int32_t tpm_get_capability(uint32_t cap, uint32_t property, uint32_t count, uint8_t *resp, uint32_t *resp_len);
const tpm_info_t *tpm_info(void);
const char *tpm_status(void);

#endif
