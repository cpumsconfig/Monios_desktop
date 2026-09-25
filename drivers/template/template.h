/*
 * template.h - Monios 驱动模板头文件。
 *
 * 本文件定义驱动内部使用的设备上下文与调试宏。
 * 真实驱动请复制本目录并改名：
 *   drivers/template/template.c  ->  drivers/mydriver/mydriver.c
 *   drivers/template/template.h  ->  drivers/mydriver/mydriver.h
 */

#ifndef _TEMPLATE_H_
#define _TEMPLATE_H_

#include "driver_api.h"
#include "stdbool.h"
#include "stdint.h"

/* 驱动版本号（自行修改）。 */
#define TEMPLATE_DRIVER_MAJOR   1u
#define TEMPLATE_DRIVER_MINOR   0u

/* 设备上下文：每个设备实例一份，记录端口/中断/状态。 */
typedef struct {
    uint16_t    io_base;     /* 设备 IO 端口基址（端口映射设备用） */
    uint8_t     irq;         /* 中断号；0 表示无中断 */
    bool        opened;      /* 设备是否已打开 */
    uint32_t    read_ops;    /* 累计读次数（统计用） */
    uint32_t    write_ops;   /* 累计写次数 */
} template_device_t;

/* 模块级日志函数指针（由 DriverEntry 从 runtime 保存）。 */
extern monios_driver_log_fn_t template_log;

/* 简易日志宏：需要先有 g_log/runtime->log。 */
#define TPL_LOG(...) do { if (template_log) template_log(__VA_ARGS__); } while (0)

#endif /* _TEMPLATE_H_ */
