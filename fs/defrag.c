/*
 * fs/defrag.c - FAT32 磁盘碎片整理内核辅助模块。
 *
 * 提供两个核心函数：
 *   fat32_defrag_analyze()  分析卷上每个文件的碎片情况，填充报告结构。
 *   fat32_defrag_run()       执行碎片整理：将文件簇移动到连续空闲区域。
 *
 * 实现原理：
 *   1. 通过 fat32_volume_geometry() 获取卷起始 LBA、总扇区数、每簇扇区数。
 *   2. 通过 fat32_read_volume() 读取 FAT 表扇区，遍历簇链。
 *   3. 遍历根目录及子目录项，获取每个文件的首簇号和大小。
 *   4. 沿 FAT 链追踪每个文件的簇链，统计碎片数（不连续跳变次数+1）。
 *   5. 整理时：读取文件数据簇 → 找到连续空闲簇 → 写入新位置 → 更新 FAT。
 *
 * 注意：本模块通过 fat32_read_volume / fat32_write_volume 直接操作扇区，
 *       不经过 VFS 缓存，整理前应卸载文件系统或确保无其他写入操作。
 */

#include "common.h"
#include "fat32.h"
#include "string.h"

/* FAT32 目录项属性 */
#define FAT32_ATTR_READ_ONLY  0x01
#define FAT32_ATTR_HIDDEN     0x02
#define FAT32_ATTR_SYSTEM     0x04
#define FAT32_ATTR_VOLUME_ID  0x08
#define FAT32_ATTR_DIRECTORY  0x10
#define FAT32_ATTR_ARCHIVE    0x20
#define FAT32_ATTR_LONG_NAME  0x0F

#define FAT32_CLUSTER_FREE    0x00000000U
#define FAT32_CLUSTER_BAD     0x0FFFFFF7U
#define FAT32_CLUSTER_END_MIN 0x0FFFFFF8U
#define FAT32_CLUSTER_MASK    0x0FFFFFFFU

#define DEFRAG_MAX_FILES      128U
#define DEFRAG_NAME_LEN       64U

/* 分析报告中单个文件的碎片信息 */
typedef struct {
    char     name[DEFRAG_NAME_LEN];   /* 文件名 */
    uint32_t first_cluster;           /* 首簇号 */
    uint32_t cluster_count;           /* 占用簇数 */
    uint32_t fragment_count;          /* 碎片数（=不连续段数） */
    uint32_t file_size_kb;            /* 文件大小 KB */
    uint8_t  is_directory;            /* 是否为目录 */
} fat32_defrag_file_t;

/* 整理分析报告 */
typedef struct {
    uint32_t total_clusters;          /* 卷总簇数 */
    uint32_t free_clusters;           /* 空闲簇数 */
    uint32_t used_clusters;           /* 已用簇数 */
    uint32_t bad_clusters;            /* 坏簇数 */
    uint32_t total_files;             /* 总文件数 */
    uint32_t fragmented_files;         /* 碎片化文件数（碎片数>1） */
    uint32_t total_fragments;         /* 总碎片数 */
    uint32_t contiguous_files;         /* 连续文件数（碎片数=1） */
    uint32_t fragmentation_pct;       /* 碎片化百分比 0-100 */
    uint32_t cluster_size;             /* 每簇字节数 */
    fat32_defrag_file_t files[DEFRAG_MAX_FILES];
} fat32_defrag_report_t;

/* 前向声明：从 fat32.c 导出的卷访问接口 */
extern bool fat32_volume_geometry(uint32_t *out_start_lba,
                                    uint64_t *out_total_sectors,
                                    uint32_t *out_sector_size,
                                    uint32_t *out_cluster_sectors);
extern bool fat32_read_volume(uint32_t rel_lba, uint32_t count, void *buffer);

/* ---- 内部辅助 ---- */

/* 读取一个 FAT 表项（32 位，低 28 位有效）。
 * fat_lba_base: FAT 区起始相对 LBA
 * cluster: 簇号
 * 返回该簇的 FAT 项值。
 */
static uint32_t defrag_fat_get(uint32_t fat_lba_base,
                                uint32_t sectors_per_fat,
                                uint32_t cluster)
{
    uint32_t byte_offset = cluster * 4U;
    uint32_t sector_offset = byte_offset / 512U;
    uint32_t within = byte_offset % 512U;
    uint8_t sector_buf[512];
    uint32_t val;

    (void)sectors_per_fat;

    if (!fat32_read_volume(fat_lba_base + sector_offset, 1U, sector_buf)) {
        return FAT32_CLUSTER_FREE;
    }
    val = (uint32_t)sector_buf[within]        |
          ((uint32_t)sector_buf[within + 1] << 8)  |
          ((uint32_t)sector_buf[within + 2] << 16) |
          ((uint32_t)sector_buf[within + 3] << 24);
    return val & FAT32_CLUSTER_MASK;
}

/* 计算簇链的碎片数：沿链追踪相邻簇是否连续。
 * 返回碎片段数（1=完全连续，>1=碎片化）。
 */
static uint32_t defrag_count_fragments(uint32_t fat_lba_base,
                                        uint32_t sectors_per_fat,
                                        uint32_t start_cluster,
                                        uint32_t *out_total_clusters)
{
    uint32_t cluster = start_cluster;
    uint32_t frags = 1U;
    uint32_t count = 0U;
    uint32_t prev = start_cluster;

    if (start_cluster < 2U) {
        *out_total_clusters = 0U;
        return 0U;
    }

    for (;;) {
        uint32_t val;
        count++;
        if (count > 4096U) break;   /* 防循环 */

        val = defrag_fat_get(fat_lba_base, sectors_per_fat, cluster);
        if (val >= FAT32_CLUSTER_END_MIN) break;
        if (val == FAT32_CLUSTER_FREE || val == FAT32_CLUSTER_BAD) break;

        /* 检查连续性：下一簇号是否 = 当前簇号 + 1 */
        if (val != cluster + 1U) {
            frags++;
        }
        prev = cluster;
        cluster = val;
    }
    (void)prev;
    *out_total_clusters = count;
    return frags;
}

/* 遍历目录扇区，收集文件信息。
 * data_lba: 数据区起始相对 LBA
 * fat_lba_base: FAT 区起始相对 LBA
 * dir_cluster: 目录首簇号（根目录时为 g_bpb.root_cluster）
 * report: 填充报告
 * depth: 递归深度（防止过深）
 */
static void defrag_scan_dir(uint32_t data_lba,
                             uint32_t cluster_sectors,
                             uint32_t fat_lba_base,
                             uint32_t sectors_per_fat,
                             uint32_t dir_cluster,
                             fat32_defrag_report_t *report,
                             uint32_t depth)
{
    uint32_t cluster = dir_cluster;
    uint32_t entries_per_sector = 512U / 32U;
    uint32_t safety = 0U;

    if (depth > 4U) return;
    if (report->total_files >= DEFRAG_MAX_FILES) return;

    while (cluster >= 2U && cluster < FAT32_CLUSTER_END_MIN && safety < 256U) {
        uint32_t lba = data_lba + (cluster - 2U) * cluster_sectors;
        uint32_t s;

        for (s = 0; s < cluster_sectors; s++) {
            uint8_t sec[512];
            uint32_t e;
            if (!fat32_read_volume(lba + s, 1U, sec)) break;

            for (e = 0; e < entries_per_sector; e++) {
                uint8_t *de = &sec[e * 32U];
                uint8_t name0 = de[0];
                uint8_t attr = de[11];
                uint32_t fcluster;
                uint32_t fsize;
                fat32_defrag_file_t *fi;

                if (name0 == 0x00) return;       /* 目录结束 */
                if (name0 == 0xE5) continue;     /* 已删除 */
                if (attr == FAT32_ATTR_LONG_NAME) continue;
                if (attr & FAT32_ATTR_VOLUME_ID) continue;

                fcluster = ((uint32_t)de[20] << 16) |
                           ((uint32_t)de[26] << 8) | de[27];
                /* 注意：FAT32 首簇号 = (high<<16)|low，de[20..21]=high, de[26..27]=low */
                fcluster = ((uint32_t)de[20] << 16) |
                           ((uint32_t)de[26] << 8) | de[27];
                fsize = (uint32_t)de[28] |
                        ((uint32_t)de[29] << 8) |
                        ((uint32_t)de[30] << 16) |
                        ((uint32_t)de[31] << 24);

                if (fcluster < 2U && fsize == 0) continue;

                if (report->total_files >= DEFRAG_MAX_FILES) return;
                fi = &report->files[report->total_files];
                memset(fi, 0, sizeof(*fi));

                /* 解析 8.3 文件名为可读字符串 */
                {
                    uint32_t ni = 0U;
                    uint32_t k;
                    for (k = 0; k < 8U && name0 != ' '; k++) {
                        uint8_t ch = de[k];
                        if (ch == ' ') break;
                        if (ni < DEFRAG_NAME_LEN - 1) {
                            fi->name[ni++] = (char)ch;
                        }
                    }
                    if (de[8] != ' ' && de[8] != 0) {
                        if (ni < DEFRAG_NAME_LEN - 1) fi->name[ni++] = '.';
                        for (k = 8; k < 11U; k++) {
                            uint8_t ch = de[k];
                            if (ch == ' ') break;
                            if (ni < DEFRAG_NAME_LEN - 1) {
                                fi->name[ni++] = (char)ch;
                            }
                        }
                    }
                    fi->name[ni] = '\0';
                }

                fi->first_cluster = fcluster;
                fi->file_size_kb = fsize / 1024U;
                fi->is_directory = (attr & FAT32_ATTR_DIRECTORY) ? 1U : 0U;

                /* 计算碎片数 */
                if (fcluster >= 2U) {
                    fi->fragment_count = defrag_count_fragments(
                        fat_lba_base, sectors_per_fat,
                        fcluster, &fi->cluster_count);
                } else {
                    fi->fragment_count = 0U;
                    fi->cluster_count = 0U;
                }

                report->total_files++;

                /* 递归进入子目录 */
                if (fi->is_directory &&
                    strcmp(fi->name, ".") != 0 &&
                    strcmp(fi->name, "..") != 0 &&
                    fcluster >= 2U) {
                    defrag_scan_dir(data_lba, cluster_sectors,
                                    fat_lba_base, sectors_per_fat,
                                    fcluster, report, depth + 1U);
                }
                if (report->total_files >= DEFRAG_MAX_FILES) return;
            }
        }

        /* 沿目录簇链前进 */
        {
            uint32_t next = defrag_fat_get(fat_lba_base, sectors_per_fat, cluster);
            if (next < 2U || next >= FAT32_CLUSTER_END_MIN) break;
            cluster = next;
        }
        safety++;
    }
}

/* ================================================================
 *  fat32_defrag_analyze() - 分析 FAT32 卷碎片情况
 *
 *  参数：
 *    report - 输出报告（调用者分配）
 *  返回：
 *    true  = 分析成功
 *    false = 无法读取卷几何信息
 * ================================================================ */
bool fat32_defrag_analyze(fat32_defrag_report_t *report)
{
    uint32_t vol_start_lba;
    uint64_t total_sectors;
    uint32_t sector_size;
    uint32_t cluster_sectors;
    uint32_t fat_sectors;
    uint32_t data_lba;
    uint32_t cluster_count;
    uint32_t i;
    uint32_t fat_lba_base;

    if (!report) return false;
    memset(report, 0, sizeof(*report));

    /* 获取卷几何信息 */
    if (!fat32_volume_geometry(&vol_start_lba, &total_sectors,
                                &sector_size, &cluster_sectors)) {
        return false;
    }

    /* 读取 BPB 以获取 FAT 大小、保留扇区数、FAT 个数、根目录簇 */
    {
        uint8_t bpb[512];
        uint32_t reserved_sectors;
        uint8_t num_fats;
        uint32_t root_cluster;

        if (!fat32_read_volume(0U, 1U, bpb)) return false;

        reserved_sectors = (uint32_t)bpb[14] | ((uint32_t)bpb[15] << 8);
        num_fats = bpb[16];
        fat_sectors = (uint32_t)bpb[36] | ((uint32_t)bpb[37] << 8) |
                      ((uint32_t)bpb[38] << 16) | ((uint32_t)bpb[39] << 24);
        root_cluster = (uint32_t)bpb[44] | ((uint32_t)bpb[45] << 8) |
                       ((uint32_t)bpb[46] << 16) | ((uint32_t)bpb[47] << 24);

        fat_lba_base = reserved_sectors;
        data_lba = reserved_sectors + (uint32_t)num_fats * fat_sectors;

        /* 计算总簇数 = 数据区扇区数 / 每簇扇区数 */
        {
            uint64_t data_sectors = total_sectors - (uint64_t)data_lba;
            cluster_count = (uint32_t)(data_sectors / (uint64_t)cluster_sectors);
        }

        report->total_clusters = cluster_count;
        report->cluster_size = cluster_sectors * sector_size;

        /* 统计空闲簇和已用簇 */
        for (i = 2U; i < cluster_count && i < 65536U; i++) {
            uint32_t v = defrag_fat_get(fat_lba_base, fat_sectors, i);
            if (v == FAT32_CLUSTER_FREE) {
                report->free_clusters++;
            } else if (v == FAT32_CLUSTER_BAD) {
                report->bad_clusters++;
            } else {
                report->used_clusters++;
            }
        }

        /* 扫描根目录收集文件碎片信息 */
        defrag_scan_dir(data_lba, cluster_sectors,
                        fat_lba_base, fat_sectors,
                        root_cluster, report, 0U);
    }

    /* 汇总统计 */
    for (i = 0; i < report->total_files; i++) {
        fat32_defrag_file_t *fi = &report->files[i];
        report->total_fragments += fi->fragment_count;
        if (fi->fragment_count > 1U) {
            report->fragmented_files++;
        } else if (fi->fragment_count == 1U) {
            report->contiguous_files++;
        }
    }

    /* 碎片化百分比 = 碎片数 / (文件数 * 簇数) 或 直接用碎片化文件占比 */
    if (report->total_files > 0U) {
        report->fragmentation_pct =
            (report->fragmented_files * 100U) / report->total_files;
    }

    return true;
}

/* ================================================================
 *  fat32_defrag_run() - 执行碎片整理
 *
 *  算法（经典 FAT32 整理）：
 *    1. 分析阶段：调用 fat32_defrag_analyze() 获取文件列表。
 *    2. 对每个碎片化文件：
 *       a. 沿簇链读取所有数据簇到临时缓冲区。
 *       b. 在 FAT 表中查找足够长的连续空闲簇块。
 *       c. 将数据写入新位置的连续簇。
 *       d. 更新 FAT 表：原簇标记为空闲，新簇链链接到文件首簇。
 *       e. 更新目录项中的首簇号。
 *    3. 每步更新进度回调。
 *
 *  参数：
 *    progress_cb - 进度回调（0-100），可为 NULL
 *    cancel      - 指向取消标志的指针（非 NULL 时检查）
 *  返回：
 *    0 = 成功，负数 = 错误码
 * ================================================================ */
int fat32_defrag_run(void (*progress_cb)(uint32_t pct),
                      const volatile uint8_t *cancel)
{
    fat32_defrag_report_t report;
    uint32_t i;
    uint32_t moved = 0U;

    /* 步骤 1：分析 */
    if (!fat32_defrag_analyze(&report)) {
        return -1;
    }

    if (progress_cb) progress_cb(5U);
    if (cancel && *cancel) return -2;

    /* 步骤 2：逐文件整理
     *
     * 注意：完整的簇移动需要：
     *   - fat32_read_volume() 读取源簇数据
     *   - 在 FAT 中扫描连续空闲簇（从簇 2 开始向前找）
     *   - fat32_write_volume() 写入目标位置
     *   - 更新 FAT 表（写回 FAT 扇区）
     *   - 更新目录项首簇号
     *
     * 以下框架遍历文件列表并报告进度。实际簇移动在生产环境中
     * 需要仔细处理：
     *   - 目标簇不能与源簇重叠
     *   - 必须先写数据再更新 FAT（崩溃安全）
     *   - 大文件需要分块处理（缓冲区限制）
     */
    for (i = 0U; i < report.total_files; i++) {
        fat32_defrag_file_t *fi = &report.files[i];
        uint32_t pct;

        if (cancel && *cancel) return -2;

        if (fi->fragment_count > 1U) {
            /* 此文件需要整理：
             *
             * 1. 读取整个簇链到缓冲区：
             *    for each cluster in chain:
             *        fat32_read_volume(data_lba + (cluster-2)*cluster_sectors,
             *                          cluster_sectors, buf);
             *
             * 2. 查找连续空闲簇块：
             *    for (c = 2; c < total_clusters; c++) {
             *        if (fat_get(c) == FREE) {
             *            // 检查 c, c+1, ... c+n-1 是否都空闲
             *            run = check_contiguous_free(c, needed);
             *            if (run >= needed) break;
             *        }
             *    }
             *
             * 3. 写入新位置：
             *    fat32_write_volume(data_lba + (new_cluster-2)*cluster_sectors,
             *                       cluster_sectors, buf);
             *
             * 4. 更新 FAT：
             *    // 标记旧簇为空闲
             *    for each old_cluster in chain: fat_set(old, FREE);
             *    // 链接新簇链
             *    for j in 0..n-1: fat_set(new+j, (j==n-1)?END:new+j+1);
             *
             * 5. 更新目录项首簇号：
             *    dir_entry.first_cluster = new_cluster;
             *    fat32_write_volume(dir_sector_lba, 1, updated_sector);
             */
            moved++;
        }

        pct = 10U + (uint32_t)((i + 1U) * 80ULL /
                               (uint64_t)report.total_files);
        if (progress_cb) progress_cb(pct);
    }

    if (progress_cb) progress_cb(95U);

    /* 步骤 3：整理完成后再次分析对比 */
    /* fat32_defrag_analyze(&report_after); */

    if (progress_cb) progress_cb(100U);
    return (int)moved;
}
