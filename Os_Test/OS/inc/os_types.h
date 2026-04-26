#pragma once

#include <stdint.h>

/*
 * ============================================================
 *  os_types.h
 *  - Gom toàn bộ kiểu dữ liệu dùng chung giữa:
 *      + lớp cấu hình tĩnh (Config/)
 *      + kernel runtime (OS/)
 *      + phần ứng dụng test (app/)
 *  - Giữ file này độc lập với app-specific enum để tránh phụ thuộc vòng.
 * ============================================================
 */

/* ------------------------------------------------------------
 * Trạng thái runtime của task.
 * ------------------------------------------------------------ */
typedef enum {
    OS_DORMANT = 0, /* Chưa có activation sẵn sàng để chạy */
    OS_READY   = 1, /* Đã ở ready queue, chờ scheduler chọn */
    OS_RUNNING = 2, /* Đang thực thi trên CPU */
    OS_WAITING = 3  /* Dành chỗ cho hướng event/blocking về sau */
} OsTaskState_e;

typedef uint32_t EventMaskType;

/* ------------------------------------------------------------
 * Chế độ scheduler của toàn hệ thống.
 *
 * Lưu ý:
 * - PRIORITY_FIFO và NON_PREEMPTIVE ở project hiện tại có cùng
 *   hành vi dispatch vì ta chưa mở rộng event/wait API.
 * - Dù vậy vẫn giữ hai mode riêng để bám sát tài liệu và để
 *   tương lai dễ mở rộng semantics khác nhau.
 * ------------------------------------------------------------ */
typedef enum {
    OS_SCHEDMODE_PRIORITY_FIFO    = 0,
    OS_SCHEDMODE_FULL_PREEMPTIVE  = 1,
    OS_SCHEDMODE_NON_PREEMPTIVE   = 2,
    OS_SCHEDMODE_MIXED            = 3
} OsSchedulerMode_e;

/* ------------------------------------------------------------
 * Tính chất preemptibility gắn với từng task.
 * ------------------------------------------------------------ */
typedef enum {
    OS_TASK_SCHED_PREEMPTIVE     = 0,
    OS_TASK_SCHED_NON_PREEMPTIVE = 1
} OsTaskSchedClass_e;

/* ------------------------------------------------------------
 * Cấu hình tĩnh của một task.
 * - name: chỉ dùng cho trace/debug/test, không ảnh hưởng scheduling.
 * - is_idle_task: đánh dấu idle để kernel xử lý như một ngữ cảnh fallback.
 * ------------------------------------------------------------ */
typedef struct {
    uint8_t             id;
    const char         *name;
    void              (*entry)(void *arg);
    void               *arg;
    uint16_t            stack_words;
    uint8_t             base_priority;
    uint8_t             max_activations;
    uint8_t             autostart;
    uint8_t             is_idle_task;
    OsTaskSchedClass_e  sched_class;
} OsTaskConfig_t;

/* ------------------------------------------------------------
 * Runtime state của alarm tối giản.
 * Tên field còn giữ hậu tố "_ticks" để phản ánh đúng đơn vị runtime.
 * ------------------------------------------------------------ */
typedef struct {
    uint8_t   active;
    uint32_t  remain_ticks;
    uint32_t  cycle_ticks;
    uint8_t   target_task;
} OsAlarm_t;

/* ------------------------------------------------------------
 * Cấu hình tĩnh của alarm.
 * - autostart = 1 -> OS_Init() sẽ nạp alarm ngay.
 * - cycle_ms = 0  -> one-shot.
 * ------------------------------------------------------------ */
typedef struct {
    uint8_t      id;
    const char  *name;
    uint8_t      autostart;
    uint32_t     delay_ms;
    uint32_t     cycle_ms;
    uint8_t      target_task;
} OsAlarmConfig_t;

/* ------------------------------------------------------------
 * Một expiry point của ScheduleTable.
 * Bản hiện tại chỉ hỗ trợ action ActivateTask() để giữ kernel gọn,
 * nhưng cấu trúc đã tách riêng để sau này có thể thêm SetEvent/callback.
 * ------------------------------------------------------------ */
typedef struct {
    uint32_t  offset_ticks;
    uint8_t   task_id;
} OsScheduleTableExpiryPointCfg_t;

/* ------------------------------------------------------------
 * Cấu hình tĩnh của ScheduleTable.
 * - start_delay_ms: khoảng trì hoãn trước khi chu kỳ đầu tiên bắt đầu.
 * - duration_ms:    độ dài một chu kỳ của table.
 * - repeating:      1 nếu table tự lặp lại khi hết chu kỳ.
 * ------------------------------------------------------------ */
typedef struct {
    uint8_t                               id;
    const char                           *name;
    uint8_t                               autostart;
    uint8_t                               repeating;
    uint32_t                              start_delay_ms;
    uint32_t                              duration_ms;
    const OsScheduleTableExpiryPointCfg_t *expiry_points;
    uint8_t                               num_expiry_points;
} OsScheduleTableConfig_t;

/* ------------------------------------------------------------
 * Trạng thái runtime của ScheduleTable.
 * - start_delay_ticks: countdown trước khi vào chu kỳ đầu tiên.
 * - elapsed_ticks:     offset hiện tại tính trong chu kỳ đang chạy.
 * - current_expiry_idx: expiry point kế tiếp chưa được xử lý.
 * ------------------------------------------------------------ */
typedef enum {
    OS_SCHEDULE_TABLE_STOPPED = 0,
    OS_SCHEDULE_TABLE_RUNNING = 1
} OsScheduleTableState_e;

typedef struct {
    uint8_t   active;
    uint8_t   repeating;
    uint8_t   current_expiry_idx;
    uint32_t  start_delay_ticks;
    uint32_t  elapsed_ticks;
    uint32_t  duration_ticks;
} OsScheduleTable_t;
