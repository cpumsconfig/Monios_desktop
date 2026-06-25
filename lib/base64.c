#include "base64.h"
#include "common.h"

static const char g_base64_table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static int32_t base64_decode_char(char ch)
{
    if (ch >= 'A' && ch <= 'Z') {
        return ch - 'A';
    }
    if (ch >= 'a' && ch <= 'z') {
        return ch - 'a' + 26;
    }
    if (ch >= '0' && ch <= '9') {
        return ch - '0' + 52;
    }
    if (ch == '+') {
        return 62;
    }
    if (ch == '/') {
        return 63;
    }
    if (ch == '=') {
        return -2;
    }
    if (ch == ' ' || ch == '\r' || ch == '\n' || ch == '\t') {
        return -3;
    }
    return -1;
}

int32_t base64_encode(const uint8_t *input, uint32_t input_size, char *output, uint32_t output_size)
{
    uint32_t out_len = ((input_size + 2) / 3) * 4;
    uint32_t i;
    uint32_t out_index = 0;

    if (output_size <= out_len) {
        return -1;
    }

    for (i = 0; i + 2 < input_size; i += 3) {
        uint32_t value = ((uint32_t) input[i] << 16) |
                         ((uint32_t) input[i + 1] << 8) |
                         (uint32_t) input[i + 2];
        output[out_index++] = g_base64_table[(value >> 18) & 0x3F];
        output[out_index++] = g_base64_table[(value >> 12) & 0x3F];
        output[out_index++] = g_base64_table[(value >> 6) & 0x3F];
        output[out_index++] = g_base64_table[value & 0x3F];
    }

    if (i < input_size) {
        uint32_t value = (uint32_t) input[i] << 16;
        output[out_index++] = g_base64_table[(value >> 18) & 0x3F];
        if (i + 1 < input_size) {
            value |= (uint32_t) input[i + 1] << 8;
            output[out_index++] = g_base64_table[(value >> 12) & 0x3F];
            output[out_index++] = g_base64_table[(value >> 6) & 0x3F];
            output[out_index++] = '=';
        } else {
            output[out_index++] = g_base64_table[(value >> 12) & 0x3F];
            output[out_index++] = '=';
            output[out_index++] = '=';
        }
    }

    output[out_index] = '\0';
    return (int32_t) out_index;
}

int32_t base64_decode(const char *input, uint8_t *output, uint32_t output_size)
{
    int32_t values[4];
    uint32_t value_count = 0;
    uint32_t out_index = 0;

    while (*input != '\0') {
        int32_t value = base64_decode_char(*input++);

        if (value == -3) {
            continue;
        }
        if (value < -2) {
            return -1;
        }

        values[value_count++] = value;
        if (value_count == 4) {
            uint32_t triple;
            uint32_t bytes_to_write = 3;

            if (values[0] < 0 || values[1] < 0) {
                return -1;
            }

            if (values[2] == -2) {
                values[2] = 0;
                values[3] = 0;
                bytes_to_write = 1;
            } else if (values[3] == -2) {
                values[3] = 0;
                bytes_to_write = 2;
            } else if (values[2] < 0 || values[3] < 0) {
                return -1;
            }

            if (out_index + bytes_to_write > output_size) {
                return -1;
            }

            triple = ((uint32_t) values[0] << 18) |
                     ((uint32_t) values[1] << 12) |
                     ((uint32_t) values[2] << 6) |
                     (uint32_t) values[3];

            output[out_index++] = (uint8_t) ((triple >> 16) & 0xFF);
            if (bytes_to_write >= 2) {
                output[out_index++] = (uint8_t) ((triple >> 8) & 0xFF);
            }
            if (bytes_to_write == 3) {
                output[out_index++] = (uint8_t) (triple & 0xFF);
            }

            value_count = 0;
        }
    }

    if (value_count != 0) {
        return -1;
    }

    return (int32_t) out_index;
}
