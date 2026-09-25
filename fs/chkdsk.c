/* ============================================================
 *  开机自动 chkdsk
 *
 *  挂载根文件系统后由 kernel_main 调用。检测上次是否为非正常关机：
 *    - 正常关机：fat32_wal_mark_clean() 已把 WAL 日志头置为 CLEAN，
 *      fat32_wal_dirty_at_mount() 返回 false，直接跳过。
 *    - 非正常关机：WAL 已在 fat32_init() 中完成 redo/undo 恢复，
 *      fat32_wal_dirty_at_mount() 返回 true，这里再跑一遍 FAT32 一致性
 *      检查/修复（双保险），结果输出到串口。
 * ============================================================ */

#include "common.h"
#include "fat32.h"
#include "chkdsk.h"
#include "kernel.h"

static void chk_log_u32(const char *prefix, uint32_t v)
{
    char buf[32];
    uint32_t p = 0;
    char tmp[12];
    uint32_t n = 0;

    while (prefix != NULL && *prefix != '\0' && p < 20U) {
        buf[p++] = *prefix++;
    }
    if (v == 0U) {
        buf[p++] = '0';
    } else {
        while (v > 0U && n < 11U) {
            tmp[n++] = (char) ('0' + (v % 10U));
            v /= 10U;
        }
        while (n > 0U && p < 31U) {
            buf[p++] = tmp[--n];
        }
    }
    buf[p] = '\0';
    serial_write(buf);
}

void chkdsk_auto_on_boot(void)
{
    fat32_chkdsk_report_t report;

    if (!fat32_wal_dirty_at_mount()) {
        return;   /* 干净卸载，无需检查 */
    }

    serial_write("chkdsk: dirty shutdown detected, checking FAT32 volume\r\n");
    if (!fat32_chkdsk(&report)) {
        serial_write("chkdsk: FAT32 not mounted or check aborted\r\n");
        return;
    }

    chk_log_u32("chkdsk: errors=", report.errors);
    chk_log_u32(" fixed=", report.fixed);
    chk_log_u32(" files=", report.files);
    serial_write("\r\n");

    if (report.errors == 0U) {
        serial_write("chkdsk: volume clean\r\n");
    } else if (report.errors == report.fixed) {
        serial_write("chkdsk: all errors fixed\r\n");
    } else {
        serial_write("chkdsk: some errors remain\r\n");
    }
}
