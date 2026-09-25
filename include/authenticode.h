#ifndef _AUTHENTICODE_H_
#define _AUTHENTICODE_H_

#include "stdbool.h"
#include "stdint.h"

#define AUTHENTICODE_SIGNER_ID_SIZE 32U

/*
 * Verify a PE Authenticode signature, including the embedded CMS signature,
 * signer certificate, and certificate chain to the kernel trust store.
 */
bool authenticode_verify_pe(const uint8_t *data,
                            uint32_t size,
                            uint8_t signer_id[AUTHENTICODE_SIGNER_ID_SIZE]);

#endif
