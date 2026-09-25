#ifndef _CHKDSK_H_
#define _CHKDSK_H_

/*
 * 开机自动文件系统检查。
 *
 *  - chkdsk_auto_on_boot(): 由 kernel_main 在挂载根文件系统后调用。
 *    若上次为非正常关机（FAT32 WAL 日志头处于 DIRTY），则自动运行
 *    FAT32 一致性检查与修复，结果输出到串口/调试控制台。
 */
void chkdsk_auto_on_boot(void);

#endif
