#include "toolchain_format.h"
#include "toolchain_util.h"

#define LD_IMAGE_BASE       0x04000000ULL
#define LD_TEXT_RVA         0x00001000U
#define LD_FILE_ALIGNMENT   0x00000200U
#define LD_SECTION_ALIGNMENT 0x00001000U
#define LD_HEADER_SIZE       0x00000200U
#define LD_PE_OFFSET         0x00000080U
#define LD_MAX_IMAGE         131072U
#define LD_SYS_HANDLE_WRITE 10U
#define LD_SYS_EXIT_PROCESS 17U

static uint32_t ld_align_up(uint32_t value, uint32_t alignment)
{
    return (value + alignment - 1U) & ~(alignment - 1U);
}

static void ld_write_u16(uint8_t *where, uint16_t value)
{
    where[0] = (uint8_t) value;
    where[1] = (uint8_t) (value >> 8);
}

static void ld_write_u32(uint8_t *where, uint32_t value)
{
    where[0] = (uint8_t) value;
    where[1] = (uint8_t) (value >> 8);
    where[2] = (uint8_t) (value >> 16);
    where[3] = (uint8_t) (value >> 24);
}

static void ld_write_u64(uint8_t *where, uint64_t value)
{
    ld_write_u32(where, (uint32_t) value);
    ld_write_u32(where + 4, (uint32_t) (value >> 32));
}

static bool ld_emit_byte(uint8_t *code, uint32_t capacity, uint32_t *used, uint8_t value)
{
    if (*used >= capacity) {
        return false;
    }
    code[(*used)++] = value;
    return true;
}

static bool ld_emit_u32(uint8_t *code, uint32_t capacity, uint32_t *used, uint32_t value)
{
    if (*used + 4U > capacity) {
        return false;
    }
    ld_write_u32(code + *used, value);
    *used += 4U;
    return true;
}

static bool ld_emit_u64(uint8_t *code, uint32_t capacity, uint32_t *used, uint64_t value)
{
    if (*used + 8U > capacity) {
        return false;
    }
    ld_write_u64(code + *used, value);
    *used += 8U;
    return true;
}

static bool ld_emit_write(uint8_t *code,
                          uint32_t code_capacity,
                          uint32_t *code_size,
                          uint32_t rdata_rva,
                          uint32_t rdata_offset,
                          uint32_t string_length)
{
    if (!ld_emit_byte(code, code_capacity, code_size, 0xB8) ||
        !ld_emit_u32(code, code_capacity, code_size, LD_SYS_HANDLE_WRITE) ||
        !ld_emit_byte(code, code_capacity, code_size, 0xBB) ||
        !ld_emit_u32(code, code_capacity, code_size, STDOUT_FILENO) ||
        !ld_emit_byte(code, code_capacity, code_size, 0x48) ||
        !ld_emit_byte(code, code_capacity, code_size, 0xB9) ||
        !ld_emit_u64(code,
                     code_capacity,
                     code_size,
                     LD_IMAGE_BASE + rdata_rva + rdata_offset) ||
        !ld_emit_byte(code, code_capacity, code_size, 0xBA) ||
        !ld_emit_u32(code, code_capacity, code_size, string_length) ||
        !ld_emit_byte(code, code_capacity, code_size, 0xCD) ||
        !ld_emit_byte(code, code_capacity, code_size, 0x80)) {
        return false;
    }
    return true;
}

static bool ld_emit_exit(uint8_t *code,
                         uint32_t code_capacity,
                         uint32_t *code_size,
                         int32_t exit_code)
{
    if (!ld_emit_byte(code, code_capacity, code_size, 0xB8) ||
        !ld_emit_u32(code, code_capacity, code_size, LD_SYS_EXIT_PROCESS) ||
        !ld_emit_byte(code, code_capacity, code_size, 0xBB) ||
        !ld_emit_u32(code, code_capacity, code_size, (uint32_t) exit_code) ||
        !ld_emit_byte(code, code_capacity, code_size, 0xCD) ||
        !ld_emit_byte(code, code_capacity, code_size, 0x80)) {
        return false;
    }
    return true;
}

static bool ld_write_section(uint8_t *header,
                             const char *name,
                             uint32_t virtual_size,
                             uint32_t virtual_address,
                             uint32_t raw_size,
                             uint32_t raw_pointer,
                             uint32_t characteristics)
{
    memset(header, 0, 40);
    memcpy(header, name, strlen(name));
    ld_write_u32(header + 8, virtual_size);
    ld_write_u32(header + 12, virtual_address);
    ld_write_u32(header + 16, raw_size);
    ld_write_u32(header + 20, raw_pointer);
    ld_write_u32(header + 36, characteristics);
    return true;
}

static bool ld_write_pe(const char *output_path,
                        const uint8_t *code,
                        uint32_t code_size,
                        const uint8_t *rdata,
                        uint32_t rdata_size)
{
    static uint8_t image[LD_MAX_IMAGE];
    uint8_t *pe;
    uint8_t *optional;
    uint8_t *sections;
    uint32_t text_raw_size = ld_align_up(code_size, LD_FILE_ALIGNMENT);
    uint32_t text_virtual_size = ld_align_up(code_size, LD_SECTION_ALIGNMENT);
    uint32_t rdata_raw_size = ld_align_up(rdata_size, LD_FILE_ALIGNMENT);
    uint32_t rdata_virtual_size;
    uint32_t rdata_rva;
    uint32_t rdata_raw_pointer;
    uint32_t image_size;
    uint32_t total_size;

    if (rdata_raw_size == 0) {
        rdata_raw_size = LD_FILE_ALIGNMENT;
    }
    rdata_virtual_size = ld_align_up(rdata_size, LD_SECTION_ALIGNMENT);
    if (rdata_virtual_size == 0) {
        rdata_virtual_size = LD_SECTION_ALIGNMENT;
    }
    rdata_rva = LD_TEXT_RVA + text_virtual_size;
    rdata_raw_pointer = LD_HEADER_SIZE + text_raw_size;
    image_size = ld_align_up(rdata_rva + rdata_virtual_size, LD_SECTION_ALIGNMENT);
    total_size = rdata_raw_pointer + rdata_raw_size;
    if (total_size > sizeof(image) || image_size >= 0x00400000U) {
        fputs("ld: generated image is too large\r\n");
        return false;
    }

    memset(image, 0, total_size);
    image[0] = 'M';
    image[1] = 'Z';
    ld_write_u32(image + 0x3C, LD_PE_OFFSET);
    pe = image + LD_PE_OFFSET;
    pe[0] = 'P';
    pe[1] = 'E';
    pe[2] = 0;
    pe[3] = 0;
    ld_write_u16(pe + 4, 0x8664);
    ld_write_u16(pe + 6, 2);
    ld_write_u16(pe + 20, 240);
    ld_write_u16(pe + 22, 0x0002);

    optional = pe + 24;
    ld_write_u16(optional, 0x020B);
    ld_write_u32(optional + 4, text_raw_size);
    ld_write_u32(optional + 8, rdata_raw_size);
    ld_write_u32(optional + 16, LD_TEXT_RVA);
    ld_write_u32(optional + 20, LD_TEXT_RVA);
    ld_write_u64(optional + 24, LD_IMAGE_BASE);
    ld_write_u32(optional + 32, LD_SECTION_ALIGNMENT);
    ld_write_u32(optional + 36, LD_FILE_ALIGNMENT);
    ld_write_u16(optional + 48, 6);
    ld_write_u32(optional + 56, image_size);
    ld_write_u32(optional + 60, LD_HEADER_SIZE);
    ld_write_u16(optional + 68, APP_SUBSYSTEM_CONSOLE);
    ld_write_u64(optional + 72, 0x00100000ULL);
    ld_write_u64(optional + 80, 0x00001000ULL);
    ld_write_u64(optional + 88, 0x00100000ULL);
    ld_write_u64(optional + 96, 0x00001000ULL);
    ld_write_u32(optional + 108, 16);

    sections = optional + 240;
    ld_write_section(sections,
                     ".text",
                     code_size,
                     LD_TEXT_RVA,
                     text_raw_size,
                     LD_HEADER_SIZE,
                     0x60000020U);
    ld_write_section(sections + 40,
                     ".rdata",
                     rdata_size,
                     rdata_rva,
                     rdata_raw_size,
                     rdata_raw_pointer,
                     0x40000040U);
    memcpy(image + LD_HEADER_SIZE, code, code_size);
    if (rdata_size > 0) {
        memcpy(image + rdata_raw_pointer, rdata, rdata_size);
    }
    return tool_write_file(output_path, image, total_size);
}

static bool ld_link(const char *input_path, const char *output_path)
{
    static uint8_t object[TOOLCHAIN_FILE_MAX];
    static uint8_t code[16384];
    static uint8_t rdata[16384];
    uint32_t object_size;
    uint32_t operation_count;
    uint32_t string_bytes;
    uint32_t operations_end;
    uint32_t code_size = 0;
    uint32_t rdata_size = 0;
    uint32_t code_cursor = 0;
    uint32_t rdata_cursor = 0;
    uint32_t index;
    bool has_exit = false;

    if (!tool_read_file(input_path, object, sizeof(object), &object_size)) {
        fputs("ld: cannot read input object\r\n");
        return false;
    }
    if (object_size < MOBJ_HEADER_SIZE ||
        object[0] != MOBJ_MAGIC_0 ||
        object[1] != MOBJ_MAGIC_1 ||
        object[2] != MOBJ_MAGIC_2 ||
        object[3] != MOBJ_MAGIC_3 ||
        mobj_read_u32(object + 4) != MOBJ_VERSION) {
        fputs("ld: invalid MOBJ object\r\n");
        return false;
    }
    operation_count = mobj_read_u32(object + 8);
    string_bytes = mobj_read_u32(object + 12);
    if (operation_count == 0 || operation_count > MOBJ_MAX_OPS) {
        fputs("ld: invalid operation count\r\n");
        return false;
    }
    operations_end = MOBJ_HEADER_SIZE + operation_count * MOBJ_OP_SIZE;
    if (operations_end > object_size ||
        string_bytes > object_size - operations_end ||
        string_bytes > MOBJ_MAX_STRING_BYTES) {
        fputs("ld: truncated MOBJ object\r\n");
        return false;
    }

    for (index = 0; index < operation_count; index++) {
        const uint8_t *operation = object + MOBJ_HEADER_SIZE + index * MOBJ_OP_SIZE;
        uint32_t type = mobj_read_u32(operation);
        uint32_t string_offset = mobj_read_u32(operation + 8);
        uint32_t string_length = mobj_read_u32(operation + 12);

        if (type == MOBJ_OP_WRITE) {
            if (string_offset > string_bytes ||
                string_length > string_bytes - string_offset ||
                rdata_size + string_length > sizeof(rdata) ||
                code_size + 22U > sizeof(code)) {
                fputs("ld: invalid write operation\r\n");
                return false;
            }
            rdata_size += string_length;
            code_size += 22U;
        } else if (type == MOBJ_OP_EXIT) {
            if (code_size + 12U > sizeof(code)) {
                fputs("ld: code section is too large\r\n");
                return false;
            }
            code_size += 12U;
            has_exit = true;
        } else {
            fputs("ld: unsupported MOBJ operation\r\n");
            return false;
        }
    }
    if (!has_exit) {
        if (code_size + 12U > sizeof(code)) {
            fputs("ld: code section is too large\r\n");
            return false;
        }
        code_size += 12U;
    }

    {
        uint32_t text_virtual_size = ld_align_up(code_size, LD_SECTION_ALIGNMENT);
        uint32_t rdata_rva = LD_TEXT_RVA + text_virtual_size;

        for (index = 0; index < operation_count; index++) {
            const uint8_t *operation = object + MOBJ_HEADER_SIZE + index * MOBJ_OP_SIZE;
            uint32_t type = mobj_read_u32(operation);
            int32_t value = mobj_read_i32(operation + 4);
            uint32_t string_offset = mobj_read_u32(operation + 8);
            uint32_t string_length = mobj_read_u32(operation + 12);

            if (type == MOBJ_OP_WRITE) {
                const uint8_t *strings = object + operations_end;

                if (!ld_emit_write(code,
                                   sizeof(code),
                                   &code_cursor,
                                   rdata_rva,
                                   rdata_cursor,
                                   string_length)) {
                    fputs("ld: cannot emit write instruction\r\n");
                    return false;
                }
                memcpy(rdata + rdata_cursor,
                       strings + string_offset,
                       string_length);
                rdata_cursor += string_length;
            } else if (type == MOBJ_OP_EXIT &&
                       !ld_emit_exit(code, sizeof(code), &code_cursor, value)) {
                fputs("ld: cannot emit exit instruction\r\n");
                return false;
            }
        }
        if (!has_exit && !ld_emit_exit(code, sizeof(code), &code_cursor, 0)) {
            fputs("ld: cannot emit default exit instruction\r\n");
            return false;
        }
    }
    if (code_cursor != code_size || rdata_cursor != rdata_size) {
        fputs("ld: internal size mismatch\r\n");
        return false;
    }
    return ld_write_pe(output_path, code, code_size, rdata, rdata_size);
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
        fputs("MoniOS tiny ld 0.1\r\n");
        return 0;
    }
    if (argc == 2 && strcmp(argv[1], "--help") == 0) {
        tool_usage("ld", "ld input.mobj -o output.exe");
        return 0;
    }
    for (index = 1; index < argc; index++) {
        if (strcmp(argv[index], "-o") == 0) {
            if (index + 1 >= argc) {
                tool_usage("ld", "missing output path after -o");
                return 1;
            }
            output_path = argv[++index];
            continue;
        }
        if (argv[index][0] == '-') {
            tool_usage("ld", "only -o, --help and --version are supported");
            return 1;
        }
        if (input_path != 0) {
            tool_usage("ld", "only one input object is supported");
            return 1;
        }
        input_path = argv[index];
    }
    if (input_path == 0) {
        tool_usage("ld", "ld input.mobj -o output.exe");
        return 1;
    }
    if (output_path == 0) {
        if (!tool_default_output(input_path,
                                 ".exe",
                                 default_output,
                                 sizeof(default_output))) {
            fputs("ld: cannot derive output path\r\n");
            return 1;
        }
        output_path = default_output;
    }
    return ld_link(input_path, output_path) ? 0 : 1;
}
