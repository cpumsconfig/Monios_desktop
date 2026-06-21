#ifndef _SMBUS_H_
#define _SMBUS_H_

#include "stdbool.h"

void smbus_init(void);
void smbus_driver_shutdown(void);
bool smbus_available(void);
const char *smbus_status(void);

#endif
