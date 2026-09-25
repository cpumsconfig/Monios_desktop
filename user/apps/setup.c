#include "appsys.h"
#include "stdio.h"
#include "string.h"
#include "unistd.h"
#include "path.h"

#define SETUP_IMAGE_UEFI       "C:\\SYSTEM_UEFI.ZIP"
#define SETUP_IMAGE_MBR        "C:\\SYSTEM_MBR.ZIP"
#define SETUP_BOOT_CONFIG      "C:\\MONIOS.INI"
#define SETUP_BOOT_BIN         "C:\\BOOT.BIN"
#define SETUP_AUTH_PATH        "C:\\Monios\\System\\Config\\pwd.txt"
#define SETUP_LOG_PATH         "C:\\install.log"
#define SETUP_SALT             "monios"
#define SETUP_SHA256_BLOCK     64U
#define SETUP_SHA256_DIGEST    32U
#define SETUP_PASSWORD_MAX     48U
#define SETUP_SCREEN_W         1024U
#define SETUP_SCREEN_H         768U
#define SETUP_PANEL_X          172U
#define SETUP_PANEL_Y          116U
#define SETUP_PANEL_W          680U
#define SETUP_PANEL_H          470U
#define SETUP_LOG_LINES        8U
#define SETUP_LOG_TEXT_MAX     88U
#define SETUP_PLAN_TEXT_MAX    96U
#define SETUP_REPORT_MAX       1024U
#define SETUP_DEFAULT_PART_LBA 2048U
#define SETUP_TICKS_PER_SEC    100U
#define SETUP_FAT32_TOTAL_SECTORS 202752U
#define SETUP_FAT32_RESERVED_SECTORS 32U
#define SETUP_FAT32_FAT_COUNT 2U
#define SETUP_FAT32_FAT_SIZE 1576U
#define SETUP_FAT32_SECTORS_PER_CLUSTER 32U
#define SETUP_FAT32_ROOT_CLUSTER 2U
#define SETUP_FAT32_DATA_LBA (SETUP_FAT32_RESERVED_SECTORS + SETUP_FAT32_FAT_COUNT * SETUP_FAT32_FAT_SIZE)
#define SETUP_ZIP_LOCAL_SIG   0x04034B50U
#define SETUP_ZIP_CENTRAL_SIG 0x02014B50U
#define SETUP_ZIP_END_SIG     0x06054B50U
#define SETUP_ZIP_METHOD_STORE 0U

typedef struct {
    uint32_t state[8];
    uint64_t bit_count;
    uint8_t buffer[SETUP_SHA256_BLOCK];
    uint32_t buffer_len;
} setup_sha256_t;

static const uint32_t g_sha256_init[8] = {
    0x6A09E667U, 0xBB67AE85U, 0x3C6EF372U, 0xA54FF53AU,
    0x510E527FU, 0x9B05688CU, 0x1F83D9ABU, 0x5BE0CD19U
};

static const uint32_t g_sha256_k[64] = {
    0x428A2F98U, 0x71374491U, 0xB5C0FBCFU, 0xE9B5DBA5U,
    0x3956C25BU, 0x59F111F1U, 0x923F82A4U, 0xAB1C5ED5U,
    0xD807AA98U, 0x12835B01U, 0x243185BEU, 0x550C7DC3U,
    0x72BE5D74U, 0x80DEB1FEU, 0x9BDC06A7U, 0xC19BF174U,
    0xE49B69C1U, 0xEFBE4786U, 0x0FC19DC6U, 0x240CA1CCU,
    0x2DE92C6FU, 0x4A7484AAU, 0x5CB0A9DCU, 0x76F988DAU,
    0x983E5152U, 0xA831C66DU, 0xB00327C8U, 0xBF597FC7U,
    0xC6E00BF3U, 0xD5A79147U, 0x06CA6351U, 0x14292967U,
    0x27B70A85U, 0x2E1B2138U, 0x4D2C6DFCU, 0x53380D13U,
    0x650A7354U, 0x766A0ABBU, 0x81C2C92EU, 0x92722C85U,
    0xA2BFE8A1U, 0xA81A664BU, 0xC24B8B70U, 0xC76C51A3U,
    0xD192E819U, 0xD6990624U, 0xF40E3585U, 0x106AA070U,
    0x19A4C116U, 0x1E376C08U, 0x2748774CU, 0x34B0BCB5U,
    0x391C0CB3U, 0x4ED8AA4AU, 0x5B9CCA4FU, 0x682E6FF3U,
    0x748F82EEU, 0x78A5636FU, 0x84C87814U, 0x8CC70208U,
    0x90BEFFFAU, 0xA4506CEBU, 0xBEF9A3F7U, 0xC67178F2U
};

typedef enum {
    SETUP_INPUT_PASSWORD,
    SETUP_INPUT_CONFIRM_PASSWORD,
    SETUP_INPUT_CONFIRM_INSTALL,
    SETUP_INPUT_TARGET_SELECT
} setup_input_screen_t;

typedef enum {
    SETUP_TARGET_UNKNOWN,
    SETUP_TARGET_UEFI,
    SETUP_TARGET_MBR
} setup_target_mode_t;

typedef struct {
    setup_input_screen_t screen;
    const char *first_password;
    const char *message;
} setup_input_ui_t;

typedef struct {
    setup_target_mode_t mode;
    const char *image_path;
    const char *label;
} setup_target_t;

typedef struct {
    app_installer_target_info_t info;
    char label[96];
    char details[96];
    bool selectable;
} setup_disk_target_t;

typedef struct {
    setup_target_t media;
    app_installer_target_info_t target;
    uint32_t image_size;
    uint32_t copy_bytes;
    uint32_t free_mb;
    bool create_partition;
    char target_label[96];
} setup_install_plan_t;

static char g_setup_logs[SETUP_LOG_LINES][SETUP_LOG_TEXT_MAX];
static uint32_t g_setup_log_count;
static setup_disk_target_t g_setup_disk_targets[APP_INSTALLER_MAX_TARGETS];
static uint32_t g_setup_disk_target_count;
static const char *g_setup_confirm_target_label = "selected target";
static char g_setup_disk_model[48];
static uint32_t g_setup_disk_mb;
static uint32_t g_setup_image_mb;
static char g_setup_progress_detail[96];

static void setup_line(const char *text)
{
    fputs(text);
    fputs("\r\n");
}

static void setup_log_clear(void)
{
    g_setup_log_count = 0;
    for (uint32_t i = 0; i < SETUP_LOG_LINES; i++) {
        g_setup_logs[i][0] = '\0';
    }
}

static void setup_copy_log(char *dst, const char *src)
{
    uint32_t i = 0;

    if (dst == 0) {
        return;
    }
    if (src == 0) {
        dst[0] = '\0';
        return;
    }
    while (src[i] != '\0' && i + 1 < SETUP_LOG_TEXT_MAX) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static void setup_log_add(const char *text)
{
    if (text == 0 || text[0] == '\0') {
        return;
    }
    if (g_setup_log_count > 0 &&
        strcmp(g_setup_logs[g_setup_log_count - 1], text) == 0) {
        return;
    }
    if (g_setup_log_count == SETUP_LOG_LINES) {
        for (uint32_t i = 1; i < SETUP_LOG_LINES; i++) {
            strcpy(g_setup_logs[i - 1], g_setup_logs[i]);
        }
        g_setup_log_count--;
    }
    setup_copy_log(g_setup_logs[g_setup_log_count++], text);
}

static bool setup_text_has_token(const char *text, const char *token)
{
    uint32_t token_len;

    if (text == 0 || token == 0 || token[0] == '\0') {
        return false;
    }
    token_len = (uint32_t) strlen(token);
    for (uint32_t i = 0; text[i] != '\0'; i++) {
        uint32_t j = 0;

        while (j < token_len) {
            char a = text[i + j];
            char b = token[j];

            if (a >= 'A' && a <= 'Z') {
                a = (char) (a + ('a' - 'A'));
            }
            if (b >= 'A' && b <= 'Z') {
                b = (char) (b + ('a' - 'A'));
            }
            if (a != b) {
                break;
            }
            j++;
        }
        if (j == token_len) {
            return true;
        }
    }
    return false;
}

static const char *setup_env_get(const char *name)
{
    const app_launch_info_t *info = app_launch_info();
    uint32_t name_len;

    if (info == 0 || name == 0 || info->env == 0) {
        return 0;
    }
    name_len = (uint32_t) strlen(name);
    for (uint32_t i = 0; i < info->env_count; i++) {
        const char *pair = info->env[i];
        uint32_t j = 0;

        if (pair == 0) {
            continue;
        }
        while (j < name_len && pair[j] == name[j]) {
            j++;
        }
        if (j == name_len && pair[j] == '=') {
            return pair + j + 1;
        }
    }
    return 0;
}

static setup_target_mode_t setup_mode_from_text(const char *text)
{
    if (setup_text_has_token(text, "uefi")) {
        return SETUP_TARGET_UEFI;
    }
    if (setup_text_has_token(text, "mbr") ||
        setup_text_has_token(text, "bios") ||
        setup_text_has_token(text, "legacy")) {
        return SETUP_TARGET_MBR;
    }
    return SETUP_TARGET_UNKNOWN;
}

static setup_target_t setup_target_for_mode(setup_target_mode_t mode)
{
    setup_target_t target;

    target.mode = SETUP_TARGET_UNKNOWN;
    target.image_path = 0;
    target.label = "Unknown";
    if (mode == SETUP_TARGET_UEFI) {
        target.mode = SETUP_TARGET_UEFI;
        target.image_path = SETUP_IMAGE_UEFI;
        target.label = "UEFI";
    } else if (mode == SETUP_TARGET_MBR) {
        target.mode = SETUP_TARGET_MBR;
        target.image_path = SETUP_IMAGE_MBR;
        target.label = "BIOS/MBR";
    }
    return target;
}

static int32_t setup_media_file_size(const char *path)
{
    return app_installer_media_size(path);
}

static bool setup_media_file_exists(const char *path)
{
    return setup_media_file_size(path) >= 0;
}

static uint32_t setup_rotr(uint32_t value, uint32_t shift)
{
    return (value >> shift) | (value << (32U - shift));
}

static uint32_t setup_load_be32(const uint8_t *data)
{
    return ((uint32_t) data[0] << 24) |
           ((uint32_t) data[1] << 16) |
           ((uint32_t) data[2] << 8) |
           (uint32_t) data[3];
}

static void setup_store_be32(uint8_t *data, uint32_t value)
{
    data[0] = (uint8_t) (value >> 24);
    data[1] = (uint8_t) (value >> 16);
    data[2] = (uint8_t) (value >> 8);
    data[3] = (uint8_t) value;
}

static void setup_sha256_transform(setup_sha256_t *ctx, const uint8_t block[SETUP_SHA256_BLOCK])
{
    uint32_t w[64];
    uint32_t a;
    uint32_t b;
    uint32_t c;
    uint32_t d;
    uint32_t e;
    uint32_t f;
    uint32_t g;
    uint32_t h;

    for (uint32_t i = 0; i < 16; i++) {
        w[i] = setup_load_be32(block + i * 4U);
    }
    for (uint32_t i = 16; i < 64; i++) {
        uint32_t s0 = setup_rotr(w[i - 15], 7) ^ setup_rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
        uint32_t s1 = setup_rotr(w[i - 2], 17) ^ setup_rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }

    a = ctx->state[0];
    b = ctx->state[1];
    c = ctx->state[2];
    d = ctx->state[3];
    e = ctx->state[4];
    f = ctx->state[5];
    g = ctx->state[6];
    h = ctx->state[7];

    for (uint32_t i = 0; i < 64; i++) {
        uint32_t s1 = setup_rotr(e, 6) ^ setup_rotr(e, 11) ^ setup_rotr(e, 25);
        uint32_t ch = (e & f) ^ ((~e) & g);
        uint32_t temp1 = h + s1 + ch + g_sha256_k[i] + w[i];
        uint32_t s0 = setup_rotr(a, 2) ^ setup_rotr(a, 13) ^ setup_rotr(a, 22);
        uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        uint32_t temp2 = s0 + maj;

        h = g;
        g = f;
        f = e;
        e = d + temp1;
        d = c;
        c = b;
        b = a;
        a = temp1 + temp2;
    }

    ctx->state[0] += a;
    ctx->state[1] += b;
    ctx->state[2] += c;
    ctx->state[3] += d;
    ctx->state[4] += e;
    ctx->state[5] += f;
    ctx->state[6] += g;
    ctx->state[7] += h;
}

static void setup_sha256_init(setup_sha256_t *ctx)
{
    memcpy(ctx->state, g_sha256_init, sizeof(g_sha256_init));
    ctx->bit_count = 0;
    ctx->buffer_len = 0;
}

static void setup_sha256_update(setup_sha256_t *ctx, const uint8_t *data, uint32_t size)
{
    while (size > 0) {
        uint32_t chunk = SETUP_SHA256_BLOCK - ctx->buffer_len;

        if (chunk > size) {
            chunk = size;
        }
        memcpy(ctx->buffer + ctx->buffer_len, data, chunk);
        ctx->buffer_len += chunk;
        data += chunk;
        size -= chunk;
        ctx->bit_count += (uint64_t) chunk * 8ULL;
        if (ctx->buffer_len == SETUP_SHA256_BLOCK) {
            setup_sha256_transform(ctx, ctx->buffer);
            ctx->buffer_len = 0;
        }
    }
}

static void setup_sha256_final(setup_sha256_t *ctx, uint8_t digest[SETUP_SHA256_DIGEST])
{
    uint64_t bits = ctx->bit_count;
    uint8_t pad = 0x80;
    uint8_t zero = 0;
    uint8_t len[8];

    setup_sha256_update(ctx, &pad, 1);
    while (ctx->buffer_len != 56) {
        setup_sha256_update(ctx, &zero, 1);
    }
    for (uint32_t i = 0; i < 8; i++) {
        len[7 - i] = (uint8_t) (bits >> (i * 8U));
    }
    setup_sha256_update(ctx, len, sizeof(len));

    for (uint32_t i = 0; i < 8; i++) {
        setup_store_be32(digest + i * 4U, ctx->state[i]);
    }
}

static uint32_t setup_base64_encode(const uint8_t *data, uint32_t size, char *out, uint32_t out_size)
{
    static const char table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    uint32_t pos = 0;

    for (uint32_t i = 0; i < size; i += 3) {
        uint32_t remaining = size - i;
        uint32_t triple = ((uint32_t) data[i] << 16);

        if (remaining > 1) {
            triple |= (uint32_t) data[i + 1] << 8;
        }
        if (remaining > 2) {
            triple |= data[i + 2];
        }
        if (pos + 4 >= out_size) {
            return 0;
        }
        out[pos++] = table[(triple >> 18) & 0x3F];
        out[pos++] = table[(triple >> 12) & 0x3F];
        out[pos++] = remaining > 1 ? table[(triple >> 6) & 0x3F] : '=';
        out[pos++] = remaining > 2 ? table[triple & 0x3F] : '=';
    }
    if (pos >= out_size) {
        return 0;
    }
    out[pos] = '\0';
    return pos;
}

static bool setup_make_password_file(const char *password, char *out, uint32_t out_size)
{
    setup_sha256_t sha;
    uint8_t digest[SETUP_SHA256_DIGEST];
    char hash[48];
    uint32_t pos = 0;

    if (password == 0 || password[0] == '\0' || out_size < 64) {
        return false;
    }
    setup_sha256_init(&sha);
    setup_sha256_update(&sha, (const uint8_t *) SETUP_SALT, (uint32_t) strlen(SETUP_SALT));
    setup_sha256_update(&sha, (const uint8_t *) password, (uint32_t) strlen(password));
    setup_sha256_final(&sha, digest);
    if (setup_base64_encode(digest, sizeof(digest), hash, sizeof(hash)) == 0) {
        return false;
    }
    strcpy(out, SETUP_SALT);
    pos = (uint32_t) strlen(out);
    out[pos++] = '$';
    out[pos] = '\0';
    strcpy(out + pos, hash);
    pos = (uint32_t) strlen(out);
    out[pos++] = '\r';
    out[pos++] = '\n';
    out[pos] = '\0';
    return true;
}

static void setup_u32_to_dec(char *out, uint32_t value)
{
    char temp[12];
    uint32_t pos = 0;

    if (value == 0) {
        out[0] = '0';
        out[1] = '\0';
        return;
    }
    while (value > 0 && pos < sizeof(temp)) {
        temp[pos++] = (char) ('0' + (value % 10U));
        value /= 10U;
    }
    for (uint32_t i = 0; i < pos; i++) {
        out[i] = temp[pos - 1U - i];
    }
    out[pos] = '\0';
}

static bool setup_parse_index(const char *text, uint32_t max_value, uint32_t *out)
{
    uint32_t value = 0;
    uint32_t i = 0;

    if (text == 0 || text[0] == '\0' || out == 0) {
        return false;
    }
    while (text[i] != '\0') {
        if (text[i] < '0' || text[i] > '9') {
            return false;
        }
        value = value * 10U + (uint32_t) (text[i] - '0');
        if (value > max_value) {
            return false;
        }
        i++;
    }
    *out = value;
    return true;
}

static bool setup_text_equals_ignore_case(const char *left, const char *right)
{
    uint32_t i = 0;

    if (left == 0 || right == 0) {
        return false;
    }
    while (left[i] != '\0' && right[i] != '\0') {
        char a = left[i];
        char b = right[i];

        if (a >= 'A' && a <= 'Z') {
            a = (char) (a + ('a' - 'A'));
        }
        if (b >= 'A' && b <= 'Z') {
            b = (char) (b + ('a' - 'A'));
        }
        if (a != b) {
            return false;
        }
        i++;
    }
    return left[i] == '\0' && right[i] == '\0';
}

static void setup_append_text(char *dst, uint32_t dst_size, const char *src)
{
    uint32_t len;
    uint32_t pos = 0;

    if (dst == 0 || dst_size == 0 || src == 0) {
        return;
    }
    len = (uint32_t) strlen(dst);
    while (src[pos] != '\0' && len + 1U < dst_size) {
        dst[len++] = src[pos++];
    }
    dst[len] = '\0';
}

static void setup_append_u32(char *dst, uint32_t dst_size, uint32_t value)
{
    char number[12];

    setup_u32_to_dec(number, value);
    setup_append_text(dst, dst_size, number);
}

static uint32_t setup_sector_mb(uint32_t sectors)
{
    return sectors / 2048U;
}

static uint32_t setup_bytes_mb(uint32_t bytes)
{
    return (bytes + 1024U * 1024U - 1U) / (1024U * 1024U);
}

static uint32_t setup_min_u32(uint32_t a, uint32_t b)
{
    return a < b ? a : b;
}

static bool setup_partition_type_supported(uint8_t type)
{
    return type == 0x0B || type == 0x0C || type == 0x07 || type == 0x83 || type == 0xEF;
}

static bool setup_target_supported(const app_installer_target_info_t *target)
{
    if (target == 0 || target->sector_count == 0) {
        return false;
    }
    if (target->kind == APP_INSTALLER_TARGET_KIND_UEFI_ESP ||
        target->kind == APP_INSTALLER_TARGET_KIND_DISK) {
        return true;
    }
    return setup_partition_type_supported(target->partition_type);
}

static const char *setup_target_kind_name(const app_installer_target_info_t *target)
{
    if (target == 0) {
        return "Unknown";
    }
    if (target->kind == APP_INSTALLER_TARGET_KIND_UEFI_ESP) {
        return "Create FAT32 partition";
    }
    if (target->kind == APP_INSTALLER_TARGET_KIND_DISK) {
        return "Whole disk";
    }
    return "Existing partition";
}

static void setup_append_partition_type(char *out, uint32_t out_size, uint8_t type)
{
    if (type == 0) {
        setup_append_text(out, out_size, "none");
    } else if (type == 0xEF) {
        setup_append_text(out, out_size, "EF");
    } else {
        setup_append_u32(out, out_size, type);
    }
}

static void setup_make_target_label(const app_installer_target_info_t *target,
                                    char *out,
                                    uint32_t out_size)
{
    if (out == 0 || out_size == 0) {
        return;
    }
    out[0] = '\0';
    if (target == 0) {
        setup_append_text(out, out_size, "Unknown target");
        return;
    }
    if (target->kind == APP_INSTALLER_TARGET_KIND_UEFI_ESP) {
        setup_append_text(out, out_size, "Create FAT32 partition (");
        setup_append_u32(out, out_size, setup_sector_mb(target->sector_count));
        setup_append_text(out, out_size, " MB)");
    } else if (target->kind == APP_INSTALLER_TARGET_KIND_DISK) {
        setup_append_text(out, out_size, "Whole disk (");
        setup_append_u32(out, out_size, setup_sector_mb(target->sector_count));
        setup_append_text(out, out_size, " MB)");
    } else {
        setup_append_text(out, out_size, "Partition ");
        setup_append_u32(out, out_size, (uint32_t) target->partition_index + 1U);
        setup_append_text(out, out_size, " (");
        setup_append_u32(out, out_size, setup_sector_mb(target->sector_count));
        setup_append_text(out, out_size, " MB)");
    }
    if (!setup_target_supported(target)) {
        setup_append_text(out, out_size, "  unsupported");
    }
}

static void setup_make_target_details(const app_installer_target_info_t *target,
                                      char *out,
                                      uint32_t out_size)
{
    if (out == 0 || out_size == 0) {
        return;
    }
    out[0] = '\0';
    if (target == 0) {
        return;
    }
    setup_append_text(out, out_size, setup_target_kind_name(target));
    setup_append_text(out, out_size, " | LBA ");
    setup_append_u32(out, out_size, target->start_lba);
    setup_append_text(out, out_size, " | type ");
    setup_append_partition_type(out, out_size, target->partition_type);
    if (target->kind == APP_INSTALLER_TARGET_KIND_UEFI_ESP) {
        setup_append_text(out, out_size, " | recommended");
    } else if (target->kind == APP_INSTALLER_TARGET_KIND_PART) {
        setup_append_text(out, out_size, " | ");
        setup_append_text(out, out_size, target->active != 0 ? "bootable" : "not bootable");
        if (!setup_partition_type_supported(target->partition_type)) {
            setup_append_text(out, out_size, " | unsupported");
        }
    } else if (!setup_target_supported(target)) {
        setup_append_text(out, out_size, " | cannot install");
    }
}

static void setup_mask_text(const char *src, char *dst, uint32_t dst_size)
{
    uint32_t len = 0;

    if (dst_size == 0) {
        return;
    }
    while (src != 0 && src[len] != '\0' && len + 1U < dst_size) {
        dst[len] = '*';
        len++;
    }
    dst[len] = '\0';
}

static void setup_draw_outline(uint16_t x, uint16_t y, uint16_t width, uint16_t height, uint32_t color)
{
    app_graphics_fill_rect(x, y, width, 1, color);
    app_graphics_fill_rect(x, (uint16_t) (y + height - 1), width, 1, color);
    app_graphics_fill_rect(x, y, 1, height, color);
    app_graphics_fill_rect((uint16_t) (x + width - 1), y, 1, height, color);
}

static void setup_draw_base(const char *title, const char *subtitle)
{
    app_graphics_fill_rect(0, 0, SETUP_SCREEN_W, SETUP_SCREEN_H, 0x00182A3A);
    app_graphics_fill_rect(0, 0, SETUP_SCREEN_W, 88, 0x000F2438);
    app_graphics_fill_rect(0, 88, SETUP_SCREEN_W, 4, 0x002F80ED);
    app_graphics_fill_rect(SETUP_PANEL_X, SETUP_PANEL_Y, SETUP_PANEL_W, SETUP_PANEL_H, 0x00F4F8FC);
    setup_draw_outline(SETUP_PANEL_X, SETUP_PANEL_Y, SETUP_PANEL_W, SETUP_PANEL_H, 0x0095A8BA);
    app_graphics_draw_text(64, 30, "MoniOS Setup", 0x00FFFFFF);
    app_graphics_draw_text((uint16_t) (SETUP_PANEL_X + 44), (uint16_t) (SETUP_PANEL_Y + 38), title, 0x0017232E);
    if (subtitle != 0 && subtitle[0] != '\0') {
        app_graphics_draw_text((uint16_t) (SETUP_PANEL_X + 44), (uint16_t) (SETUP_PANEL_Y + 70), subtitle, 0x004A6278);
    }
}

static void setup_draw_input_box(uint16_t x, uint16_t y, uint16_t width, const char *value, bool focus, bool secret)
{
    char masked[SETUP_PASSWORD_MAX];
    const char *display = value != 0 ? value : "";

    if (secret) {
        setup_mask_text(value, masked, sizeof(masked));
        display = masked;
    }
    app_graphics_fill_rect(x, y, width, 34, 0x00FFFFFF);
    setup_draw_outline(x, y, width, 34, focus ? 0x002F80ED : 0x0095A8BA);
    app_graphics_draw_text((uint16_t) (x + 12), (uint16_t) (y + 10), display, 0x0017232E);
}

static void setup_draw_button(uint16_t x, uint16_t y, uint16_t width, const char *label)
{
    app_graphics_fill_rect(x, y, width, 34, 0x002F80ED);
    setup_draw_outline(x, y, width, 34, 0x001F5FB8);
    app_graphics_draw_text((uint16_t) (x + 18), (uint16_t) (y + 10), label, 0x00FFFFFF);
}

static void setup_draw_error(const char *message)
{
    setup_draw_base("Installer cannot continue", message);
    app_graphics_fill_rect(236, 282, 552, 64, 0x00FDE2E2);
    setup_draw_outline(236, 282, 552, 64, 0x00D94C5A);
    app_graphics_draw_text(260, 306, message != 0 ? message : "Unknown setup error", 0x00A82636);
    app_graphics_present();
}

static void setup_draw_logs(void)
{
    uint16_t x = 232;
    uint16_t y = 414;
    uint16_t width = 560;
    uint16_t height = 118;

    app_graphics_draw_text(x, (uint16_t) (y - 22), "Installation log", 0x0017232E);
    app_graphics_fill_rect(x, y, width, height, 0x00FFFFFF);
    setup_draw_outline(x, y, width, height, 0x0095A8BA);
    for (uint32_t i = 0; i < g_setup_log_count; i++) {
        app_graphics_draw_text((uint16_t) (x + 12),
                               (uint16_t) (y + 10 + i * 13U),
                               g_setup_logs[i],
                               0x004A6278);
    }
}

static void setup_draw_password_screen(uint32_t stage, const char *first, const char *current, const char *message)
{
    setup_draw_base("Create root password", "This password will be written to the installed hard disk.");
    app_graphics_draw_text(236, 238, "Password", 0x004A6278);
    setup_draw_input_box(236, 262, 420, first, stage == 0, true);
    app_graphics_draw_text(236, 326, "Confirm password", 0x004A6278);
    setup_draw_input_box(236, 350, 420, current, stage == 1, true);
    if (message != 0 && message[0] != '\0') {
        app_graphics_draw_text(236, 416, message, 0x00A82636);
    } else {
        app_graphics_draw_text(236, 416, "Use at least one character. Press Enter after each field.", 0x004A6278);
    }
    app_graphics_present();
}

static void setup_draw_confirm_screen(const char *current)
{
    setup_draw_base("Confirm disk installation", "Installing will overwrite the selected target.");
    app_graphics_fill_rect(236, 238, 552, 80, 0x00FFF4D6);
    setup_draw_outline(236, 238, 552, 80, 0x00D9A441);
    app_graphics_draw_text(260, 264, "Type YES to install MoniOS to:", 0x0017232E);
    app_graphics_draw_text(260, 292, g_setup_confirm_target_label, 0x0017232E);
    app_graphics_draw_text(260, 320, "All existing data on the target may be lost.", 0x00A65F00);
    app_graphics_draw_text(236, 378, "Confirmation", 0x004A6278);
    setup_draw_input_box(356, 366, 180, current, true, false);
    setup_draw_button(556, 366, 112, "Install");
    app_graphics_present();
}

static void setup_draw_target_screen(const char *current, const char *message)
{
    setup_draw_base("Select install target", "Choose a disk or partition number, then press Enter.");
    app_graphics_draw_text(236, 198, "Detected disk", 0x0017232E);
    app_graphics_draw_text(236, 222, g_setup_disk_model, 0x004A6278);
    app_graphics_draw_text(236, 250, "Detected targets", 0x0017232E);
    for (uint32_t i = 0; i < g_setup_disk_target_count; i++) {
        char line[112];
        uint16_t y = (uint16_t) (282 + i * 36U);

        line[0] = '\0';
        setup_append_u32(line, sizeof(line), i + 1U);
        setup_append_text(line, sizeof(line), ". ");
        setup_append_text(line, sizeof(line), g_setup_disk_targets[i].label);
        app_graphics_draw_text(260, y, line, g_setup_disk_targets[i].selectable ? 0x004A6278 : 0x0091A0AD);
        app_graphics_draw_text(286, (uint16_t) (y + 16),
                               g_setup_disk_targets[i].details,
                               g_setup_disk_targets[i].selectable ? 0x00657A8C : 0x0091A0AD);
    }
    if (message != 0 && message[0] != '\0') {
        app_graphics_draw_text(236, 504, message, 0x00A82636);
    } else {
        app_graphics_draw_text(236, 504, "A FAT32 partition will receive files from the selected package.", 0x004A6278);
    }
    app_graphics_draw_text(236, 542, "Target number", 0x004A6278);
    setup_draw_input_box(356, 530, 180, current, true, false);
    setup_draw_button(556, 530, 112, "Select");
    app_graphics_present();
}

static void setup_draw_input_state(const setup_input_ui_t *ui, const char *current)
{
    if (ui == 0) {
        return;
    }
    switch (ui->screen) {
    case SETUP_INPUT_PASSWORD:
        setup_draw_password_screen(0, current, "", ui->message);
        break;
    case SETUP_INPUT_CONFIRM_PASSWORD:
        setup_draw_password_screen(1, ui->first_password, current, ui->message);
        break;
    case SETUP_INPUT_CONFIRM_INSTALL:
        setup_draw_confirm_screen(current);
        break;
    case SETUP_INPUT_TARGET_SELECT:
        setup_draw_target_screen(current, ui->message);
        break;
    }
}

static void setup_draw_progress(uint32_t percent, const char *status)
{
    uint16_t x = 232;
    uint16_t y = 332;
    uint16_t width = 560;
    uint16_t fill;
    char percent_text[24];
    char percent_value[12];

    if (percent > 100) {
        percent = 100;
    }
    setup_draw_base("Installing MoniOS", status);
    app_graphics_draw_text(x, (uint16_t) (y - 42), status != 0 ? status : "Working", 0x0017232E);
    app_graphics_fill_rect(x, y, width, 34, 0x00D7E2EC);
    fill = (uint16_t) ((percent * width) / 100U);
    app_graphics_fill_rect(x, y, fill, 34, 0x002F80ED);
    setup_draw_outline(x, y, width, 34, 0x0095A8BA);
    setup_u32_to_dec(percent_value, percent);
    strcpy(percent_text, percent_value);
    strcpy(percent_text + strlen(percent_text), "%");
    app_graphics_draw_text((uint16_t) (x + 244), (uint16_t) (y + 52), percent_text, 0x004A6278);
    if (g_setup_progress_detail[0] != '\0') {
        app_graphics_draw_text((uint16_t) (SETUP_PANEL_X + 44), (uint16_t) (SETUP_PANEL_Y + 110), g_setup_progress_detail, 0x004A6278);
    }
    setup_draw_logs();
    app_graphics_present();
}

static void setup_print_progress(uint32_t percent, const char *status)
{
    g_setup_progress_detail[0] = '\0';
    setup_log_add(status);
    fputs("[");
    print_uint(percent);
    fputs("%] ");
    setup_line(status);
    setup_draw_progress(percent, status);
}

static void setup_set_progress_detail(uint32_t copied, uint32_t total, uint32_t kb_per_sec)
{
    g_setup_progress_detail[0] = '\0';
    setup_append_u32(g_setup_progress_detail, sizeof(g_setup_progress_detail), copied / (1024U * 1024U));
    setup_append_text(g_setup_progress_detail, sizeof(g_setup_progress_detail), " MB / ");
    setup_append_u32(g_setup_progress_detail, sizeof(g_setup_progress_detail), setup_bytes_mb(total));
    setup_append_text(g_setup_progress_detail, sizeof(g_setup_progress_detail), " MB");
    if (kb_per_sec > 0) {
        setup_append_text(g_setup_progress_detail, sizeof(g_setup_progress_detail), "  ");
        setup_append_u32(g_setup_progress_detail, sizeof(g_setup_progress_detail), kb_per_sec);
        setup_append_text(g_setup_progress_detail, sizeof(g_setup_progress_detail), " KB/s");
    }
}

static void setup_print_copy_progress(uint32_t percent,
                                      const char *status,
                                      uint32_t copied,
                                      uint32_t total,
                                      uint32_t kb_per_sec)
{
    setup_set_progress_detail(copied, total, kb_per_sec);
    setup_log_add(status);
    fputs("[");
    print_uint(percent);
    fputs("%] ");
    setup_line(g_setup_progress_detail);
    setup_draw_progress(percent, status);
}

static uint32_t setup_round_up_512(uint32_t value)
{
    return (value + 511U) & ~511U;
}

static uint16_t setup_load_le16(const uint8_t *data)
{
    return (uint16_t) data[0] | ((uint16_t) data[1] << 8);
}

static uint32_t setup_load_le32(const uint8_t *data)
{
    return (uint32_t) data[0] |
           ((uint32_t) data[1] << 8) |
           ((uint32_t) data[2] << 16) |
           ((uint32_t) data[3] << 24);
}

static void setup_store_le16(uint8_t *data, uint16_t value)
{
    data[0] = (uint8_t) value;
    data[1] = (uint8_t) (value >> 8);
}

static void setup_store_le32(uint8_t *data, uint32_t value)
{
    data[0] = (uint8_t) value;
    data[1] = (uint8_t) (value >> 8);
    data[2] = (uint8_t) (value >> 16);
    data[3] = (uint8_t) (value >> 24);
}

static uint32_t setup_volume_sectors_for_target(const app_installer_target_info_t *target)
{
    if (target == 0 || target->sector_count < SETUP_FAT32_TOTAL_SECTORS) {
        return 0;
    }
    return SETUP_FAT32_TOTAL_SECTORS;
}

static bool setup_prepare_fat32_boot_sector(uint8_t boot_sector[512],
                                            uint32_t hidden_lba,
                                            uint32_t total_sectors)
{
    if (app_installer_read_media(SETUP_BOOT_BIN, 0, boot_sector, 512) != 512) {
        return false;
    }
    setup_store_le16(boot_sector + 11, 512);
    boot_sector[13] = SETUP_FAT32_SECTORS_PER_CLUSTER;
    setup_store_le16(boot_sector + 14, SETUP_FAT32_RESERVED_SECTORS);
    boot_sector[16] = SETUP_FAT32_FAT_COUNT;
    setup_store_le16(boot_sector + 17, 0);
    setup_store_le16(boot_sector + 19, 0);
    boot_sector[21] = 0xF8;
    setup_store_le16(boot_sector + 22, 0);
    setup_store_le16(boot_sector + 24, 63);
    setup_store_le16(boot_sector + 26, 16);
    setup_store_le32(boot_sector + 28, hidden_lba);
    setup_store_le32(boot_sector + 32, total_sectors);
    setup_store_le32(boot_sector + 36, SETUP_FAT32_FAT_SIZE);
    setup_store_le16(boot_sector + 40, 0);
    setup_store_le16(boot_sector + 42, 0);
    setup_store_le32(boot_sector + 44, SETUP_FAT32_ROOT_CLUSTER);
    setup_store_le16(boot_sector + 48, 1);
    setup_store_le16(boot_sector + 50, 6);
    boot_sector[64] = 0x80;
    boot_sector[65] = 0;
    boot_sector[66] = 0x29;
    setup_store_le32(boot_sector + 67, 0x4D4F4E49U);
    memcpy(boot_sector + 71, "MONIOS     ", 11);
    memcpy(boot_sector + 82, "FAT32   ", 8);
    boot_sector[510] = 0x55;
    boot_sector[511] = 0xAA;
    return true;
}

static bool setup_write_zero_sectors(uint32_t start_lba, uint32_t sector_count)
{
    static uint8_t zero[4096];
    uint32_t done = 0;

    memset(zero, 0, sizeof(zero));
    while (done < sector_count) {
        uint32_t sectors = sector_count - done;
        uint32_t bytes;

        if (sectors > sizeof(zero) / 512U) {
            sectors = sizeof(zero) / 512U;
        }
        bytes = sectors * 512U;
        if (app_installer_write_buffer(zero, start_lba + done, bytes) != (int) bytes) {
            return false;
        }
        done += sectors;
    }
    return true;
}

static bool setup_write_fat32_fsinfo(uint32_t lba)
{
    uint8_t fsinfo[512];

    memset(fsinfo, 0, sizeof(fsinfo));
    setup_store_le32(fsinfo + 0, 0x41615252U);
    setup_store_le32(fsinfo + 484, 0x61417272U);
    setup_store_le32(fsinfo + 488, 0xFFFFFFFFU);
    setup_store_le32(fsinfo + 492, 0xFFFFFFFFU);
    fsinfo[510] = 0x55;
    fsinfo[511] = 0xAA;
    return app_installer_write_buffer(fsinfo, lba, sizeof(fsinfo)) == (int) sizeof(fsinfo);
}

static bool setup_format_fat32_partition(const setup_install_plan_t *plan,
                                         const uint8_t boot_sector[512])
{
    uint8_t fat0[512];
    uint32_t target_lba;

    if (plan == 0 || boot_sector == 0 || plan->target.sector_count < SETUP_FAT32_TOTAL_SECTORS) {
        return false;
    }
    target_lba = plan->target.start_lba;
    setup_print_progress(8, "Formatting FAT32 partition");
    if (!setup_write_zero_sectors(target_lba, SETUP_FAT32_RESERVED_SECTORS)) {
        return false;
    }
    if (app_installer_write_buffer(boot_sector, target_lba, 512) != 512 ||
        app_installer_write_buffer(boot_sector, target_lba + 6U, 512) != 512 ||
        !setup_write_fat32_fsinfo(target_lba + 1U) ||
        !setup_write_fat32_fsinfo(target_lba + 7U)) {
        return false;
    }
    if (!setup_write_zero_sectors(target_lba + SETUP_FAT32_RESERVED_SECTORS,
                                  SETUP_FAT32_FAT_COUNT * SETUP_FAT32_FAT_SIZE)) {
        return false;
    }
    memset(fat0, 0, sizeof(fat0));
    setup_store_le32(fat0 + 0, 0x0FFFFFF8U);
    setup_store_le32(fat0 + 4, 0xFFFFFFFFU);
    setup_store_le32(fat0 + 8, 0x0FFFFFFFU);
    for (uint32_t fat = 0; fat < SETUP_FAT32_FAT_COUNT; fat++) {
        uint32_t fat_lba = target_lba + SETUP_FAT32_RESERVED_SECTORS + fat * SETUP_FAT32_FAT_SIZE;

        if (app_installer_write_buffer(fat0, fat_lba, sizeof(fat0)) != (int) sizeof(fat0)) {
            return false;
        }
    }
    return setup_write_zero_sectors(target_lba + SETUP_FAT32_DATA_LBA,
                                    SETUP_FAT32_SECTORS_PER_CLUSTER);
}

static bool setup_write_partition_table(const setup_install_plan_t *plan,
                                        const uint8_t boot_sector[512])
{
    uint8_t mbr[512];
    uint8_t *entry;

    if (plan == 0 || !plan->create_partition) {
        return true;
    }
    if (plan->target.start_lba < SETUP_DEFAULT_PART_LBA ||
        plan->target.sector_count == 0) {
        return false;
    }

    memset(mbr, 0, sizeof(mbr));
    if (plan->media.mode == SETUP_TARGET_MBR && boot_sector != 0) {
        memcpy(mbr, boot_sector, 446);
    }
    entry = mbr + 446;
    entry[0] = 0x80;
    entry[1] = 0x20;
    entry[2] = 0x21;
    entry[3] = 0x00;
    entry[4] = plan->target.partition_type != 0 ? plan->target.partition_type : 0xEF;
    entry[5] = 0xFE;
    entry[6] = 0xFF;
    entry[7] = 0xFF;
    setup_store_le32(entry + 8, plan->target.start_lba);
    setup_store_le32(entry + 12, plan->target.sector_count);
    mbr[510] = 0x55;
    mbr[511] = 0xAA;
    setup_print_progress(3, plan->media.mode == SETUP_TARGET_MBR ? "Writing MBR boot sector" : "Creating disk partition");
    return app_installer_write_buffer(mbr, 0, sizeof(mbr)) == (int) sizeof(mbr);
}

static void setup_read_line(char *buffer, uint32_t size, bool secret, const setup_input_ui_t *ui)
{
    uint32_t len = 0;

    if (size == 0) {
        return;
    }
    buffer[0] = '\0';
    setup_draw_input_state(ui, buffer);
    while (1) {
        char ch;
        int got = read(STDIN_FILENO, &ch, 1);

        if (got <= 0) {
            app_sleep_ticks(1);
            continue;
        }
        if (ch == '\r') {
            continue;
        }
        if (ch == '\n') {
            fputs("\r\n");
            break;
        }
        if (ch == '\b') {
            if (len > 0) {
                len--;
                buffer[len] = '\0';
                fputs("\b \b");
                setup_draw_input_state(ui, buffer);
            }
            continue;
        }
        if ((uint8_t) ch < 32 || len + 1 >= size) {
            continue;
        }
        buffer[len++] = ch;
        buffer[len] = '\0';
        putchar(secret ? '*' : ch);
        setup_draw_input_state(ui, buffer);
    }
}

static bool setup_read_package_at(const char *path, uint32_t offset, void *buffer, uint32_t size)
{
    return app_installer_read_media(path, offset, buffer, size) == (int) size;
}

static bool setup_zip_name_to_target(const char *name, char *target, uint32_t target_size)
{
    uint32_t out = 0;
    bool slash = true;

    if (name == 0 || target == 0 || target_size < 2 || name[0] == '\0') {
        return false;
    }
    if (strlen(PATH_ROOT) + 1U > target_size) {
        return false;
    }
    strcpy(target, PATH_ROOT);
    out = (uint32_t) strlen(target);
    for (uint32_t i = 0; name[i] != '\0'; i++) {
        char ch = name[i];

        if (ch == ':') {
            return false;
        }
        if (ch == '/' || ch == '\\') {
            if (slash) {
                continue;
            }
            slash = true;
        } else {
            slash = false;
        }
        if (out + 1U >= target_size) {
            return false;
        }
        if (ch >= 'a' && ch <= 'z') {
            ch = (char) (ch - ('a' - 'A'));
        }
        target[out++] = ch == '/' ? PATH_SEPARATOR : ch;
        target[out] = '\0';
    }
    if (out <= strlen(PATH_ROOT)) {
        return false;
    }
    for (uint32_t i = 0; target[i] != '\0'; i++) {
        if (target[i] == '.' &&
            (i == 0 || target[i - 1U] == PATH_SEPARATOR) &&
            target[i + 1U] == '.' &&
            (target[i + 2U] == PATH_SEPARATOR || target[i + 2U] == '\0')) {
            return false;
        }
    }
    return true;
}

static bool setup_zip_scan(const char *zip_path,
                           uint32_t *total_uncompressed,
                           const setup_install_plan_t *plan)
{
    uint32_t offset = 0;
    uint32_t package_bytes;
    int32_t package_size;
    uint32_t copied = 0;
    uint32_t last_percent = 12U;
    uint64_t start_ticks = app_ticks();

    if (zip_path == 0 || total_uncompressed == 0) {
        return false;
    }
    if (plan != 0 && plan->image_size > 0x7FFFFFFFU) {
        return false;
    }
    package_size = plan != 0 ? (int32_t) plan->image_size : setup_media_file_size(zip_path);
    if (package_size <= 0) {
        return false;
    }
    package_bytes = (uint32_t) package_size;
    if (plan == 0) {
        *total_uncompressed = 0;
    }
    while (offset <= package_bytes && package_bytes - offset >= 30U) {
        uint8_t header[30];
        uint32_t sig;
        uint16_t flags;
        uint16_t method;
        uint32_t compressed_size;
        uint32_t uncompressed_size;
        uint16_t name_len;
        uint16_t extra_len;
        uint32_t data_offset;
        char name[128];
        char target_path[128];

        if (!setup_read_package_at(zip_path, offset, header, sizeof(header))) {
            return false;
        }
        sig = setup_load_le32(header);
        if (sig == SETUP_ZIP_CENTRAL_SIG || sig == SETUP_ZIP_END_SIG) {
            return true;
        }
        if (sig != SETUP_ZIP_LOCAL_SIG) {
            return false;
        }
        flags = setup_load_le16(header + 6);
        method = setup_load_le16(header + 8);
        compressed_size = setup_load_le32(header + 18);
        uncompressed_size = setup_load_le32(header + 22);
        name_len = setup_load_le16(header + 26);
        extra_len = setup_load_le16(header + 28);
        if ((flags & 0x0008U) != 0 ||
            method != SETUP_ZIP_METHOD_STORE ||
            compressed_size != uncompressed_size ||
            name_len == 0 ||
            name_len >= sizeof(name)) {
            return false;
        }
        if (name_len > package_bytes - offset - 30U ||
            extra_len > package_bytes - offset - 30U - name_len ||
            !setup_read_package_at(zip_path, offset + 30U, name, name_len)) {
            return false;
        }
        name[name_len] = '\0';
        data_offset = offset + 30U + name_len + extra_len;
        if (compressed_size > package_bytes - data_offset) {
            return false;
        }
        if (name[name_len - 1U] != '/') {
            if (!setup_zip_name_to_target(name, target_path, sizeof(target_path))) {
                return false;
            }
            if (plan == 0) {
                if (uncompressed_size > 0xFFFFFFFFU - *total_uncompressed) {
                    return false;
                }
                *total_uncompressed += uncompressed_size;
            } else {
                if (uncompressed_size > APP_INSTALLER_COPY_TARGET_MAX) {
                    setup_line("package file is too large");
                    return false;
                }
                int written = app_installer_copy_target_file(zip_path,
                                                             data_offset,
                                                             uncompressed_size,
                                                             target_path,
                                                             plan->target.start_lba);
                uint32_t percent;
                uint32_t kb_per_sec = 0;
                uint64_t ticks;

                if (written != (int) uncompressed_size) {
                    setup_line("file copy failed");
                    setup_line(target_path);
                    fputs("copy result: ");
                    print_int(written);
                    fputs("\r\n");
                    return false;
                }
                if (copied > *total_uncompressed ||
                    uncompressed_size > *total_uncompressed - copied) {
                    return false;
                }
                copied += uncompressed_size;
                percent = 12U + (uint32_t) (((uint64_t) copied * 76ULL) /
                                             (uint64_t) (*total_uncompressed == 0 ? 1U : *total_uncompressed));
                if (percent > 88U) {
                    percent = 88U;
                }
                ticks = app_ticks() - start_ticks;
                if (ticks > 0) {
                    kb_per_sec = (uint32_t) ((((uint64_t) copied / 1024ULL) * SETUP_TICKS_PER_SEC) / ticks);
                }
                if (percent != last_percent || copied == *total_uncompressed) {
                    setup_print_copy_progress(percent,
                                              "Copying package files",
                                              copied,
                                              *total_uncompressed,
                                              kb_per_sec);
                    last_percent = percent;
                }
            }
        }
        offset = data_offset + compressed_size;
    }
    return true;
}

static setup_target_t setup_select_target(void)
{
    bool have_uefi = setup_media_file_exists(SETUP_IMAGE_UEFI);
    bool have_mbr = setup_media_file_exists(SETUP_IMAGE_MBR);
    setup_target_mode_t mode = SETUP_TARGET_UNKNOWN;
    setup_target_t target;
    const char *boot_mode = setup_env_get("MONIOS_BOOT_MODE");

    setup_log_add("Detecting boot mode");
    if (boot_mode != 0) {
        mode = setup_mode_from_text(boot_mode);
    }
    if (mode == SETUP_TARGET_UNKNOWN && app_file_exists(SETUP_BOOT_CONFIG)) {
        char config[128];
        int read_bytes = app_file_read(SETUP_BOOT_CONFIG, config, sizeof(config) - 1U);

        if (read_bytes > 0) {
            config[read_bytes] = '\0';
            mode = setup_mode_from_text(config);
        }
    }
    if (mode == SETUP_TARGET_UEFI && !have_uefi) {
        mode = have_mbr ? SETUP_TARGET_MBR : SETUP_TARGET_UNKNOWN;
    }
    if (mode == SETUP_TARGET_MBR && !have_mbr) {
        mode = have_uefi ? SETUP_TARGET_UEFI : SETUP_TARGET_UNKNOWN;
    }
    if (mode == SETUP_TARGET_UNKNOWN) {
        mode = have_uefi ? SETUP_TARGET_UEFI :
               (have_mbr ? SETUP_TARGET_MBR : SETUP_TARGET_UNKNOWN);
    }

    target = setup_target_for_mode(mode);
    if (target.mode == SETUP_TARGET_UEFI) {
        setup_log_add("Boot mode: UEFI");
        setup_log_add("Package: SYSTEM_UEFI.ZIP");
    } else if (target.mode == SETUP_TARGET_MBR) {
        setup_log_add("Boot mode: BIOS/MBR");
        setup_log_add("Package: SYSTEM_MBR.ZIP");
    }
    return target;
}

static bool setup_choose_disk_target(const setup_target_t *media,
                                     uint32_t image_size,
                                     app_installer_target_info_t *chosen)
{
    app_installer_target_list_t list;
    setup_input_ui_t ui;
    const char *message = 0;
    int count;
    uint32_t required_sectors = SETUP_FAT32_TOTAL_SECTORS;

    if (chosen == 0 || media == 0 || image_size == 0) {
        return false;
    }
    memset(&list, 0, sizeof(list));
    count = app_installer_list_targets(&list);
    if (count <= 0 || !list.disk_present) {
        setup_line("No installable disk target found.");
        return false;
    }
    g_setup_disk_model[0] = '\0';
    setup_append_text(g_setup_disk_model, sizeof(g_setup_disk_model), list.disk_model);
    setup_append_text(g_setup_disk_model, sizeof(g_setup_disk_model), "  ");
    setup_append_u32(g_setup_disk_model, sizeof(g_setup_disk_model), setup_sector_mb(list.disk_sector_count));
    setup_append_text(g_setup_disk_model, sizeof(g_setup_disk_model), " MB disk  ");
    setup_append_u32(g_setup_disk_model, sizeof(g_setup_disk_model), setup_bytes_mb(image_size));
    setup_append_text(g_setup_disk_model, sizeof(g_setup_disk_model), " MB package");
    g_setup_disk_mb = setup_sector_mb(list.disk_sector_count);
    g_setup_image_mb = setup_bytes_mb(image_size);
    g_setup_disk_target_count = 0;
    for (uint32_t i = 0; i < list.target_count && g_setup_disk_target_count < APP_INSTALLER_MAX_TARGETS; i++) {
        bool selectable;

        if (list.targets[i].sector_count == 0) {
            continue;
        }
        uint32_t target_required = list.targets[i].kind == APP_INSTALLER_TARGET_KIND_DISK ?
                                   (SETUP_DEFAULT_PART_LBA + required_sectors) :
                                   required_sectors;
        selectable = setup_target_supported(&list.targets[i]) &&
                     list.targets[i].sector_count >= target_required;
        g_setup_disk_targets[g_setup_disk_target_count].info = list.targets[i];
        g_setup_disk_targets[g_setup_disk_target_count].selectable = selectable;
        setup_make_target_label(&list.targets[i],
                                g_setup_disk_targets[g_setup_disk_target_count].label,
                                sizeof(g_setup_disk_targets[g_setup_disk_target_count].label));
        setup_make_target_details(&list.targets[i],
                                  g_setup_disk_targets[g_setup_disk_target_count].details,
                                  sizeof(g_setup_disk_targets[g_setup_disk_target_count].details));
        if (list.targets[i].sector_count < target_required) {
            setup_append_text(g_setup_disk_targets[g_setup_disk_target_count].details,
                              sizeof(g_setup_disk_targets[g_setup_disk_target_count].details),
                              " | too small");
        }
        g_setup_disk_target_count++;
    }
    if (g_setup_disk_target_count == 0) {
        setup_line("No usable disk target found.");
        return false;
    }

    while (1) {
        char answer[8];
        uint32_t selected = 0;

        ui.screen = SETUP_INPUT_TARGET_SELECT;
        ui.first_password = 0;
        ui.message = message;
        setup_line("");
        setup_line("Select install target:");
        for (uint32_t i = 0; i < g_setup_disk_target_count; i++) {
            fputs("  ");
            print_uint(i + 1U);
            fputs(". ");
            setup_line(g_setup_disk_targets[i].label);
        }
        fputs("Target number: ");
        setup_read_line(answer, sizeof(answer), false, &ui);
        if (setup_parse_index(answer, g_setup_disk_target_count, &selected) &&
            selected > 0 && selected <= g_setup_disk_target_count) {
            if (!g_setup_disk_targets[selected - 1U].selectable) {
                message = "Selected target cannot be used.";
                setup_draw_target_screen("", message);
                app_sleep_ticks(80);
                continue;
            }
            *chosen = g_setup_disk_targets[selected - 1U].info;
            setup_log_add("Install target selected");
            setup_log_add(g_setup_disk_targets[selected - 1U].label);
            return true;
        }
        message = "Invalid target number.";
        setup_draw_target_screen("", message);
        app_sleep_ticks(80);
    }
}

static bool setup_confirm_install(const setup_install_plan_t *plan)
{
    char answer[8];
    setup_input_ui_t ui;
    static char target_label[96];
    char line[SETUP_PLAN_TEXT_MAX];

    setup_line("");
    if (plan == 0) {
        return false;
    }
    setup_make_target_label(&plan->target, target_label, sizeof(target_label));
    g_setup_confirm_target_label = target_label;
    setup_line("Install MoniOS to the selected target? This overwrites data.");
    setup_line(target_label);
    line[0] = '\0';
    setup_append_text(line, sizeof(line), "Package: ");
    setup_append_text(line, sizeof(line), plan->media.label);
    setup_append_text(line, sizeof(line), "  ");
    setup_append_u32(line, sizeof(line), g_setup_image_mb);
    setup_append_text(line, sizeof(line), " MB");
    setup_line(line);
    line[0] = '\0';
    setup_append_text(line, sizeof(line), "Target usable: ");
    setup_append_u32(line, sizeof(line), setup_sector_mb(plan->target.sector_count));
    setup_append_text(line, sizeof(line), " MB");
    if (plan->free_mb > 0) {
        setup_append_text(line, sizeof(line), "  remaining after package: ");
        setup_append_u32(line, sizeof(line), plan->free_mb);
        setup_append_text(line, sizeof(line), " MB");
    }
    setup_line(line);
    if (plan->create_partition) {
        setup_line("Partition table will be recreated with one FAT32 partition.");
    }
    fputs("Type YES to continue: ");
    ui.screen = SETUP_INPUT_CONFIRM_INSTALL;
    ui.first_password = 0;
    ui.message = 0;
    setup_read_line(answer, sizeof(answer), false, &ui);
    return setup_text_equals_ignore_case(answer, "yes");
}

static bool setup_get_password(char *password, uint32_t password_size)
{
    char confirm[SETUP_PASSWORD_MAX];
    setup_input_ui_t ui;
    const char *message = 0;

    while (1) {
        fputs("Set root password: ");
        ui.screen = SETUP_INPUT_PASSWORD;
        ui.first_password = 0;
        ui.message = message;
        setup_read_line(password, password_size, true, &ui);
        fputs("Confirm password: ");
        ui.screen = SETUP_INPUT_CONFIRM_PASSWORD;
        ui.first_password = password;
        ui.message = 0;
        setup_read_line(confirm, sizeof(confirm), true, &ui);
        if (password[0] == '\0') {
            setup_line("Password cannot be empty.");
            message = "Password cannot be empty.";
            setup_draw_password_screen(0, "", "", message);
            app_sleep_ticks(80);
            continue;
        }
        if (strcmp(password, confirm) != 0) {
            setup_line("Passwords do not match.");
            message = "Passwords do not match.";
            setup_draw_password_screen(0, "", "", message);
            app_sleep_ticks(80);
            continue;
        }
        return true;
    }
}

static bool setup_install_package(const setup_install_plan_t *plan)
{
    uint8_t boot_sector[512];
    uint32_t total_uncompressed = 0;

    if (plan == 0 || plan->media.image_path == 0) {
        return false;
    }
    if (plan->target.sector_count < SETUP_FAT32_TOTAL_SECTORS) {
        setup_line("selected target is too small");
        return false;
    }
    if (!setup_prepare_fat32_boot_sector(boot_sector,
                                         plan->target.start_lba,
                                         SETUP_FAT32_TOTAL_SECTORS)) {
        setup_line("boot sector prepare failed");
        return false;
    }
    if (!setup_write_partition_table(plan, boot_sector)) {
        setup_line("partition table write failed");
        return false;
    }
    if (!setup_format_fat32_partition(plan, boot_sector)) {
        setup_line("FAT32 format failed");
        return false;
    }
    setup_print_progress(10, "Scanning package");
    if (!setup_zip_scan(plan->media.image_path, &total_uncompressed, 0) || total_uncompressed == 0) {
        setup_line("package scan failed");
        return false;
    }
    if (!setup_zip_scan(plan->media.image_path, &total_uncompressed, plan)) {
        setup_line("package copy failed");
        return false;
    }
    return true;
}

static bool setup_build_install_plan(const setup_target_t *media,
                                     const app_installer_target_info_t *disk_target,
                                     setup_install_plan_t *plan)
{
    uint32_t total;

    if (media == 0 || disk_target == 0 || plan == 0 || media->image_path == 0) {
        return false;
    }
    memset(plan, 0, sizeof(*plan));
    plan->media = *media;
    plan->target = *disk_target;
    {
        int32_t size = setup_media_file_size(media->image_path);

        if (size <= 0) {
            return false;
        }
        plan->image_size = (uint32_t) size;
    }
    if (plan->image_size == 0) {
        return false;
    }
    total = SETUP_FAT32_TOTAL_SECTORS;
    plan->copy_bytes = total;
    plan->create_partition = disk_target->kind == APP_INSTALLER_TARGET_KIND_UEFI_ESP ||
                             disk_target->kind == APP_INSTALLER_TARGET_KIND_DISK;
    if (plan->create_partition) {
        if (disk_target->sector_count < SETUP_DEFAULT_PART_LBA + SETUP_FAT32_TOTAL_SECTORS) {
            return false;
        }
        plan->target.start_lba = SETUP_DEFAULT_PART_LBA;
        plan->target.sector_count = SETUP_FAT32_TOTAL_SECTORS;
        plan->target.partition_type = plan->media.mode == SETUP_TARGET_MBR ? 0x0C : 0xEF;
        plan->target.active = 1;
    } else if (plan->target.sector_count < SETUP_FAT32_TOTAL_SECTORS) {
        return false;
    }
    if (plan->create_partition) {
        if (disk_target->sector_count > SETUP_DEFAULT_PART_LBA + total) {
            plan->free_mb = setup_sector_mb(disk_target->sector_count - SETUP_DEFAULT_PART_LBA - total);
        }
    } else if (disk_target->sector_count > total) {
        plan->free_mb = setup_sector_mb(disk_target->sector_count - total);
    }
    setup_make_target_label(disk_target, plan->target_label, sizeof(plan->target_label));
    return true;
}

static void setup_append_report_line(char *report, uint32_t report_size, const char *label, const char *value)
{
    setup_append_text(report, report_size, label);
    setup_append_text(report, report_size, value);
    setup_append_text(report, report_size, "\r\n");
}

static void setup_append_report_u32(char *report, uint32_t report_size, const char *label, uint32_t value)
{
    setup_append_text(report, report_size, label);
    setup_append_u32(report, report_size, value);
    setup_append_text(report, report_size, "\r\n");
}

static void setup_build_install_report(const setup_install_plan_t *plan,
                                       char *report,
                                       uint32_t report_size)
{
    if (report == 0 || report_size == 0) {
        return;
    }
    report[0] = '\0';
    setup_append_report_line(report, report_size, "MoniOS installation report", "");
    setup_append_report_line(report, report_size, "Package mode: ", plan != 0 ? plan->media.label : "unknown");
    setup_append_report_line(report, report_size, "Package path: ", plan != 0 ? plan->media.image_path : "unknown");
    setup_append_report_line(report, report_size, "Target: ", plan != 0 ? plan->target_label : "unknown");
    setup_append_report_u32(report, report_size, "Disk MB: ", g_setup_disk_mb);
    setup_append_report_u32(report, report_size, "Package MB: ", g_setup_image_mb);
    if (plan != 0) {
        setup_append_report_u32(report, report_size, "Start LBA: ", plan->target.start_lba);
        setup_append_report_u32(report, report_size, "Target sectors: ", plan->target.sector_count);
        setup_append_report_line(report,
                                 report_size,
                                 "Partition mode: ",
                                 plan->create_partition ? "created FAT32 partition" : "existing target");
    }
    setup_append_report_line(report, report_size, "", "");
    setup_append_report_line(report, report_size, "Recent setup log", "");
    for (uint32_t i = 0; i < g_setup_log_count; i++) {
        setup_append_report_line(report, report_size, "- ", g_setup_logs[i]);
    }
}

int main(int argc, char **argv)
{
    const app_launch_info_t *info = app_launch_info();
    setup_target_t target;
    app_installer_target_info_t disk_target;
    setup_install_plan_t plan;
    char password[SETUP_PASSWORD_MAX];
    char auth_file[96];
    char install_report[SETUP_REPORT_MAX];
    int32_t image_size;

    (void) argc;
    (void) argv;

    app_enter_graphics_mode();
    app_sleep_ticks(20);
    setup_log_clear();
    setup_draw_base("Starting MoniOS Setup", "Checking installer media.");
    app_graphics_present();

    setup_line("MoniOS Setup");
    setup_line("------------");

    if (!app_installer_boot_media()) {
        setup_line("This is not installer media.");
        setup_draw_error("This is not installer media.");
        return 1;
    }
    if (info == 0 || info->privilege_level > APP_PRIV_R2) {
        setup_line("Setup must run from installer boot.");
        setup_draw_error("Setup must run from installer boot.");
        return 1;
    }
    target = setup_select_target();
    if (target.image_path == 0) {
        setup_line("No system package found.");
        setup_draw_error("No system package found.");
        return 1;
    }
    setup_line("Installer media: ready");
    setup_line(target.label);
    setup_line(target.image_path);
    image_size = setup_media_file_size(target.image_path);
    if (image_size <= 0) {
        setup_line("Selected package is missing or empty.");
        setup_draw_error("Selected package is missing or empty.");
        return 1;
    }

    if (!setup_choose_disk_target(&target, (uint32_t) image_size, &disk_target)) {
        setup_draw_error("No install target selected.");
        return 1;
    }
    if (!setup_build_install_plan(&target, &disk_target, &plan)) {
        setup_line("Install plan creation failed.");
        setup_draw_error("Install plan creation failed.");
        return 1;
    }
    if (!setup_get_password(password, sizeof(password))) {
        return 1;
    }
    if (!setup_confirm_install(&plan)) {
        setup_line("Installation canceled.");
        setup_draw_error("Installation canceled.");
        return 1;
    }
    if (!setup_make_password_file(password, auth_file, sizeof(auth_file))) {
        setup_line("Password file creation failed.");
        setup_draw_error("Password file creation failed.");
        return 1;
    }
    setup_print_progress(0, "MoniOS Setup");
    if (!setup_install_package(&plan)) {
        setup_print_progress(100, "Installation failed");
        app_sleep_ticks(120);
        return 1;
    }
    setup_print_progress(90, "Writing password");
    if (app_installer_write_target_file(SETUP_AUTH_PATH,
                                        auth_file,
                                        plan.target.start_lba,
                                        (uint32_t) strlen(auth_file)) <= 0) {
        setup_line("Failed to write target password.");
        setup_draw_error("Failed to write target password.");
        return 1;
    }
    setup_print_progress(96, "Writing install log");
    setup_build_install_report(&plan, install_report, sizeof(install_report));
    if (app_installer_write_target_file(SETUP_LOG_PATH,
                                        install_report,
                                        plan.target.start_lba,
                                        (uint32_t) strlen(install_report)) <= 0) {
        setup_line("Install log write skipped.");
        setup_log_add("Install log write skipped");
    }

    setup_print_progress(100, "Installation complete");
    setup_line("Installation complete. Remove ISO if needed.");
    setup_line("Rebooting...");
    app_sleep_ticks(200);
    app_installer_reboot();
    return 0;
}
