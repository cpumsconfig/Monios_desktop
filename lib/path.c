#include "common.h"
#include "path.h"

#define PATH_COMPONENT_MAX 32
#define PATH_COMPONENT_NAME_MAX 64

static bool path_is_separator(char ch)
{
    return ch == '/' || ch == '\\';
}

static bool path_has_drive_prefix(const char *path)
{
    char ch;

    if (path == NULL || path[0] == '\0' || path[1] != ':') {
        return false;
    }
    ch = path[0];
    return (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z');
}

static bool path_process_stream(const char *path,
                                char *output,
                                uint32_t output_size,
                                uint32_t *length,
                                uint32_t previous_lengths[PATH_COMPONENT_MAX],
                                uint32_t *depth)
{
    char component[PATH_COMPONENT_NAME_MAX];

    while (*path != '\0') {
        uint32_t component_length = 0;
        uint32_t old_length;

        while (path_is_separator(*path)) {
            path++;
        }
        if (*path == '\0') {
            break;
        }

        while (*path != '\0' && !path_is_separator(*path)) {
            if (component_length + 1 >= sizeof(component)) {
                return false;
            }
            component[component_length++] = *path++;
        }
        component[component_length] = '\0';

        if (strcmp(component, ".") == 0) {
            continue;
        }
        if (strcmp(component, "..") == 0) {
            if (*depth > 0) {
                *length = previous_lengths[--(*depth)];
                output[*length] = '\0';
            }
            continue;
        }
        if (*depth >= PATH_COMPONENT_MAX) {
            return false;
        }

        old_length = *length;
        if (*length > 1) {
            if (*length + 1 >= output_size) {
                return false;
            }
            output[(*length)++] = '/';
        }
        if (*length + component_length >= output_size) {
            return false;
        }

        memcpy(output + *length, component, component_length);
        *length += component_length;
        output[*length] = '\0';
        previous_lengths[(*depth)++] = old_length;
    }

    return true;
}

static const char *path_skip_drive_prefix(const char *path)
{
    if (!path_has_drive_prefix(path)) {
        return path;
    }
    path += 2;
    while (path_is_separator(*path)) {
        path++;
    }
    return path;
}

bool path_is_absolute(const char *path)
{
    return path != NULL && (path[0] == '/' || path[0] == '\\' || path_has_drive_prefix(path));
}

bool path_resolve(const char *base, const char *input, char *output, uint32_t output_size)
{
    uint32_t previous_lengths[PATH_COMPONENT_MAX];
    uint32_t depth = 0;
    uint32_t length = 1;

    if (output == NULL || output_size < 2 || input == NULL) {
        return false;
    }

    output[0] = '/';
    output[1] = '\0';

    if (path_is_absolute(input)) {
        return path_process_stream(path_skip_drive_prefix(input), output, output_size, &length, previous_lengths, &depth);
    }

    if (base != NULL && path_is_absolute(base)) {
        if (!path_process_stream(path_skip_drive_prefix(base), output, output_size, &length, previous_lengths, &depth)) {
            return false;
        }
    }

    return path_process_stream(input, output, output_size, &length, previous_lengths, &depth);
}
