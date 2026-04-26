#pragma once

#include "os_app_cfg.h"

/*
 * ============================================================
 *  os_config.h
 *  - Chứa các cấu hình build-time dùng chung cho toàn kernel.
 *  - Tư duy của bản refactor:
 *      + cấu hình đối tượng cụ thể (task/alarm/table) nằm ở os_app_cfg.c
 *      + cấu hình hạ tầng chung (tick, priority scale, storage limit)
 *        nằm ở file này
 * ============================================================
 */

/* ------------------------------------------------------------
 * Tick hệ thống.
 * 1000 Hz => 1 tick = 1 ms, thuận tiện cho cả demo và test script.
 * ------------------------------------------------------------ */
#ifndef OS_TICK_HZ
#define OS_TICK_HZ 1000u
#endif

/* ------------------------------------------------------------
 * Semihosting mặc định tắt để không làm gãy flow Renode batch.
 * ------------------------------------------------------------ */
#ifndef OS_ENABLE_SEMIHOSTING
#define OS_ENABLE_SEMIHOSTING 0u
#endif

/* ------------------------------------------------------------
 * Idle mặc định dùng WFI để mô phỏng hành vi RTOS tiết kiệm năng lượng.
 * ------------------------------------------------------------ */
#ifndef OS_IDLE_USE_WFI
#define OS_IDLE_USE_WFI 1u
#endif

#ifndef OS_SCHED_MODE_DEFAULT
#define OS_SCHED_MODE_DEFAULT OS_SCHEDMODE_MIXED
#endif

/* ------------------------------------------------------------
 * Kích thước các bảng đối tượng được suy ra trực tiếp từ enum app config.
 * Đây là điểm quan trọng để kernel không còn lệ thuộc vào hard-code thủ công.
 * ------------------------------------------------------------ */
#define OS_MAX_TASKS            TASK_COUNT
#define OS_MAX_ALARMS           ALARM_COUNT
#define OS_MAX_SCHEDULE_TABLES  SCHEDULE_TABLE_COUNT

/* ------------------------------------------------------------
 * Vùng stack vật lý cấp phát theo "slot" có kích thước cực đại chung.
 * Mỗi task chỉ dùng phần stack_words của chính nó, nhưng storage tĩnh sẽ
 * được đặt theo giới hạn này để đơn giản hóa layout RAM.
 * ------------------------------------------------------------ */
#define OS_TASK_STACK_STORAGE_WORDS_MAX  192u

/* ------------------------------------------------------------
 * Thang priority của hệ thống.
 * Giá trị càng lớn thì độ ưu tiên càng cao.
 * ------------------------------------------------------------ */
#define OS_PRIO_IDLE      0u
#define OS_PRIO_LOW       1u
#define OS_PRIO_MEDIUM    2u
#define OS_PRIO_HIGH      3u
#define OS_PRIO_CRITICAL  4u

#define OS_MAX_PRIORITY    OS_PRIO_CRITICAL
#define OS_PRIORITY_LEVELS (OS_MAX_PRIORITY + 1u)
