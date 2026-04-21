#pragma once

/*
 * ============================================================
 *  os_sched_table.h
 *  - Header private giữa kernel core và module ScheduleTable.
 *  - File này không dành cho application layer; app chỉ gọi API public
 *    qua os_kernel.h nếu thật sự cần.
 * ============================================================
 */

void os_sched_table_reset_all(void);
void os_sched_table_autostart_all(void);
void os_sched_table_on_tick(void);
