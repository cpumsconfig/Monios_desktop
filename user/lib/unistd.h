#ifndef _UNISTD_H_
#define _UNISTD_H_

#include "appsys.h"
#include "stdint.h"

int read(uint64_t handle, void *buffer, uint32_t size);
int write(uint64_t handle, const void *buffer, uint32_t size);

#endif
