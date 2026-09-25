#include "toolchain_format.h"
#include "toolchain_util.h"

/*
 * MoniOS tiny gcc 0.3
 *
 * Two modes:
 *   gcc source.c -S -o out.mas      compile C -> tiny assembly (.mas)
 *   gcc source.c -o out.exe         one-shot driver: C -> .mas -> .mobj -> PE32+ .exe
 *
 * Preprocessing (basic):
 *   - strips // and /* *​/ comments
 *   - drops #include / #define / other # directive lines
 *   - expands object-like macros: #define NAME replacement
 *
 * The front-end still recognises only ordered puts("..."), printf("..."),
 * putchar('x') calls and a single integer return statement.  This is a
 * freestanding stage-1 compiler; it is not GNU GCC compatible.
 */

typedef struct {
    const char *argument;
    bool add_line_end;
    bool character_literal;
} gcc_output_call_t;

#define GCC_MAX_MACROS 16U
#define GCC_MACRO_NAME_MAX 32U
#define GCC_MACRO_VALUE_MAX 128U

typedef struct {
    char name[GCC_MACRO_NAME_MAX];
    char value[GCC_MACRO_VALUE_MAX];
} gcc_macro_t;

static gcc_macro_t g_macros[GCC_MAX_MACROS];
static uint32_t g_macro_count;

/* ── tiny PE / MOBJ pipeline (mirrors as.c + ld.c, in memory) ── */
#define GCC_PE_IMAGE_BASE     0x04000000ULL
#define GCC_PE_TEXT_RVA       0x00001000U
#define GCC_PE_FILE_ALIGN     0x00000200U
#define GCC_PE_SECT_ALIGN     0x00001000U
#define GCC_PE_HEADER_SIZE    0x00000200U
#define GCC_PE_MAX_IMAGE      131072U
#define GCC_SYS_HANDLE_WRITE  10U
#define GCC_SYS_EXIT_PROCESS  17U

typedef struct {
    uint32_t type;
    int32_t value;
    uint32_t string_offset;
    uint32_t string_length;
} gcc_op_t;

static char g_gcc_decoded[MOBJ_MAX_STRING_BYTES];

static bool gcc_identifier_char(char value)
{
    return (value >= 'a' && value <= 'z') ||
           (value >= 'A' && value <= 'Z') ||
           (value >= '0' && value <= '9') ||
           value == '_';
}

static const char *gcc_skip_literal(const char *cursor, char delimiter)
{
    bool escaped = false;

    if (cursor == 0 || *cursor != delimiter) {
        return cursor;
    }
    cursor++;
    while (*cursor != '\0') {
        if (escaped) {
            escaped = false;
        } else if (*cursor == '\\') {
            escaped = true;
        } else if (*cursor == delimiter) {
            return cursor + 1;
        }
        cursor++;
    }
    return 0;
}

/* ── preprocessing: collect object-like macros ─────────────────── */
static void gcc_collect_macros(const char *source)
{
    bool line_comment = false;
    bool block_comment = false;
    bool at_line_start = true;
    const char *cursor = source;

    g_macro_count = 0;
    while (*cursor != '\0') {
        if (line_comment) {
            if (*cursor == '\n') {
                line_comment = false;
                at_line_start = true;
            }
            cursor++;
            continue;
        }
        if (block_comment) {
            if (cursor[0] == '*' && cursor[1] == '/') {
                block_comment = false;
                cursor += 2;
            } else {
                cursor++;
            }
            continue;
        }
        if (cursor[0] == '/' && cursor[1] == '/') {
            line_comment = true;
            cursor += 2;
            continue;
        }
        if (cursor[0] == '/' && cursor[1] == '*') {
            block_comment = true;
            cursor += 2;
            continue;
        }
        if (*cursor == '"' || *cursor == '\'') {
            cursor = gcc_skip_literal(cursor, *cursor);
            if (cursor == 0) {
                return;
            }
            at_line_start = false;
            continue;
        }
        if (*cursor == '\n') {
            cursor++;
            at_line_start = true;
            continue;
        }
        if (at_line_start && (*cursor == ' ' || *cursor == '\t')) {
            cursor++;
            continue;
        }
        if (at_line_start && *cursor == '#') {
            const char *p = cursor + 1;

            at_line_start = false;
            while (*p == ' ' || *p == '\t') {
                p++;
            }
            if (strncmp(p, "define", 6) == 0 &&
                !gcc_identifier_char(p[6])) {
                const char *name;
                const char *value;
                uint32_t nlen;

                p += 6;
                while (*p == ' ' || *p == '\t') {
                    p++;
                }
                name = p;
                while (gcc_identifier_char(*p)) {
                    p++;
                }
                nlen = (uint32_t) (p - name);
                while (*p == ' ' || *p == '\t') {
                    p++;
                }
                value = p;
                while (*p != '\0' && *p != '\n') {
                    p++;
                }
                while (p > value &&
                       (p[-1] == ' ' || p[-1] == '\t' || p[-1] == '\r')) {
                    p--;
                }
                if (nlen > 0 && nlen < GCC_MACRO_NAME_MAX &&
                    g_macro_count < GCC_MAX_MACROS) {
                    gcc_macro_t *m = &g_macros[g_macro_count++];
                    uint32_t vlen = (uint32_t) (p - value);

                    if (vlen >= GCC_MACRO_VALUE_MAX) {
                        vlen = GCC_MACRO_VALUE_MAX - 1U;
                    }
                    memcpy(m->name, name, nlen);
                    m->name[nlen] = '\0';
                    memcpy(m->value, value, vlen);
                    m->value[vlen] = '\0';
                }
            }
            while (*cursor != '\0' && *cursor != '\n') {
                cursor++;
            }
            continue;
        }
        at_line_start = false;
        cursor++;
    }
}

static const char *gcc_find_macro(const char *name, uint32_t len)
{
    uint32_t i;

    for (i = 0; i < g_macro_count; i++) {
        if (strlen(g_macros[i].name) == len &&
            strncmp(g_macros[i].name, name, len) == 0) {
            return g_macros[i].value;
        }
    }
    return 0;
}

/* Emit cleaned source: strip comments, drop # lines, expand macros. */
static bool gcc_preprocess(const char *source,
                           char *out,
                           uint32_t capacity,
                           uint32_t *used)
{
    bool line_comment = false;
    bool block_comment = false;
    const char *cursor = source;

    *used = 0;
    while (*cursor != '\0') {
        if (line_comment) {
            if (*cursor == '\n') {
                if (!tool_append_char(out, capacity, used, '\n')) {
                    return false;
                }
                line_comment = false;
            }
            cursor++;
            continue;
        }
        if (block_comment) {
            if (cursor[0] == '*' && cursor[1] == '/') {
                block_comment = false;
                cursor += 2;
            } else {
                cursor++;
            }
            continue;
        }
        if (cursor[0] == '/' && cursor[1] == '/') {
            line_comment = true;
            cursor += 2;
            continue;
        }
        if (cursor[0] == '/' && cursor[1] == '*') {
            block_comment = true;
            cursor += 2;
            continue;
        }
        if (*cursor == '"' || *cursor == '\'') {
            char delim = *cursor;
            const char *after = gcc_skip_literal(cursor, delim);
            uint32_t len;

            if (after == 0) {
                return false;
            }
            len = (uint32_t) (after - cursor);
            if (*used + len + 1 >= capacity) {
                return false;
            }
            memcpy(out + *used, cursor, len);
            *used += len;
            out[*used] = '\0';
            cursor = after;
            continue;
        }
        /* directive line: skip */
        if (cursor == source || cursor[-1] == '\n') {
            const char *p = cursor;
            while (*p == ' ' || *p == '\t') {
                p++;
            }
            if (*p == '#') {
                while (*p != '\0' && *p != '\n') {
                    p++;
                }
                cursor = (*p == '\n') ? p : p;
                if (*cursor == '\n') {
                    if (!tool_append_char(out, capacity, used, '\n')) {
                        return false;
                    }
                    cursor++;
                }
                continue;
            }
        }
        /* identifier token: maybe a macro */
        if (gcc_identifier_char(*cursor) && !gcc_identifier_char(cursor[-1])) {
            const char *start = cursor;
            uint32_t len;
            const char *value;

            while (gcc_identifier_char(*cursor)) {
                cursor++;
            }
            len = (uint32_t) (cursor - start);
            value = gcc_find_macro(start, len);
            if (value != 0) {
                if (!tool_append_text(out, capacity, used, value)) {
                    return false;
                }
            } else {
                if (*used + len + 1 >= capacity) {
                    return false;
                }
                memcpy(out + *used, start, len);
                *used += len;
                out[*used] = '\0';
            }
            continue;
        }
        if (!tool_append_char(out, capacity, used, *cursor)) {
            return false;
        }
        cursor++;
    }
    return true;
}

static const char *gcc_find_call_argument(const char *cursor,
                                          const char *name,
                                          bool add_line_end,
                                          bool character_literal,
                                          gcc_output_call_t *call)
{
    uint32_t name_length;
    const char *after_name;

    if (cursor == 0 || name == 0 || call == 0) {
        return 0;
    }
    name_length = (uint32_t) strlen(name);
    if (strncmp(cursor, name, name_length) != 0 ||
        gcc_identifier_char(cursor[name_length])) {
        return 0;
    }
    after_name = tool_skip_space(cursor + name_length);
    if (*after_name != '(') {
        return 0;
    }
    call->argument = after_name + 1;
    call->add_line_end = add_line_end;
    call->character_literal = character_literal;
    return call->argument;
}

static bool gcc_find_next_output_call(const char *source,
                                      const char *start,
                                      gcc_output_call_t *call)
{
    const char *cursor;
    bool line_comment = false;
    bool block_comment = false;

    if (source == 0 || start == 0 || call == 0) {
        return false;
    }
    cursor = start;
    while (*cursor != '\0') {
        if (line_comment) {
            if (*cursor == '\n') {
                line_comment = false;
            }
            cursor++;
            continue;
        }
        if (block_comment) {
            if (cursor[0] == '*' && cursor[1] == '/') {
                block_comment = false;
                cursor += 2;
            } else {
                cursor++;
            }
            continue;
        }
        if (cursor[0] == '/' && cursor[1] == '/') {
            line_comment = true;
            cursor += 2;
            continue;
        }
        if (cursor[0] == '/' && cursor[1] == '*') {
            block_comment = true;
            cursor += 2;
            continue;
        }
        if (*cursor == '"' || *cursor == '\'') {
            cursor = gcc_skip_literal(cursor, *cursor);
            if (cursor == 0) {
                return false;
            }
            continue;
        }
        if ((cursor == source || !gcc_identifier_char(cursor[-1])) &&
            (gcc_find_call_argument(cursor, "puts", true, false, call) != 0 ||
             gcc_find_call_argument(cursor, "printf", false, false, call) != 0 ||
             gcc_find_call_argument(cursor, "putchar", false, true, call) != 0)) {
            return true;
        }
        cursor++;
    }
    return false;
}

static const char *gcc_find_keyword(const char *source, const char *keyword)
{
    uint32_t keyword_length;
    const char *cursor;
    bool line_comment = false;
    bool block_comment = false;

    if (source == 0 || keyword == 0) {
        return 0;
    }
    keyword_length = (uint32_t) strlen(keyword);
    cursor = source;
    while (*cursor != '\0') {
        if (line_comment) {
            if (*cursor == '\n') {
                line_comment = false;
            }
            cursor++;
            continue;
        }
        if (block_comment) {
            if (cursor[0] == '*' && cursor[1] == '/') {
                block_comment = false;
                cursor += 2;
            } else {
                cursor++;
            }
            continue;
        }
        if (cursor[0] == '/' && cursor[1] == '/') {
            line_comment = true;
            cursor += 2;
            continue;
        }
        if (cursor[0] == '/' && cursor[1] == '*') {
            block_comment = true;
            cursor += 2;
            continue;
        }
        if (*cursor == '"' || *cursor == '\'') {
            cursor = gcc_skip_literal(cursor, *cursor);
            if (cursor == 0) {
                return 0;
            }
            continue;
        }
        if (strncmp(cursor, keyword, keyword_length) == 0 &&
            (cursor == source || !gcc_identifier_char(cursor[-1])) &&
            !gcc_identifier_char(cursor[keyword_length])) {
            return cursor + keyword_length;
        }
        cursor++;
    }
    return 0;
}

static bool gcc_append_escaped_string(char *output,
                                      uint32_t capacity,
                                      uint32_t *used,
                                      const char *text,
                                      uint32_t length)
{
    uint32_t index;

    for (index = 0; index < length; index++) {
        uint8_t value = (uint8_t) text[index];

        switch (value) {
        case '\n':
            if (!tool_append_text(output, capacity, used, "\\n")) {
                return false;
            }
            break;
        case '\r':
            if (!tool_append_text(output, capacity, used, "\\r")) {
                return false;
            }
            break;
        case '\t':
            if (!tool_append_text(output, capacity, used, "\\t")) {
                return false;
            }
            break;
        case '\\':
            if (!tool_append_text(output, capacity, used, "\\\\")) {
                return false;
            }
            break;
        case '"':
            if (!tool_append_text(output, capacity, used, "\\\"")) {
                return false;
            }
            break;
        default:
            if (value < 0x20U) {
                return false;
            }
            if (!tool_append_char(output, capacity, used, (char) value)) {
                return false;
            }
            break;
        }
    }
    return true;
}

static bool gcc_decode_character(const char *text,
                                char *output,
                                uint32_t output_size,
                                uint32_t *length_out)
{
    const char *cursor;
    uint8_t value;

    if (text == 0 || output == 0 || output_size < 2) {
        return false;
    }
    cursor = text;
    while (*cursor != '\0' && *cursor != '\'') {
        cursor++;
    }
    if (*cursor != '\'') {
        return false;
    }
    cursor++;
    if (*cursor == '\\') {
        cursor++;
        switch (*cursor) {
        case 'n': value = '\n'; break;
        case 'r': value = '\r'; break;
        case 't': value = '\t'; break;
        case '\\': value = '\\'; break;
        case '\'': value = '\''; break;
        case '"': value = '"'; break;
        default: return false;
        }
    } else if (*cursor != '\0') {
        value = (uint8_t) *cursor;
    } else {
        return false;
    }
    cursor++;
    if (*cursor != '\'') {
        return false;
    }
    output[0] = (char) value;
    output[1] = '\0';
    if (length_out != 0) {
        *length_out = 1;
    }
    return true;
}

static const char *gcc_call_argument_end(const char *argument, char delimiter)
{
    const char *cursor = argument;

    if (cursor == 0) {
        return 0;
    }
    while (*cursor != '\0' && *cursor != delimiter) {
        cursor++;
    }
    return gcc_skip_literal(cursor, delimiter);
}

static bool gcc_append_write(char *output,
                             uint32_t capacity,
                             uint32_t *used,
                             const char *message,
                             uint32_t message_length)
{
    return tool_append_text(output, capacity, used, "write \"") &&
           gcc_append_escaped_string(output,
                                     capacity,
                                     used,
                                     message,
                                     message_length) &&
           tool_append_text(output, capacity, used, "\"\r\n");
}

/* Compile preprocessed source text into .mas text. Returns false on error. */
static bool gcc_generate_mas(const char *source,
                             char *output,
                             uint32_t output_capacity,
                             uint32_t *output_used)
{
    static char message[TOOLCHAIN_TEXT_MAX];
    uint32_t used = 0;
    int32_t return_code = 0;
    uint32_t call_count = 0;
    const char *cursor;
    const char *argument_end;
    const char *return_keyword;
    gcc_output_call_t call;

    output[0] = '\0';
    if (!tool_append_text(output, output_capacity, &used, "MONIOS-AS 1\r\n")) {
        fputs("gcc: generated assembly is too large\r\n");
        return false;
    }
    cursor = source;
    while (gcc_find_next_output_call(source, cursor, &call)) {
        char delimiter = call.character_literal ? '\'' : '"';
        uint32_t message_length = 0;

        if (call.character_literal) {
            if (!gcc_decode_character(call.argument,
                                      message,
                                      sizeof(message),
                                      &message_length)) {
                fputs("gcc: expected one character literal\r\n");
                return false;
            }
        } else if (!tool_decode_quoted(call.argument,
                                      message,
                                      sizeof(message),
                                      &message_length)) {
            fputs("gcc: expected one quoted string literal\r\n");
            return false;
        }
        if (call.add_line_end) {
            if (message_length + 2U >= sizeof(message)) {
                fputs("gcc: string literal is too large\r\n");
                return false;
            }
            message[message_length++] = '\r';
            message[message_length++] = '\n';
            message[message_length] = '\0';
        }
        if (message_length > MOBJ_MAX_STRING_BYTES ||
            !gcc_append_write(output, output_capacity, &used,
                              message, message_length)) {
            fputs("gcc: generated assembly is too large\r\n");
            return false;
        }
        argument_end = gcc_call_argument_end(call.argument, delimiter);
        if (argument_end == 0 ||
            *tool_skip_space(argument_end) != ')') {
            fputs("gcc: only one literal argument is supported\r\n");
            return false;
        }
        cursor = argument_end;
        call_count++;
    }
    (void) call_count;
    return_keyword = gcc_find_keyword(source, "return");
    if (return_keyword != 0) {
        if (!tool_parse_i32(return_keyword, &return_code)) {
            fputs("gcc: return must use an integer literal\r\n");
            return false;
        }
    }

    if (!tool_append_text(output, output_capacity, &used, "exit ") ||
        !tool_append_i32(output, output_capacity, &used, return_code) ||
        !tool_append_text(output, output_capacity, &used, "\r\n")) {
        fputs("gcc: generated assembly is too large\r\n");
        return false;
    }
    *output_used = used;
    return true;
}

/* ── in-memory assembler: .mas text -> MOBJ bytes ─────────────── */
static bool gcc_assemble_mas(const char *mas,
                             uint8_t *object,
                             uint32_t object_cap,
                             uint32_t *object_size)
{
    static uint8_t strings[MOBJ_MAX_STRING_BYTES];
    static gcc_op_t ops[MOBJ_MAX_OPS];
    uint32_t op_count = 0;
    uint32_t string_bytes = 0;
    const char *line = mas;
    bool has_exit = false;

    while (*line != '\0') {
        char buffer[256];
        uint32_t i = 0;
        const char *cursor;
        char *after_prefix;

        while (*line != '\0' && *line != '\n' && i < sizeof(buffer) - 1) {
            buffer[i++] = *line++;
        }
        buffer[i] = '\0';
        if (*line == '\n') {
            line++;
        }
        cursor = buffer;
        while (*cursor == ' ' || *cursor == '\t') {
            cursor++;
        }
        if (*cursor == '\0' || *cursor == '#' || *cursor == ';') {
            continue;
        }
        if (strncmp(cursor, "MONIOS-AS", 9) == 0) {
            continue;
        }
        after_prefix = 0;
        if (strncmp(cursor, "write", 5) == 0 &&
            (cursor[5] == ' ' || cursor[5] == '\t')) {
            uint32_t len;

            if (op_count >= MOBJ_MAX_OPS ||
                !tool_decode_quoted(cursor + 5, g_gcc_decoded,
                                    sizeof(g_gcc_decoded), &len) ||
                len > MOBJ_MAX_STRING_BYTES - string_bytes) {
                return false;
            }
            memcpy(strings + string_bytes, g_gcc_decoded, len);
            ops[op_count].type = MOBJ_OP_WRITE;
            ops[op_count].value = 0;
            ops[op_count].string_offset = string_bytes;
            ops[op_count].string_length = len;
            string_bytes += len;
            op_count++;
            continue;
        }
        if (strncmp(cursor, "exit", 4) == 0 &&
            (cursor[4] == ' ' || cursor[4] == '\t' || cursor[4] == '\0')) {
            int32_t value;

            if (op_count >= MOBJ_MAX_OPS ||
                !tool_parse_i32(cursor + 4, &value)) {
                return false;
            }
            ops[op_count].type = MOBJ_OP_EXIT;
            ops[op_count].value = value;
            ops[op_count].string_offset = 0;
            ops[op_count].string_length = 0;
            op_count++;
            has_exit = true;
            continue;
        }
        return false;
    }
    if (!has_exit) {
        if (op_count >= MOBJ_MAX_OPS) {
            return false;
        }
        ops[op_count].type = MOBJ_OP_EXIT;
        ops[op_count].value = 0;
        ops[op_count].string_offset = 0;
        ops[op_count].string_length = 0;
        op_count++;
    }
    *object_size = MOBJ_HEADER_SIZE + op_count * MOBJ_OP_SIZE + string_bytes;
    if (*object_size > object_cap) {
        return false;
    }
    object[0] = MOBJ_MAGIC_0;
    object[1] = MOBJ_MAGIC_1;
    object[2] = MOBJ_MAGIC_2;
    object[3] = MOBJ_MAGIC_3;
    mobj_write_u32(object + 4, MOBJ_VERSION);
    mobj_write_u32(object + 8, op_count);
    mobj_write_u32(object + 12, string_bytes);
    {
        uint32_t off = MOBJ_HEADER_SIZE;
        uint32_t idx;

        for (idx = 0; idx < op_count; idx++) {
            mobj_write_u32(object + off, ops[idx].type);
            mobj_write_i32(object + off + 4, ops[idx].value);
            mobj_write_u32(object + off + 8, ops[idx].string_offset);
            mobj_write_u32(object + off + 12, ops[idx].string_length);
            off += MOBJ_OP_SIZE;
        }
        memcpy(object + off, strings, string_bytes);
    }
    return true;
}

/* ── in-memory linker: MOBJ bytes -> PE32+ image ──────────────── */
static void gcc_pe_w16(uint8_t *w, uint16_t v)
{
    w[0] = (uint8_t) v;
    w[1] = (uint8_t) (v >> 8);
}

static void gcc_pe_w32(uint8_t *w, uint32_t v)
{
    w[0] = (uint8_t) v;
    w[1] = (uint8_t) (v >> 8);
    w[2] = (uint8_t) (v >> 16);
    w[3] = (uint8_t) (v >> 24);
}

static void gcc_pe_w64(uint8_t *w, uint64_t v)
{
    gcc_pe_w32(w, (uint32_t) v);
    gcc_pe_w32(w + 4, (uint32_t) (v >> 32));
}

static uint32_t gcc_align_up(uint32_t v, uint32_t a)
{
    return (v + a - 1U) & ~(a - 1U);
}

static bool gcc_pe_link(const uint8_t *object, uint32_t object_size,
                        uint8_t *image, uint32_t image_cap, uint32_t *image_size)
{
    static uint8_t code[16384];
    static uint8_t rdata[16384];
    uint32_t op_count, string_bytes, ops_end;
    uint32_t code_size = 0, rdata_size = 0;
    uint32_t code_cursor = 0, rdata_cursor = 0;
    uint32_t idx;
    bool has_exit = false;

    if (object_size < MOBJ_HEADER_SIZE ||
        object[0] != 'M' || object[1] != 'O' ||
        object[2] != 'B' || object[3] != 'J' ||
        mobj_read_u32(object + 4) != MOBJ_VERSION) {
        return false;
    }
    op_count = mobj_read_u32(object + 8);
    string_bytes = mobj_read_u32(object + 12);
    if (op_count == 0 || op_count > MOBJ_MAX_OPS) {
        return false;
    }
    ops_end = MOBJ_HEADER_SIZE + op_count * MOBJ_OP_SIZE;
    if (ops_end > object_size ||
        string_bytes > object_size - ops_end ||
        string_bytes > MOBJ_MAX_STRING_BYTES) {
        return false;
    }

    for (idx = 0; idx < op_count; idx++) {
        const uint8_t *op = object + MOBJ_HEADER_SIZE + idx * MOBJ_OP_SIZE;
        uint32_t type = mobj_read_u32(op);

        if (type == MOBJ_OP_WRITE) {
            uint32_t slen = mobj_read_u32(op + 12);

            if (rdata_size + slen > sizeof(rdata) ||
                code_size + 22U > sizeof(code)) {
                return false;
            }
            rdata_size += slen;
            code_size += 22U;
        } else if (type == MOBJ_OP_EXIT) {
            if (code_size + 12U > sizeof(code)) {
                return false;
            }
            code_size += 12U;
            has_exit = true;
        } else {
            return false;
        }
    }
    if (!has_exit && code_size + 12U > sizeof(code)) {
        return false;
    }

    {
        uint32_t text_vsize = gcc_align_up(code_size, GCC_PE_SECT_ALIGN);
        uint32_t rdata_rva = GCC_PE_TEXT_RVA + text_vsize;

        for (idx = 0; idx < op_count; idx++) {
            const uint8_t *op = object + MOBJ_HEADER_SIZE + idx * MOBJ_OP_SIZE;
            uint32_t type = mobj_read_u32(op);
            int32_t value = mobj_read_i32(op + 4);
            uint32_t soff = mobj_read_u32(op + 8);
            uint32_t slen = mobj_read_u32(op + 12);

            if (type == MOBJ_OP_WRITE) {
                const uint8_t *strings = object + ops_end;

                /* mov eax,10 ; mov ebx,1 ; mov rcx, imm64 ; mov edx,len ; int 0x80 */
                code[code_cursor++] = 0xB8; gcc_pe_w32(code + code_cursor, GCC_SYS_HANDLE_WRITE); code_cursor += 4;
                code[code_cursor++] = 0xBB; gcc_pe_w32(code + code_cursor, (uint32_t)STDOUT_FILENO); code_cursor += 4;
                code[code_cursor++] = 0x48; code[code_cursor++] = 0xB9;
                gcc_pe_w64(code + code_cursor, (uint64_t)(GCC_PE_IMAGE_BASE + rdata_rva + rdata_cursor));
                code_cursor += 8;
                code[code_cursor++] = 0xBA; gcc_pe_w32(code + code_cursor, slen); code_cursor += 4;
                code[code_cursor++] = 0xCD; code[code_cursor++] = 0x80;
                memcpy(rdata + rdata_cursor, strings + soff, slen);
                rdata_cursor += slen;
            } else if (type == MOBJ_OP_EXIT) {
                code[code_cursor++] = 0xB8; gcc_pe_w32(code + code_cursor, GCC_SYS_EXIT_PROCESS); code_cursor += 4;
                code[code_cursor++] = 0xBB; gcc_pe_w32(code + code_cursor, (uint32_t)value); code_cursor += 4;
                code[code_cursor++] = 0xCD; code[code_cursor++] = 0x80;
            }
        }
        if (!has_exit) {
            code[code_cursor++] = 0xB8; gcc_pe_w32(code + code_cursor, GCC_SYS_EXIT_PROCESS); code_cursor += 4;
            code[code_cursor++] = 0xBB; gcc_pe_w32(code + code_cursor, 0); code_cursor += 4;
            code[code_cursor++] = 0xCD; code[code_cursor++] = 0x80;
        }
    }

    /* emit PE32+ */
    {
        uint32_t text_raw = gcc_align_up(code_size, GCC_PE_FILE_ALIGN);
        uint32_t text_vsize = gcc_align_up(code_size, GCC_PE_SECT_ALIGN);
        uint32_t rdata_raw = gcc_align_up(rdata_size, GCC_PE_FILE_ALIGN);
        uint32_t rdata_vsize = gcc_align_up(rdata_size, GCC_PE_SECT_ALIGN);
        uint32_t rdata_rva = GCC_PE_TEXT_RVA + text_vsize;
        uint32_t rdata_raw_ptr = GCC_PE_HEADER_SIZE + text_raw;
        uint32_t image_vsz, total;
        uint8_t *pe, *opt, *sec;

        if (text_raw == 0) text_raw = GCC_PE_FILE_ALIGN;
        if (rdata_raw == 0) rdata_raw = GCC_PE_FILE_ALIGN;
        if (rdata_vsize == 0) rdata_vsize = GCC_PE_SECT_ALIGN;
        image_vsz = gcc_align_up(rdata_rva + rdata_vsize, GCC_PE_SECT_ALIGN);
        total = rdata_raw_ptr + rdata_raw;
        if (total > image_cap || total > GCC_PE_MAX_IMAGE) {
            return false;
        }
        memset(image, 0, total);
        image[0] = 'M';
        image[1] = 'Z';
        gcc_pe_w32(image + 0x3C, 0x80U);
        pe = image + 0x80;
        pe[0] = 'P'; pe[1] = 'E'; pe[2] = 0; pe[3] = 0;
        gcc_pe_w16(pe + 4, 0x8664);
        gcc_pe_w16(pe + 6, 2);
        gcc_pe_w16(pe + 20, 240);
        gcc_pe_w16(pe + 22, 0x0002);
        opt = pe + 24;
        gcc_pe_w16(opt, 0x020B);
        gcc_pe_w32(opt + 4, text_raw);
        gcc_pe_w32(opt + 8, rdata_raw);
        gcc_pe_w32(opt + 16, GCC_PE_TEXT_RVA);
        gcc_pe_w32(opt + 20, GCC_PE_TEXT_RVA);
        gcc_pe_w64(opt + 24, GCC_PE_IMAGE_BASE);
        gcc_pe_w32(opt + 32, GCC_PE_SECT_ALIGN);
        gcc_pe_w32(opt + 36, GCC_PE_FILE_ALIGN);
        gcc_pe_w16(opt + 48, 6);
        gcc_pe_w32(opt + 56, image_vsz);
        gcc_pe_w32(opt + 60, GCC_PE_HEADER_SIZE);
        gcc_pe_w16(opt + 68, APP_SUBSYSTEM_CONSOLE);
        gcc_pe_w64(opt + 72, 0x00100000ULL);
        gcc_pe_w64(opt + 80, 0x00001000ULL);
        gcc_pe_w64(opt + 88, 0x00100000ULL);
        gcc_pe_w64(opt + 96, 0x00001000ULL);
        gcc_pe_w32(opt + 108, 16);
        sec = opt + 240;
        memcpy(sec, ".text", 5);
        gcc_pe_w32(sec + 8, code_size);
        gcc_pe_w32(sec + 12, GCC_PE_TEXT_RVA);
        gcc_pe_w32(sec + 16, text_raw);
        gcc_pe_w32(sec + 20, GCC_PE_HEADER_SIZE);
        gcc_pe_w32(sec + 36, 0x60000020U);
        sec += 40;
        memcpy(sec, ".rdata", 6);
        gcc_pe_w32(sec + 8, rdata_size);
        gcc_pe_w32(sec + 12, rdata_rva);
        gcc_pe_w32(sec + 16, rdata_raw);
        gcc_pe_w32(sec + 20, rdata_raw_ptr);
        gcc_pe_w32(sec + 36, 0x40000040U);
        memcpy(image + GCC_PE_HEADER_SIZE, code, code_size);
        if (rdata_size > 0) {
            memcpy(image + rdata_raw_ptr, rdata, rdata_size);
        }
        *image_size = total;
    }
    return true;
}

/* Full pipeline: source text -> PE exe bytes. */
static bool gcc_build_exe(const char *cleaned_source,
                          uint8_t *image,
                          uint32_t image_cap,
                          uint32_t *image_size)
{
    static char mas[TOOLCHAIN_ASM_MAX];
    static uint8_t object[TOOLCHAIN_FILE_MAX];
    uint32_t mas_used = 0;
    uint32_t object_size = 0;

    if (!gcc_generate_mas(cleaned_source, mas, sizeof(mas), &mas_used)) {
        return false;
    }
    if (!gcc_assemble_mas(mas, object, sizeof(object), &object_size)) {
        fputs("gcc: internal assembler failed\r\n");
        return false;
    }
    if (!gcc_pe_link(object, object_size, image, image_cap, image_size)) {
        fputs("gcc: internal linker failed\r\n");
        return false;
    }
    return true;
}

static bool gcc_compile_file(const char *source_path,
                             const char *output_path,
                             bool emit_exe)
{
    static uint8_t source[TOOLCHAIN_TEXT_MAX];
    static char cleaned[TOOLCHAIN_TEXT_MAX];
    static char mas[TOOLCHAIN_ASM_MAX];
    static uint8_t image[GCC_PE_MAX_IMAGE];
    uint32_t source_size;
    uint32_t cleaned_used = 0;
    uint32_t image_size = 0;

    if (!tool_read_file(source_path, source, sizeof(source), &source_size)) {
        fputs("gcc: cannot read input file\r\n");
        return false;
    }
    (void) source_size;

    gcc_collect_macros((const char *) source);
    if (!gcc_preprocess((const char *) source, cleaned, sizeof(cleaned),
                        &cleaned_used)) {
        fputs("gcc: preprocessing failed (input too large)\r\n");
        return false;
    }
    cleaned[cleaned_used] = '\0';

    if (emit_exe) {
        if (!gcc_build_exe(cleaned, image, sizeof(image), &image_size)) {
            return false;
        }
        return tool_write_file(output_path, image, image_size);
    }

    /* -S mode: emit .mas text into a separate buffer. */
    {
        uint32_t mas_used = 0;

        if (!gcc_generate_mas(cleaned, mas, sizeof(mas), &mas_used)) {
            return false;
        }
        return tool_write_file(output_path, mas, mas_used);
    }
}

int main(int argc, char **argv)
{
    const char *input_path = 0;
    const char *output_path = 0;
    char default_output[TOOLCHAIN_PATH_MAX];
    bool assemble_only = false;
    int index;

    if (argc == 2 &&
        (strcmp(argv[1], "--version") == 0 ||
         strcmp(argv[1], "-v") == 0)) {
        fputs("MoniOS tiny gcc 0.3\r\n");
        return 0;
    }
    if (argc == 2 && strcmp(argv[1], "--help") == 0) {
        tool_usage("gcc", "gcc source.c -S [-o out.mas] | gcc source.c -o out.exe");
        return 0;
    }
    for (index = 1; index < argc; index++) {
        if (strcmp(argv[index], "-S") == 0) {
            assemble_only = true;
            continue;
        }
        if (strcmp(argv[index], "-o") == 0) {
            if (index + 1 >= argc) {
                tool_usage("gcc", "missing output path after -o");
                return 1;
            }
            output_path = argv[++index];
            continue;
        }
        if (argv[index][0] == '-') {
            tool_usage("gcc", "only -S, -o, --help and --version are supported");
            return 1;
        }
        if (input_path != 0) {
            tool_usage("gcc", "only one input file is supported");
            return 1;
        }
        input_path = argv[index];
    }
    if (input_path == 0) {
        tool_usage("gcc", "gcc source.c -S [-o out.mas]");
        return 1;
    }
    if (output_path == 0) {
        if (!tool_default_output(input_path,
                                 assemble_only ? ".mas" : ".exe",
                                 default_output,
                                 sizeof(default_output))) {
            fputs("gcc: cannot derive output path\r\n");
            return 1;
        }
        output_path = default_output;
    }

    /* Decide: .exe output (or no -S) triggers the one-shot pipeline. */
    {
        uint32_t olen = (uint32_t) strlen(output_path);
        bool is_exe = olen > 4 &&
                      strcasecmp(output_path + olen - 4, ".exe") == 0;

        if (!assemble_only && is_exe) {
            return gcc_compile_file(input_path, output_path, true) ? 0 : 1;
        }
    }
    return gcc_compile_file(input_path, output_path, false) ? 0 : 1;
}
