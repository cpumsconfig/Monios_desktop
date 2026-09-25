#include "toolchain_format.h"
#include "toolchain_util.h"

typedef struct {
    uint32_t type;
    int32_t value;
    uint32_t string_offset;
    uint32_t string_length;
} as_operation_t;

static char g_as_decoded[MOBJ_MAX_STRING_BYTES];

static void as_strip_comments(char *line)
{
    bool quoted = false;
    bool escaped = false;
    uint32_t index;

    if (line == 0) {
        return;
    }
    for (index = 0; line[index] != '\0'; index++) {
        if (quoted) {
            if (escaped) {
                escaped = false;
            } else if (line[index] == '\\') {
                escaped = true;
            } else if (line[index] == '"') {
                quoted = false;
            }
            continue;
        }
        if (line[index] == '"') {
            quoted = true;
        } else if ((line[index] == '/' && line[index + 1] == '/') ||
                   line[index] == '#' ||
                   line[index] == ';') {
            line[index] = '\0';
            return;
        }
    }
}

static bool as_line_prefix(const char *line, const char *keyword)
{
    uint32_t length = (uint32_t) strlen(keyword);

    return strncmp(line, keyword, length) == 0 &&
           (line[length] == '\0' ||
            line[length] == ' ' ||
            line[length] == '\t');
}

static bool as_parse_line(char *line,
                          as_operation_t *operations,
                          uint32_t *operation_count,
                          uint8_t *strings,
                          uint32_t *string_bytes)
{
    uint32_t length;
    int32_t value;
    char *cursor;

    as_strip_comments(line);
    cursor = (char *) tool_skip_space(line);
    while (*cursor != '\0' &&
           (cursor[strlen(cursor) - 1] == '\r' ||
            cursor[strlen(cursor) - 1] == ' ' ||
            cursor[strlen(cursor) - 1] == '\t')) {
        cursor[strlen(cursor) - 1] = '\0';
    }
    if (*cursor == '\0' || *cursor == '#' || *cursor == ';') {
        return true;
    }
    if (strcmp(cursor, "MONIOS-AS 1") == 0) {
        return true;
    }
    if (as_line_prefix(cursor, "write")) {
        if (*operation_count >= MOBJ_MAX_OPS ||
            !tool_decode_quoted(cursor + 5,
                                g_as_decoded,
                                sizeof(g_as_decoded),
                                &length) ||
            length > MOBJ_MAX_STRING_BYTES - *string_bytes) {
            return false;
        }
        memcpy(strings + *string_bytes, g_as_decoded, length);
        operations[*operation_count].type = MOBJ_OP_WRITE;
        operations[*operation_count].value = 0;
        operations[*operation_count].string_offset = *string_bytes;
        operations[*operation_count].string_length = length;
        *string_bytes += length;
        (*operation_count)++;
        return true;
    }
    if (as_line_prefix(cursor, "exit")) {
        if (*operation_count >= MOBJ_MAX_OPS ||
            !tool_parse_i32(cursor + 4, &value)) {
            return false;
        }
        operations[*operation_count].type = MOBJ_OP_EXIT;
        operations[*operation_count].value = value;
        operations[*operation_count].string_offset = 0;
        operations[*operation_count].string_length = 0;
        (*operation_count)++;
        return true;
    }
    return false;
}

static bool as_assemble(const char *input_path, const char *output_path)
{
    static uint8_t text[TOOLCHAIN_TEXT_MAX];
    static uint8_t strings[MOBJ_MAX_STRING_BYTES];
    static uint8_t object[TOOLCHAIN_FILE_MAX];
    static as_operation_t operations[MOBJ_MAX_OPS];
    uint32_t text_size;
    uint32_t operation_count = 0;
    uint32_t string_bytes = 0;
    uint32_t line_number = 0;
    uint32_t object_size;
    uint32_t offset;
    uint32_t index;
    bool has_exit = false;
    char *line;
    char *end;
    char saved;

    if (!tool_read_file(input_path, text, sizeof(text), &text_size)) {
        fputs("as: cannot read input file\r\n");
        return false;
    }
    line = (char *) text;
    while (line < (char *) text + text_size) {
        line_number++;
        end = line;
        while (*end != '\0' && *end != '\n') {
            end++;
        }
        saved = *end;
        *end = '\0';
        if (!as_parse_line(line,
                           operations,
                           &operation_count,
                           strings,
                           &string_bytes)) {
            fputs("as: invalid source line ");
            print_uint(line_number);
            fputs("\r\n");
            return false;
        }
        if (operation_count > 0 &&
            operations[operation_count - 1].type == MOBJ_OP_EXIT) {
            has_exit = true;
        }
        if (saved == '\0') {
            break;
        }
        line = end + 1;
    }
    if (!has_exit) {
        if (operation_count >= MOBJ_MAX_OPS) {
            fputs("as: too many operations\r\n");
            return false;
        }
        operations[operation_count].type = MOBJ_OP_EXIT;
        operations[operation_count].value = 0;
        operations[operation_count].string_offset = 0;
        operations[operation_count].string_length = 0;
        operation_count++;
    }

    object_size = MOBJ_HEADER_SIZE +
                  operation_count * MOBJ_OP_SIZE +
                  string_bytes;
    if (object_size > sizeof(object)) {
        fputs("as: object is too large\r\n");
        return false;
    }
    object[0] = MOBJ_MAGIC_0;
    object[1] = MOBJ_MAGIC_1;
    object[2] = MOBJ_MAGIC_2;
    object[3] = MOBJ_MAGIC_3;
    mobj_write_u32(object + 4, MOBJ_VERSION);
    mobj_write_u32(object + 8, operation_count);
    mobj_write_u32(object + 12, string_bytes);
    offset = MOBJ_HEADER_SIZE;
    for (index = 0; index < operation_count; index++) {
        mobj_write_u32(object + offset, operations[index].type);
        mobj_write_i32(object + offset + 4, operations[index].value);
        mobj_write_u32(object + offset + 8, operations[index].string_offset);
        mobj_write_u32(object + offset + 12, operations[index].string_length);
        offset += MOBJ_OP_SIZE;
    }
    memcpy(object + offset, strings, string_bytes);
    return tool_write_file(output_path, object, object_size);
}

int main(int argc, char **argv)
{
    const char *input_path = 0;
    const char *output_path = 0;
    char default_output[TOOLCHAIN_PATH_MAX];
    int index;

    if (argc == 2 &&
        (strcmp(argv[1], "--version") == 0 ||
         strcmp(argv[1], "-v") == 0)) {
        fputs("MoniOS tiny as 0.2\r\n");
        return 0;
    }
    if (argc == 2 && strcmp(argv[1], "--help") == 0) {
        tool_usage("as", "as input.mas -o output.mobj");
        return 0;
    }
    for (index = 1; index < argc; index++) {
        if (strcmp(argv[index], "-o") == 0) {
            if (index + 1 >= argc) {
                tool_usage("as", "missing output path after -o");
                return 1;
            }
            output_path = argv[++index];
            continue;
        }
        if (argv[index][0] == '-') {
            tool_usage("as", "only -o, --help and --version are supported");
            return 1;
        }
        if (input_path != 0) {
            tool_usage("as", "only one input file is supported");
            return 1;
        }
        input_path = argv[index];
    }
    if (input_path == 0) {
        tool_usage("as", "as input.mas -o output.mobj");
        return 1;
    }
    if (output_path == 0) {
        if (!tool_default_output(input_path,
                                 ".mobj",
                                 default_output,
                                 sizeof(default_output))) {
            fputs("as: cannot derive output path\r\n");
            return 1;
        }
        output_path = default_output;
    }
    return as_assemble(input_path, output_path) ? 0 : 1;
}
