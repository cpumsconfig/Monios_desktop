#ifndef _PATH_H_
#define _PATH_H_

#include "stdbool.h"
#include "stdint.h"

#define PATH_MAX_LEN 256
#define PATH_DEFAULT_DRIVE "C:"
#define PATH_ROOT          "C:\\"
#define PATH_SEPARATOR     '\\'
#define PATH_SEPARATOR_STR "\\"

bool path_is_absolute(const char *path);
bool path_resolve(const char *base, const char *input, char *output, uint32_t output_size);

#endif
