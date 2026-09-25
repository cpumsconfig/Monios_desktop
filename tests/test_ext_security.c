int printf(const char *, ...);
#define inb test_ata_inb
#define outb test_ata_outb
#define outw test_ata_outw
#include "../fs/extfs.c"
#undef inb
#undef outb
#undef outw
static uint8_t test_status = 0x08;
static uint8_t command;
static uint32_t words, flushes;
static bool flush_error, flush_timeout, stuck_drq;
uint8_t test_ata_inb(uint16_t port)
{
    (void)port;
    if (test_status != 0x08) return test_status;
    if (command == ATA_CMD_FLUSH_CACHE) {
        if (flush_error) return 0x41;
        if (flush_timeout) return ATA_STATUS_BSY;
    }
    if (command == ATA_CMD_WRITE_SECTORS && (words < 256 || stuck_drq)) return 0x48;
    return 0x40;
}
void test_ata_outb(uint16_t port, uint8_t value)
{
    if (port == ATA_COMMAND_PORT) {
        command = value;
        if (value == ATA_CMD_WRITE_SECTORS) words = 0;
        if (value == ATA_CMD_FLUSH_CACHE) flushes++;
    }
}
void test_ata_outw(uint16_t port, uint16_t value)
{ (void)value; if (port == ATA_DATA_PORT) words++; }
static void put32(uint8_t *p, uint32_t v)
{ for (unsigned i = 0; i < 4; i++) p[i] = (uint8_t)(v >> (8 * i)); }
int main(void)
{
    uint8_t super[1024];
    memset(super, 0, sizeof(super));
    super[56] = 0x53; super[57] = 0xEF;
    put32(super + 32, 8192); put32(super + 40, 1024);
    super[89] = 1; /* 256-byte inode */
    if (!extfs_parse_super(0, super)) return 1;
    super[88] = 1; /* 257-byte, unaligned inode */
    if (extfs_parse_super(0, super)) return 2;
    super[88] = 0; put32(super + 40, 0);
    if (extfs_parse_super(0, super)) return 3;
    put32(super + 40, 0xFFFFFFFFu);
    if (extfs_parse_super(0, super)) return 4;
    put32(super + 40, 1024); put32(super + 24, 32);
    if (extfs_parse_super(0, super)) return 5;
    put32(super + 24, 3);
    if (extfs_parse_super(0, super)) return 6; /* 8K unsupported by 4K cache. */
    put32(super + 24, 2);
    if (!extfs_parse_super(0, super)) return 7;
    if (sizeof(g_extfs_inode_buf) < g_extfs_info.block_size) return 8;
    g_cache_block[0] = 10;
    g_cache_dirty[0] = true;
    memset(g_cache_data[0], 0xA5, sizeof(g_cache_data[0]));
    test_status = 0x09; /* ERR together with DRQ must not report success. */
    if (extfs_sync() || !g_cache_dirty[0] || g_cache_data[0][0] != 0xA5) return 9;
    test_status = 0x28; /* device fault */
    if (extfs_sync() || !g_cache_dirty[0]) return 10;
    test_status = 0x08;
    if (!extfs_sync() || g_cache_dirty[0]) return 11;
    g_cache_block[0] = 0xFFFFFFFFu - 1;
    g_cache_dirty[0] = true;
    if (extfs_sync() || !g_cache_dirty[0]) return 12; /* LBA overflow */
    extfs_cache_init();
    for (int i = 0; i < EXTFS_CACHE_SIZE; i++) {
        g_cache_block[i] = (uint32_t)i;
        g_cache_lru[i] = (uint32_t)i;
        g_cache_dirty[i] = true;
    }
    test_status = 0x09;
    extfs_write_block_cached(100, g_extfs_block);
    if (g_cache_block[0] != 0 || !g_cache_dirty[0] || !g_write_rejected) return 13;
    test_status = 0x08;
    if (extfs_sync()) return 14; /* Rejected mutation cannot be certified clean. */
    extfs_cache_init();
    g_cache_block[0] = 10; g_cache_dirty[0] = true;
    flushes = 0; flush_error = true;
    if (extfs_sync() || !g_cache_dirty[0] || flushes != 1) return 15;
    flush_error = false; flush_timeout = true; command = 0;
    if (extfs_sync() || !g_cache_dirty[0]) return 16;
    flush_timeout = false; command = 0;
    if (!extfs_sync() || g_cache_dirty[0]) return 17;
    uint32_t prior_flushes = flushes;
    if (!extfs_sync() || flushes != prior_flushes) return 18;
    g_cache_dirty[0] = true; stuck_drq = true;
    if (extfs_sync() || !g_cache_dirty[0] || flushes != prior_flushes) return 19;
    stuck_drq = false; command = 0;
    if (!extfs_sync() || g_cache_dirty[0]) return 20;
    printf("EXT malformed superblock geometry: PASS\n");
    return 0;
}
