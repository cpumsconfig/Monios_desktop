#ifndef _BASE64_H_
#define _BASE64_H_

#include "stdint.h"

int32_t base64_encode(const uint8_t *input, uint32_t input_size, char *output, uint32_t output_size);
int32_t base64_decode(const char *input, uint8_t *output, uint32_t output_size);

#endif
