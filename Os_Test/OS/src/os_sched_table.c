#include "os_kernel.h"
#include "os_sched_table.h"
#include "stm32f10x.h"

#include <stdint.h>

/*
 * ============================================================
 *  os_sched_table.c
 *  - Module time-triggered scheduling tối giản cho project Os_Test.
 *  - Vai trò của module này rất rõ:
 *      + giữ runtime state của từng ScheduleTable
 *      + giảm countdown start delay
 *      + theo dõi offset trong chu kỳ
 *      + đến expiry point thì gọi ActivateTask()
 *
 *  Module này KHÔNG tự chọn task chạy ngay.
 *  Nó chỉ bơm activation vào kernel, còn scheduler core vẫn là nơi quyết định
 *  dispatch theo mode full/non/mixed hiện hành.
 * ============================================================
 */

static OsScheduleTable_t g_sched_table_rt[OS_MAX_SCHEDULE_TABLES];

/* ------------------------------------------------------------
 * Critical section nội bộ của module.
 * Dùng cùng triết lý save/restore PRIMASK như kernel chính để không vô tình
 * mở lại ngắt nếu caller trước đó vốn đã đang ở critical section.
 * ------------------------------------------------------------ */
static inline uint32_t os_sched_irq_save(void)
{
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    __DSB();
    __ISB();
    return primask;
}

static inline void os_sched_irq_restore(uint32_t primask)
{
    __DSB();
    __ISB();
    __set_PRIMASK(primask);
}

/* ------------------------------------------------------------
 * Helper đổi ms sang tick.
 * Giữ quy tắc làm tròn lên để delay > 0 luôn tương ứng ít nhất 1 tick.
 * ------------------------------------------------------------ */
static uint32_t os_sched_ms_to_ticks(uint32_t ms)
{
    if (ms == 0u)
    {
        return 0u;
    }

    uint64_t ticks = ((uint64_t)ms * (uint64_t)OS_TICK_HZ + 999ull) / 1000ull;
    if (ticks == 0ull)
    {
        ticks = 1ull;
    }
    if (ticks > 0xFFFFFFFFull)
    {
        ticks = 0xFFFFFFFFull;
    }
    return (uint32_t)ticks;
}

/* ------------------------------------------------------------
 * Thực thi tất cả expiry point đã "đến hạn" ở offset hiện tại.
 * Dùng so sánh <= thay vì == để nếu một tick bị trễ thì vẫn không đánh rơi
 * expiry point trong demo/test.
 * ------------------------------------------------------------ */
static void os_sched_table_fire_due_points(uint8_t sid)
{
    const OsScheduleTableConfig_t *cfg = &g_os_schedule_table_cfg[sid];
    OsScheduleTable_t *rt = &g_sched_table_rt[sid];

    while (rt->current_expiry_idx < cfg->num_expiry_points)
    {
        const OsScheduleTableExpiryPointCfg_t *point =
            &cfg->expiry_points[rt->current_expiry_idx];

        if (point->offset_ticks > rt->elapsed_ticks)
        {
            break;
        }

        ActivateTask(point->task_id);
        rt->current_expiry_idx++;
    }
}

/* ------------------------------------------------------------
 * Xử lý khi đã đi hết một chu kỳ ScheduleTable.
 * - Nếu repeating: reset offset và lập tức xử lý expiry point offset 0
 *   của chu kỳ kế tiếp ngay tại boundary.
 * - Nếu one-shot: dừng hẳn table.
 * ------------------------------------------------------------ */
static void os_sched_table_finish_cycle(uint8_t sid)
{
    OsScheduleTable_t *rt = &g_sched_table_rt[sid];

    if ((rt->duration_ticks == 0u) || (rt->elapsed_ticks < rt->duration_ticks))
    {
        return;
    }

    if (rt->repeating != 0u)
    {
        rt->elapsed_ticks = 0u;
        rt->current_expiry_idx = 0u;
        os_sched_table_fire_due_points(sid);
    }
    else
    {
        rt->active = 0u;
        rt->current_expiry_idx = 0u;
        rt->elapsed_ticks = 0u;
        rt->start_delay_ticks = 0u;
    }
}

/* ------------------------------------------------------------
 * Start runtime của một table với delay tương đối tính theo ms.
 * Bản hiện tại cố tình quy đổi delay 0 ms thành 1 tick để tránh gọi
 * ActivateTask() ngay trong lúc caller còn đang cấu hình object.
 * Điều này giữ flow đơn giản và đủ cho toàn bộ kịch bản test hiện tại.
 * ------------------------------------------------------------ */
void StartScheduleTableRel(uint8_t sid, uint32_t start_delay_ms)
{
    if (sid >= OS_MAX_SCHEDULE_TABLES)
    {
        return;
    }

    const OsScheduleTableConfig_t *cfg = &g_os_schedule_table_cfg[sid];
    if ((cfg->num_expiry_points == 0u) || (cfg->expiry_points == 0) ||
        (cfg->duration_ms == 0u))
    {
        return;
    }

    uint32_t primask = os_sched_irq_save();
    OsScheduleTable_t *rt = &g_sched_table_rt[sid];

    rt->active = 1u;
    rt->repeating = cfg->repeating;
    rt->current_expiry_idx = 0u;
    rt->elapsed_ticks = 0u;
    rt->duration_ticks = os_sched_ms_to_ticks(cfg->duration_ms);
    rt->start_delay_ticks = os_sched_ms_to_ticks((start_delay_ms == 0u) ? 1u
                                                                        : start_delay_ms);

    os_sched_irq_restore(primask);
}

/* ------------------------------------------------------------
 * Stop table và xóa toàn bộ runtime state của nó.
 * ------------------------------------------------------------ */
void StopScheduleTable(uint8_t sid)
{
    if (sid >= OS_MAX_SCHEDULE_TABLES)
    {
        return;
    }

    uint32_t primask = os_sched_irq_save();
    OsScheduleTable_t *rt = &g_sched_table_rt[sid];

    rt->active = 0u;
    rt->repeating = 0u;
    rt->current_expiry_idx = 0u;
    rt->start_delay_ticks = 0u;
    rt->elapsed_ticks = 0u;
    rt->duration_ticks = 0u;

    os_sched_irq_restore(primask);
}

/* ------------------------------------------------------------
 * Reset runtime state của toàn bộ ScheduleTable.
 * ------------------------------------------------------------ */
void os_sched_table_reset_all(void)
{
    for (uint8_t sid = 0u; sid < OS_MAX_SCHEDULE_TABLES; ++sid)
    {
        g_sched_table_rt[sid].active = 0u;
        g_sched_table_rt[sid].repeating = 0u;
        g_sched_table_rt[sid].current_expiry_idx = 0u;
        g_sched_table_rt[sid].start_delay_ticks = 0u;
        g_sched_table_rt[sid].elapsed_ticks = 0u;
        g_sched_table_rt[sid].duration_ticks = 0u;
    }
}

/* ------------------------------------------------------------
 * Autostart các table được bật trong bảng config.
 * ------------------------------------------------------------ */
void os_sched_table_autostart_all(void)
{
    for (uint8_t sid = 0u; sid < OS_MAX_SCHEDULE_TABLES; ++sid)
    {
        const OsScheduleTableConfig_t *cfg = &g_os_schedule_table_cfg[sid];
        if (cfg->autostart != 0u)
        {
            StartScheduleTableRel(sid, cfg->start_delay_ms);
        }
    }
}

/* ------------------------------------------------------------
 * Tick handler của module ScheduleTable.
 * Hàm này được gọi từ os_on_tick() sau khi kernel đã xử lý alarm.
 * ------------------------------------------------------------ */
void os_sched_table_on_tick(void)
{
    for (uint8_t sid = 0u; sid < OS_MAX_SCHEDULE_TABLES; ++sid)
    {
        OsScheduleTable_t *rt = &g_sched_table_rt[sid];

        if (rt->active == 0u)
        {
            continue;
        }

        /*
         * Giai đoạn 1:
         * Chưa bắt đầu chu kỳ chính, chỉ giảm countdown start delay.
         * Khi countdown chạm 0, offset logic của table được xem là 0 và các
         * expiry point tại offset 0 sẽ được xử lý ngay trong cùng tick đó.
         */
        if (rt->start_delay_ticks > 0u)
        {
            rt->start_delay_ticks--;
            if (rt->start_delay_ticks == 0u)
            {
                rt->elapsed_ticks = 0u;
                rt->current_expiry_idx = 0u;
                os_sched_table_fire_due_points(sid);
                os_sched_table_finish_cycle(sid);
            }
            continue;
        }

        /*
         * Giai đoạn 2:
         * Table đã chạy ổn định, mỗi tick tăng elapsed_ticks một lần rồi kiểm
         * tra tất cả expiry point đã tới hạn.
         */
        rt->elapsed_ticks++;
        os_sched_table_fire_due_points(sid);
        os_sched_table_finish_cycle(sid);
    }
}
