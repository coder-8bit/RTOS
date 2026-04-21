#pragma once

#include <stdint.h>

#include "os_types.h"

/*
 * ============================================================
 *  os_app_cfg.h
 *  - Đây là cấu hình duy nhất của ứng dụng cho bài test Non-Preemptive.
 * ============================================================
 */

/* ------------------------------------------------------------
 * ID logic của task.
 * ------------------------------------------------------------ */
typedef enum {
    TASK_INIT = 0,            /* Task bootstrap phần cứng và banner runtime */
    TASK_NP_LOW,              /* Task chạy nền, tốn thời gian, priority thấp */
    TASK_NP_HIGH,             /* Task khẩn cấp, priority ưu tiên cao, cắt ngang */
    TASK_IDLE,                /* Task rỗi của hệ thống */
    TASK_COUNT
} TaskId_e;

/* ------------------------------------------------------------
 * ID logic của alarm.
 * ------------------------------------------------------------ */
typedef enum {
    ALARM_NP_LOW = 0,
    ALARM_NP_HIGH,
    ALARM_COUNT
} AlarmId_e;

/* ------------------------------------------------------------
 * ID logic của ScheduleTable.
 * ------------------------------------------------------------ */
typedef enum {
    SCHEDULE_TABLE_MAIN = 0,
    SCHEDULE_TABLE_COUNT
} ScheduleTableId_e;

/* ============================================================
 * Prototype task của ứng dụng
 * ============================================================ */
void Task_Init(void *arg);
void Task_NP_Low(void *arg);
void Task_NP_High(void *arg);
void Task_Idle(void *arg);

/* ============================================================
 * Bảng cấu hình tĩnh do Config/os_app_cfg.c cung cấp
 * ============================================================ */
extern const OsTaskConfig_t g_os_task_cfg[TASK_COUNT];
extern const OsAlarmConfig_t g_os_alarm_cfg[ALARM_COUNT];
extern const OsScheduleTableConfig_t g_os_schedule_table_cfg[SCHEDULE_TABLE_COUNT];

/* Helper chỉ phục vụ trace/test để biết build hiện tại đang chạy profile nào. */
const char *App_GetActiveProfileName(void);
