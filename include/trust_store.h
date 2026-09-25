#ifndef _TRUST_STORE_H_
#define _TRUST_STORE_H_

#include "stdbool.h"
#include "stdint.h"
#include "x509.h"

bool trust_store_load(const char *path);
bool trust_store_ready(void);
uint32_t trust_store_certificate_count(void);
uint32_t trust_store_parsed_count(void);
const x509_trust_store_t *trust_store_get(void);

#endif
