#ifndef OS_KERNEL_H
#define OS_KERNEL_H

/*
 * =====================================================================
 *  os_kernel.h
 *  - Định nghĩa giao diện public của mini-kernel.
 *  - File này là cầu nối giữa:
 *      + application code (Task_Init, demo tasks, scheduler test tasks, idle)
 *      + kernel logic (scheduler, alarm, activation)
 *      + port layer Cortex-M3 (SysTick, SVC, PendSV)
 *
 *  Luồng chạy tổng quát của hệ thống:
 *      main()
 *        -> OS_Init()
 *        -> OS_Start()
 *        -> SVC_Handler launch task đầu tiên
 *        -> SysTick/PendSV điều phối các task tiếp theo
 * =====================================================================
 */

#include <stdbool.h>
#include <stdint.h>

#include "os_config.h"
#include "os_types.h"

/* Giá trị sentinel khi scheduler/ready queue không tìm được task hợp lệ. */
#define OS_INVALID_TASK_ID 0xFFu
#define OS_INVALID_SCHEDULE_TABLE_ID 0xFFu

/*
 * Khối điều khiển Task (TCB).
 *
 * Lưu ý cực quan trọng:
 * - field đầu tiên bắt buộc là `sp`
 * - assembly PendSV/SVC giả định offset 0 của TCB là stack pointer
 * - đổi thứ tự field mà không sửa assembly sẽ làm hỏng context switch
 */
typedef struct TCB {
    uint32_t         *sp;                 /* &R4 của SW-frame hiện tại */
    uint32_t         *stack_base;         /* Base của mảng stack tĩnh */
    uint32_t          stack_words;        /* Kích thước stack tính theo word */
    void            (*entry)(void *arg);  /* Entry function khi task được (re)start */
    void             *arg;                /* Tham số truyền vào entry */
    const char       *name;               /* Tên task phục vụ trace/test */
    uint8_t           id;                 /* ID logic của task */
    uint8_t           base_priority;      /* Priority cấu hình ban đầu */
    uint8_t           current_priority;   /* Priority runtime, có thể đổi khi mở rộng PCP */
    uint8_t           max_activations;    /* Số activation tối đa cho phép */
    uint8_t           sched_class;        /* PREEMPTIVE / NON_PREEMPTIVE */
    uint8_t           is_idle_task;       /* 1 nếu đây là idle task của hệ thống */
    volatile uint8_t  activation_count;   /* Số activation hiện đang tích lũy */
    volatile uint8_t  context_needs_init; /* 1 nếu lần chạy tới phải dựng lại stack */
    volatile uint8_t  state;              /* Trạng thái runtime hiện tại */
    volatile EventMaskType event_set;     /* Các event bit đã được báo cho task */
    volatile EventMaskType wait_mask;     /* Các event bit task đang chờ */
} TCB_t;

/* =========================================================
 *  Biến toàn cục Scheduler (dùng trong ASM handler)
 *  - PendSV_Handler (ASM) sẽ đọc/ghi các biến này
 * ========================================================= */
extern volatile TCB_t *g_current;
extern volatile TCB_t *g_next;
extern volatile uint8_t g_interrupt_nesting;
extern volatile uint32_t g_tick_count;

/* =========================================================
 *  API Kernel cho ứng dụng và debug
 * ========================================================= */

/*
 * Khởi tạo kernel và toàn bộ đối tượng runtime tĩnh.
 * Công việc chính:
 * - reset trạng thái scheduler
 * - cấu hình port Cortex-M3
 * - dựng TCB và stack metadata cho từng task
 * - nạp ready queue ban đầu
 * - cài alarm và schedule table theo bảng cấu hình
 */
void OS_Init(void);

/*
 * Chuyển CPU từ thế giới "khởi tạo bằng C" sang "chạy task bằng PSP".
 * Hàm này không tự chọn task; nó dùng g_current đã được chuẩn bị trong OS_Init().
 */
void OS_Start(void);

/*
 * Kích hoạt task theo ID.
 * Với task đang DORMANT:
 * - tăng activation_count
 * - đánh dấu context cần init lại
 * - đưa vào ready queue
 * - có thể gọi scheduler ngay nếu không ở trong ISR
 */
void ActivateTask(uint8_t tid);

/*
 * Task tự kết thúc phần việc hiện tại.
 * Semantics hiện tại:
 * - nếu không còn activation pending -> về DORMANT
 * - nếu còn activation pending      -> về READY để chạy lại từ đầu
 * - sau đó gọi scheduler để chọn task tiếp theo
 */
void TerminateTask(void);

/* Primitive event tối giản: mỗi task sở hữu một event_set 32-bit. */
void WaitEvent(EventMaskType mask);
void SetEvent(uint8_t tid, EventMaskType mask);
void ClearEvent(EventMaskType mask);
void GetEvent(uint8_t tid, EventMaskType *mask);

/* Đặt alarm tương đối, delay/cycle truyền từ góc nhìn application theo ms. */
void SetRelAlarm(uint8_t aid, uint32_t delay_ms, uint32_t cycle_ms, uint8_t target_tid);
void CancelAlarm(uint8_t aid);

/* API tối giản cho ScheduleTable ở bản refactor. */
void StartScheduleTableRel(uint8_t sid, uint32_t start_delay_ms);
void StopScheduleTable(uint8_t sid);

/* Hàm service tick được gọi từ SysTick_Handler. */
void os_on_tick(void);

/* Scheduler/dispatcher public để thuận tiện breakpoint khi học và debug. */
void os_schedule(void);
void os_dispatch(void);

/*
 * Helper dùng bởi assembly:
 * - đảm bảo task có stack frame hợp lệ trước khi restore
 * - nếu task vừa activate lại thì sẽ dựng PSP mới từ entry function
 */
uint32_t *os_get_task_stack_ptr_for_switch(TCB_t *task);

/* =========================================================
 *  Helper runtime phục vụ trace/debug/test
 * ========================================================= */
uint32_t OS_GetTickCount(void);
OsSchedulerMode_e OS_GetSchedulerMode(void);
const char *OS_GetSchedulerModeName(void);
const char *OS_GetTaskName(uint8_t tid);

#endif /* OS_KERNEL_H */
