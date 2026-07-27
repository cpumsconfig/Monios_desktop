#include "common.h"
#include "path.h"

#define PATH_COMPONENT_MAX 32
#define PATH_COMPONENT_NAME_MAX 64

static bool path_is_separator(char ch)
{
    return ch == PATH_SEPARATOR;
}

static char path_upper_drive(char drive)
{
    if (drive >= 'a' && drive <= 'z') {
        return (char) (drive - 'a' + 'A');
    }
    return drive;
}

static bool path_has_drive_prefix(const char *path)
{
    char drive;

    if (path == NULL || path[0] == '\0' || path[1] != ':') {
        return false;
    }
    drive = path_upper_drive(path[0]);
    return drive >= 'A' && drive <= 'Z';
}

static bool path_process_stream(const char *path,
                                char *output,
                                uint32_t output_size,
                                uint32_t *length,
                                uint32_t previous_lengths[PATH_COMPONENT_MAX],
                                uint32_t *depth,
                                uint32_t root_length)
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
        if (*length > root_length) {
            if (*length + 1 >= output_size) {
                return false;
            }
            output[(*length)++] = PATH_SEPARATOR;
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

static const char *path_after_drive_prefix(const char *path)
{
    if (!path_has_drive_prefix(path)) {
        return path;
    }
    return path + 2;
}

static char path_drive_for(const char *path, char fallback)
{
    if (path_has_drive_prefix(path)) {
        return path_upper_drive(path[0]);
    }
    return fallback;
}

bool path_is_absolute(const char *path)
{
    return path != NULL &&
           (path[0] == PATH_SEPARATOR || path_has_drive_prefix(path));
}

bool path_resolve(const char *base, const char *input, char *output, uint32_t output_size)
{
    uint32_t previous_lengths[PATH_COMPONENT_MAX];
    uint32_t depth = 0;
    uint32_t length = 3;
    char drive = 'C';
    const char *stream;

    if (output == NULL || output_size < 4 || input == NULL) {
        return false;
    }

    for (stream = input; *stream != '\0'; stream++) {
        if (*stream == '/') {
            return false;
        }
    }
    if (base != NULL) {
        for (stream = base; *stream != '\0'; stream++) {
            if (*stream == '/') {
                return false;
            }
        }
    }

    if (path_has_drive_prefix(input)) {
        drive = path_drive_for(input, drive);
    } else if (path_is_absolute(input)) {
        drive = path_drive_for(base, drive);
    } else {
        drive = path_drive_for(base, drive);
    }

    output[0] = drive;
    output[1] = ':';
    output[2] = PATH_SEPARATOR;
    output[3] = '\0';

    if (path_is_absolute(input)) {
        stream = path_after_drive_prefix(input);
        return path_process_stream(stream,
                                   output,
                                   output_size,
                                   &length,
                                   previous_lengths,
                                   &depth,
                                   3);
    }

    if (base != NULL && path_is_absolute(base)) {
        stream = path_after_drive_prefix(base);
        if (!path_process_stream(stream,
                                 output,
                                 output_size,
                                 &length,
                                 previous_lengths,
                                 &depth,
                                 3)) {
            return false;
        }
    }

    return path_process_stream(input,
                               output,
                               output_size,
                               &length,
                               previous_lengths,
                               &depth,
                               3);
}
