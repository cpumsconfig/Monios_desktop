/* gcc -std=gnu17 -fno-builtin -I include tests/test_zip_security.c lib/string.c -o out/test_zip_security.exe */
int printf(const char *, ...);
#include "../lib/crc32.c"
#include "../lib/zip.c"

static unsigned failures, checks;
#define CHECK(x) do { checks++; if (!(x)) { failures++; printf("FAIL line %d: %s\n", __LINE__, #x); } } while (0)
static zip_writer_t writer;
static zip_reader_t reader;
static uint8_t archive[1024], mutated[1024], output[32];

int main(void)
{
    int size;
    uint32_t cd, end;
    zip_writer_init(&writer, archive, sizeof(archive));
    CHECK(zip_writer_add_file(&writer, "hello.txt", "hello", 5) == 0);
    size = zip_writer_finish(&writer);
    CHECK(size > 0);
    CHECK(zip_reader_open(&reader, archive, (uint32_t)size) == 0);
    CHECK(zip_reader_extract(&reader, 0, output, sizeof(output)) == 5);
    CHECK(memcmp(output, "hello", 5) == 0);
    CHECK(zip_reader_extract(&reader, 0, output, 4) == -1);
    CHECK(zip_reader_open(NULL, archive, (uint32_t)size) == -1);
    CHECK(zip_reader_extract(NULL, 0, output, sizeof(output)) == -1);
    end = (uint32_t)size - 22;
    cd = rd32(archive + end + 16);

    /* Every truncation must be rejected, including a partial EOCD. */
    for (uint32_t n = 0; n < (uint32_t)size; n++)
        CHECK(zip_reader_open(&reader, archive, n) == -1);

    memcpy(mutated, archive, (uint32_t)size);
    wr32(mutated + end + 16, 0xFFFFFFF0u);
    CHECK(zip_reader_open(&reader, mutated, (uint32_t)size) == -1);
    memcpy(mutated, archive, (uint32_t)size);
    wr32(mutated + end + 12, 1); /* Directory header outside declared span. */
    CHECK(zip_reader_open(&reader, mutated, (uint32_t)size) == -1);
    memcpy(mutated, archive, (uint32_t)size);
    wr16(mutated + cd + 28, 0xFFFF);
    CHECK(zip_reader_open(&reader, mutated, (uint32_t)size) == -1);
    memcpy(mutated, archive, (uint32_t)size);
    mutated[cd + 46] = 0;
    CHECK(zip_reader_open(&reader, mutated, (uint32_t)size) == -1);
    memcpy(mutated, archive, (uint32_t)size);
    wr16(mutated + cd + 8, 1); /* Unsupported encryption. */
    CHECK(zip_reader_open(&reader, mutated, (uint32_t)size) == -1);
    memcpy(mutated, archive, (uint32_t)size);
    wr16(mutated + end + 4, 1); /* Multi-disk archive. */
    CHECK(zip_reader_open(&reader, mutated, (uint32_t)size) == -1);
    memcpy(mutated, archive, (uint32_t)size);
    wr16(mutated + end + 20, 1); /* Missing comment byte. */
    CHECK(zip_reader_open(&reader, mutated, (uint32_t)size) == -1);

    /* A valid but unrepresentable name must never alias a shorter path. */
    memcpy(mutated, archive, cd + 46);
    memset(mutated + cd + 46, 'a', ZIP_NAME_MAX);
    memcpy(mutated + cd + 46 + ZIP_NAME_MAX, archive + end, 22);
    wr16(mutated + cd + 28, ZIP_NAME_MAX);
    wr32(mutated + cd + 46 + ZIP_NAME_MAX + 12, 46 + ZIP_NAME_MAX);
    CHECK(zip_reader_open(&reader, mutated, cd + 46 + ZIP_NAME_MAX + 22) == -1);

    zip_writer_init(&writer, mutated, 32);
    memset(mutated, 0xA5, sizeof(mutated));
    CHECK(zip_writer_add_file(&writer, "a", "hello", 5) == -1);
    CHECK(writer.offset == 0 && writer.count == 0 && mutated[0] == 0xA5);
    CHECK(zip_writer_add_file(&writer, "a", NULL, 1) == -1);
    CHECK(zip_writer_add_file(&writer, "a", "", 0xFFFFFFFFu) == -1);
    CHECK(zip_writer_add_file(&writer, "a", "x", 1) == 0);
    CHECK(writer.count == 1 && writer.offset == 32);
    printf("ZIP security: %u checks, %u failures\n", checks, failures);
    return failures != 0;
}
