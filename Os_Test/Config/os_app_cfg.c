#include "os_app_cfg.h"
#include "os_config.h"

/*
 * ============================================================
 *  os_app_cfg.c
 *  - Hiện thực toàn bộ bảng cấu hình tĩnh của ứng dụng.
 *  - Phiên bản siêu đơn giản: Chỉ chứa bảng cấu hình cho thuật toán
 *    nhất định: NON PREEMPTIVE.
 * ============================================================
 */

/* ------------------------------------------------------------
 * Macro tiện dụng cho ScheduleTable không dùng.
 * ------------------------------------------------------------ */
#define OS_SCHTBL_DISABLED(_id, _name_literal)         \
    {                                                  \
        .id = (_id),                                   \
        .name = (_name_literal),                       \
        .autostart = 0u,                               \
        .repeating = 0u,                               \
        .start_delay_ms = 0u,                          \
        .duration_ms = 0u,                             \
        .expiry_points = 0,                            \
        .num_expiry_points = 0u                        \
    }

/* ------------------------------------------------------------
 * Bảng task tĩnh cho Non Preemptive.
 * ------------------------------------------------------------ */
const OsTaskConfig_t g_os_task_cfg[TASK_COUNT] = {
    [TASK_INIT] = {
        .id = TASK_INIT,
        .name = "Task_Init",
        .entry = Task_Init,
        .arg = 0,
        .stack_words = 192u,
        .base_priority = OS_PRIO_CRITICAL,
        .max_activations = 1u,
        .autostart = 1u,
        .is_idle_task = 0u,
        .sched_class = OS_TASK_SCHED_NON_PREEMPTIVE
    },
    [TASK_NP_LOW] = {
        .id = TASK_NP_LOW,
        .name = "Task_NP_Low",
        .entry = Task_NP_Low,
        .arg = 0,
        .stack_words = 160u,
        .base_priority = OS_PRIO_LOW,
        .max_activations = 1u,
        .autostart = 0u,
        .is_idle_task = 0u,
        .sched_class = OS_TASK_SCHED_NON_PREEMPTIVE
    },
    [TASK_NP_HIGH] = {
        .id = TASK_NP_HIGH,
        .name = "Task_NP_High",
        .entry = Task_NP_High,
        .arg = 0,
        .stack_words = 160u,
        .base_priority = OS_PRIO_CRITICAL,
        .max_activations = 2u,
        .autostart = 0u,
        .is_idle_task = 0u,
        .sched_class = OS_TASK_SCHED_NON_PREEMPTIVE
    },
    [TASK_IDLE] = {
        .id = TASK_IDLE,
        .name = "Task_Idle",
        .entry = Task_Idle,
        .arg = 0,
        .stack_words = 128u,
        .base_priority = OS_PRIO_IDLE,
        .max_activations = 1u,
        .autostart = 1u,
        .is_idle_task = 1u,
        .sched_class = OS_TASK_SCHED_NON_PREEMPTIVE
    }
};

/* ------------------------------------------------------------
 * Bảng alarm tĩnh kích hoạt 2 task theo thời gian định trước.
 * ------------------------------------------------------------ */
const OsAlarmConfig_t g_os_alarm_cfg[ALARM_COUNT] = {
    [ALARM_NP_LOW] = {
        .id = ALARM_NP_LOW,
        .name = "Alarm_NP_Low",
        .autostart = 1u,
        .delay_ms = 20u, // Chạy ở tick 20
        .cycle_ms = 0u,
        .target_task = TASK_NP_LOW
    },
    [ALARM_NP_HIGH] = {
        .id = ALARM_NP_HIGH,
        .name = "Alarm_NP_High",
        .autostart = 1u,
        .delay_ms = 40u, // Chạy ở tick 40, lúc Low vẫn đang bận
        .cycle_ms = 0u,
        .target_task = TASK_NP_HIGH
    }
};

/* ------------------------------------------------------------
 * Bảng ScheduleTable tĩnh trống.
 * ------------------------------------------------------------ */
const OsScheduleTableConfig_t g_os_schedule_table_cfg[SCHEDULE_TABLE_COUNT] = {
    [SCHEDULE_TABLE_MAIN] = OS_SCHTBL_DISABLED(SCHEDULE_TABLE_MAIN, "ScheduleTable_Main")
};

/* ------------------------------------------------------------
 * Helper phục vụ trace/test.
 * ------------------------------------------------------------ */
const char *App_GetActiveProfileName(void)
{
    return "NON_PREEMPTIVE_ONLY";
}
