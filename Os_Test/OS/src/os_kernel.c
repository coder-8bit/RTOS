/*
 * =====================================================================
 *  os_kernel.c
 *  - Kernel core của project Os_Test sau khi refactor.
 *
 *  Tư duy kiến trúc của bản này:
 *  1. Ready queue vẫn là priority queue + FIFO trong cùng mức priority.
 *  2. Policy preemption được tách riêng bằng scheduler mode:
 *       - PRIORITY_FIFO
 *       - FULL_PREEMPTIVE
 *       - NON_PREEMPTIVE
 *       - MIXED
 *  3. Cấu hình task/alarm/schedule table không còn hard-code trong OS_Init().
 *     Thay vào đó, kernel nạp bảng tĩnh từ Config/os_app_cfg.c.
 *  4. Port/ASM gần như không phải đổi, vì contract với TCB vẫn được giữ:
 *       - field đầu tiên của TCB vẫn là sp
 *       - PendSV/SVC vẫn chỉ save/restore context
 *       - phần "ai chạy tiếp" vẫn do C quyết định
 * =====================================================================
 */

#include "os_kernel.h"
#include "os_port.h"
#include "os_sched_table.h"
#include "stm32f10x.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

_Static_assert(OS_MAX_TASKS >= 2u, "OS_MAX_TASKS must be >= 2");
_Static_assert(OS_MAX_ALARMS >= 1u, "OS_MAX_ALARMS must be >= 1");
_Static_assert(OS_MAX_TASKS <= 255u,
               "OS_MAX_TASKS must fit into uint8_t ready queue entries");

/*
 * Ready queue cho một mức priority.
 * Mỗi priority có một ring buffer riêng để giữ FIFO trong cùng mức.
 */
typedef struct
{
    uint8_t buf[OS_MAX_TASKS];
    uint8_t head;
    uint8_t tail;
    uint8_t count;
} OsReadyQueuePrio_t;

/* ============================================================
 * Biến global mà assembly cần đọc/ghi trực tiếp
 * ============================================================ */
volatile TCB_t *g_current = NULL;
volatile TCB_t *g_next = NULL;
volatile uint8_t g_interrupt_nesting = 0u;
volatile uint32_t g_tick_count = 0u;

/* ============================================================
 * Runtime state tĩnh của kernel
 * ============================================================ */
static TCB_t tcb[OS_MAX_TASKS];
static uint32_t g_task_stack_storage[OS_MAX_TASKS][OS_TASK_STACK_STORAGE_WORDS_MAX];
static OsReadyQueuePrio_t ready_queues[OS_PRIORITY_LEVELS];
static bool task_queued[OS_MAX_TASKS];
static OsAlarm_t alarm_tbl[OS_MAX_ALARMS];

/*
 * Scheduler mode của firmware hiện tại là build-time constant.
 * Test script sẽ rebuild nhiều lần với define khác nhau để kiểm chứng.
 */
static const OsSchedulerMode_e g_scheduler_mode = OS_SCHED_MODE_DEFAULT;

/*
 * Idle task ID được suy ra từ bảng config để kernel không cần hard-code.
 * Nếu bảng config sai và không có idle task, OS_Init() vẫn sẽ cố chạy task 0,
 * nhưng test/runtime sẽ báo lỗi rất rõ qua hành vi bất thường.
 */
static uint8_t g_idle_task_id = OS_INVALID_TASK_ID;

/* ============================================================
 * Critical section helpers
 * ============================================================ */
static inline uint32_t os_irq_save(void)
{
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    __DSB();
    __ISB();
    return primask;
}

static inline void os_irq_restore(uint32_t primask)
{
    __DSB();
    __ISB();
    __set_PRIMASK(primask);
}

/* ============================================================
 * Helper nhỏ dùng nhiều nơi
 * ============================================================ */
static inline uint8_t os_clamp_priority(uint8_t prio)
{
    return (prio > OS_MAX_PRIORITY) ? (uint8_t)OS_MAX_PRIORITY : prio;
}

static inline uint32_t *os_task_stack_top(const TCB_t *task)
{
    return task->stack_base + task->stack_words;
}

static inline bool os_is_idle_task(const TCB_t *task)
{
    return (task != NULL) && (task->is_idle_task != 0u);
}

/*
 * Helper đổi ms sang tick theo quy tắc làm tròn lên.
 * Hàm này dùng cho alarm; ScheduleTable có helper tương tự ở module riêng.
 */
static uint32_t ms_to_ticks(uint32_t ms)
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

/* ============================================================
 * Ready queue
 * ============================================================ */
static void os_ready_queue_reset(void)
{
    for (uint8_t prio = 0u; prio < OS_PRIORITY_LEVELS; ++prio)
    {
        ready_queues[prio].head = 0u;
        ready_queues[prio].tail = 0u;
        ready_queues[prio].count = 0u;
    }

    for (uint8_t tid = 0u; tid < OS_MAX_TASKS; ++tid)
    {
        task_queued[tid] = false;
    }
}

static inline void os_ready_enqueue_tail(uint8_t prio, uint8_t tid)
{
    OsReadyQueuePrio_t *q = &ready_queues[prio];
    if (q->count >= OS_MAX_TASKS)
    {
        return;
    }

    q->buf[q->tail] = tid;
    q->tail = (uint8_t)((q->tail + 1u) % OS_MAX_TASKS);
    q->count++;
    task_queued[tid] = true;
}

static inline void os_ready_enqueue_head(uint8_t prio, uint8_t tid)
{
    OsReadyQueuePrio_t *q = &ready_queues[prio];
    if (q->count >= OS_MAX_TASKS)
    {
        return;
    }

    q->head = (uint8_t)((q->head + OS_MAX_TASKS - 1u) % OS_MAX_TASKS);
    q->buf[q->head] = tid;
    q->count++;
    task_queued[tid] = true;
}

static inline void os_ready_add(uint8_t tid, uint8_t prio)
{
    if ((tid >= OS_MAX_TASKS) || task_queued[tid])
    {
        return;
    }

    tcb[tid].state = OS_READY;
    os_ready_enqueue_tail(os_clamp_priority(prio), tid);
}

static inline void os_ready_add_front(uint8_t tid, uint8_t prio)
{
    if ((tid >= OS_MAX_TASKS) || task_queued[tid])
    {
        return;
    }

    tcb[tid].state = OS_READY;
    os_ready_enqueue_head(os_clamp_priority(prio), tid);
}

static uint8_t os_ready_peek_highest(void)
{
    for (int prio = (int)OS_MAX_PRIORITY; prio >= 0; --prio)
    {
        const OsReadyQueuePrio_t *q = &ready_queues[prio];
        if (q->count > 0u)
        {
            return q->buf[q->head];
        }
    }

    return OS_INVALID_TASK_ID;
}

static uint8_t os_ready_pop_highest(void)
{
    for (int prio = (int)OS_MAX_PRIORITY; prio >= 0; --prio)
    {
        OsReadyQueuePrio_t *q = &ready_queues[prio];
        if (q->count > 0u)
        {
            uint8_t tid = q->buf[q->head];
            q->head = (uint8_t)((q->head + 1u) % OS_MAX_TASKS);
            q->count--;
            task_queued[tid] = false;
            return tid;
        }
    }

    return OS_INVALID_TASK_ID;
}

/* ============================================================
 * Alarm runtime
 * ============================================================ */
static void os_alarm_reset_all(void)
{
    for (uint8_t i = 0u; i < OS_MAX_ALARMS; ++i)
    {
        alarm_tbl[i].active = 0u;
        alarm_tbl[i].target_task = OS_INVALID_TASK_ID;
        alarm_tbl[i].remain_ticks = 0u;
        alarm_tbl[i].cycle_ticks = 0u;
    }
}

static void os_alarm_autostart_all(void)
{
    for (uint8_t aid = 0u; aid < OS_MAX_ALARMS; ++aid)
    {
        const OsAlarmConfig_t *cfg = &g_os_alarm_cfg[aid];
        if (cfg->autostart != 0u)
        {
            SetRelAlarm(cfg->id, cfg->delay_ms, cfg->cycle_ms, cfg->target_task);
        }
    }
}

/* ============================================================
 * Task setup từ bảng config
 * ============================================================ */
static void os_task_setup_from_cfg(uint8_t tid)
{
    const OsTaskConfig_t *cfg = &g_os_task_cfg[tid];
    TCB_t *task = &tcb[tid];
    uint16_t stack_words = cfg->stack_words;

    if ((stack_words == 0u) || (stack_words > OS_TASK_STACK_STORAGE_WORDS_MAX))
    {
        stack_words = OS_TASK_STACK_STORAGE_WORDS_MAX;
    }

    task->sp = NULL;
    task->stack_base = g_task_stack_storage[tid];
    task->stack_words = stack_words;
    task->entry = cfg->entry;
    task->arg = cfg->arg;
    task->name = cfg->name;
    task->id = cfg->id;
    task->base_priority = os_clamp_priority(cfg->base_priority);
    task->current_priority = task->base_priority;
    task->max_activations = (cfg->max_activations == 0u) ? 1u : cfg->max_activations;
    task->sched_class = (uint8_t)cfg->sched_class;
    task->is_idle_task = cfg->is_idle_task;
    task->activation_count = 0u;
    task->context_needs_init = 1u;
    task->state = OS_DORMANT;
    task->event_set = 0u;
    task->wait_mask = 0u;

    if (cfg->is_idle_task != 0u)
    {
        g_idle_task_id = tid;
    }
}

/* ============================================================
 * ISR enter/exit
 * ============================================================ */
static void os_isr_enter(void)
{
    g_interrupt_nesting++;
}

static void os_isr_exit(void)
{
    if (g_interrupt_nesting > 0u)
    {
        g_interrupt_nesting--;
    }

    if (g_interrupt_nesting == 0u)
    {
        os_schedule();
    }
}

/* ============================================================
 * Scheduler policy helpers (Các hàm kiểm tra chính sách lập lịch)
 * ============================================================ */
static bool os_should_preempt(const TCB_t *current, const TCB_t *candidate)
{
    if ((current == NULL) || (candidate == NULL))
    {
        return false;
    }

    if (candidate->id == current->id)
    {
        return false;
    }

    /*
     * Task "Idle" không được xem là một nhiệm vụ thực tế của người dùng.
     * Nó chỉ dùng để ngâm CPU khi rảng rỗi.
     * Bất cứ khi nào có một ứng dụng (candidate) sẵn sàng chạy, hệ thống
     * bắt buộc phải đá Idle ra khỏi CPU ngay lập tức (dù ở mode Non-preemptive).
     */
    if (os_is_idle_task(current) && !os_is_idle_task(candidate))
    {
        return true;
    }

    /* 
     * Rule cơ bản: Nếu Task mới (candidate) có Priority NHỎ HƠN HOẶC BẰNG
     * Task đang chạy (current), thì chắc chắn không được phép chiếm quyền (preempt).
     */
    if (candidate->current_priority <= current->current_priority)
    {
        return false;
    }

    /* 
     * Nếu đoạn code xuống được dòng này, nghĩa là Task mới (candidate) CÓ PRIORITY CAO HƠN Task hiện tại.
     * Lúc này Quyết định chiếm quyền (Context Switch) sẽ tuỳ thuộc vào "Mode Lập Lịch" (Scheduler Mode).
     */
    switch (g_scheduler_mode)
    {
    case OS_SCHEDMODE_PRIORITY_FIFO:
    case OS_SCHEDMODE_NON_PREEMPTIVE:
        /*
         * NON-PREEMPTIVE (Không chiếm quyền): 
         * Dù Task mới có ưu tiên cao cỡ nào, nó vẫn phải XẾP HÀNG chờ Task đang chạy 
         * tự nguyện kết thúc (TerminateTask) hoặc tự nguyện xin chờ (WaitEvent).
         */
        return false;

    case OS_SCHEDMODE_FULL_PREEMPTIVE:
        /*
         * FULL PREEMPTIVE (Chiếm quyền hoàn toàn):
         * Thuật toán này rất mạnh bạo! Ngay khi một Task Priority cực cao vừa được kích hoạt 
         * (Ví dụ: do Alarm nổ ở ngắt SysTick), hàm này lập tức trả về TRUE.
         * Scheduler sẽ đình chỉ ngay lập tức Task đang thi hành, lưu trạng thái thanh ghi 
         * của nó vào Stack, và nhường toàn bộ CPU cho Task ưu tiên cao (Candidate) chạy liền.
         */
        return true;

    case OS_SCHEDMODE_MIXED:
        return (current->sched_class == (uint8_t)OS_TASK_SCHED_PREEMPTIVE);

    default:
        return false;
    }
}

/* ============================================================
 * Port helper cho SVC/PendSV
 * ============================================================ */
uint32_t *os_get_task_stack_ptr_for_switch(TCB_t *task)
{
    if (task == NULL)
    {
        return NULL;
    }

    if ((task->context_needs_init != 0u) || (task->sp == NULL))
    {
        task->sp = os_task_stack_init(task->entry, task->arg, os_task_stack_top(task));
        task->context_needs_init = 0u;
    }

    return task->sp;
}

/* ============================================================
 * Dispatch / schedule
 * ============================================================ */
void os_dispatch(void)
{
    if (g_next == NULL)
    {
        return;
    }

    os_trigger_pendsv();
}

void os_schedule(void)
{
    bool need_dispatch = false;
    uint32_t primask = os_irq_save();
    TCB_t *current = (TCB_t *)g_current;

    /*
     * Không lập lịch khi:
     * - vẫn còn đang nằm trong ISR nesting
     * - hoặc một context switch khác đã pending trong g_next
     */
    if ((g_interrupt_nesting > 0u) || (g_next != NULL))
    {
        os_irq_restore(primask);
        return;
    }

    if (current == NULL)
    {
        os_irq_restore(primask);
        return;
    }

    /*
     * Nhánh A:
     * current không còn RUNNING nữa.
     * Đây là dispatch point "chắc chắn được phép switch":
     * - task vừa TerminateTask()
     * - task vừa được re-queue vì multi-activation
     * - hoặc đang bootstrap lần đầu
     */
    if (current->state != OS_RUNNING)
    {
        uint8_t next_id = os_ready_pop_highest();

        if ((next_id == OS_INVALID_TASK_ID) && !os_is_idle_task(current))
        {
            next_id = g_idle_task_id;
        }

        if (next_id != OS_INVALID_TASK_ID)
        {
            TCB_t *next = &tcb[next_id];
            next->state = OS_RUNNING;
            g_next = next;
            need_dispatch = true;
        }
    }
    else
    {
        /*
         * Nhánh B:
         * current vẫn đang RUNNING, nên việc switch hay không phụ thuộc vào
         * policy preemption hiện hành.
         */
        uint8_t peek_id = os_ready_peek_highest();
        if (peek_id != OS_INVALID_TASK_ID)
        {
            TCB_t *candidate = &tcb[peek_id];
            if (os_should_preempt(current, candidate))
            {
                current->state = OS_READY;
                os_ready_add_front(current->id, current->current_priority);

                peek_id = os_ready_pop_highest();
                if (peek_id != OS_INVALID_TASK_ID)
                {
                    TCB_t *next = &tcb[peek_id];
                    next->state = OS_RUNNING;
                    g_next = next;
                    need_dispatch = true;
                }
            }
        }
    }

    os_irq_restore(primask);

    if (need_dispatch)
    {
        os_dispatch();
    }
}

/* ============================================================
 * Public kernel API
 * ============================================================ */
void ActivateTask(uint8_t tid)
{
    bool run_scheduler = false;

    if ((tid >= OS_MAX_TASKS) || (tid == g_idle_task_id))
    {
        return;
    }

    uint32_t primask = os_irq_save();
    TCB_t *task = &tcb[tid];

    if (task->activation_count >= task->max_activations)
    {
        os_irq_restore(primask);
        return;
    }

    task->activation_count++;

    /*
     * Chỉ khi task đang DORMANT mới cần dựng lại bối cảnh từ đầu và enqueue.
     * Nếu task đang READY/RUNNING thì activation_count tăng nhưng task không
     * bị enqueue trùng; lần chạy hiện tại kết thúc xong nó sẽ tự re-queue.
     */
    if (task->state == OS_DORMANT)
    {
        task->current_priority = task->base_priority;
        task->context_needs_init = 1u;
        task->sp = NULL;
        task->state = OS_READY;
        // os_ready_add(task->id, task->current_priority);

        os_ready_enqueue_tail(task->current_priority, task->id);
    }

    if (g_interrupt_nesting == 0u)
    {
        run_scheduler = true;
    }

    os_irq_restore(primask);

    if (run_scheduler)
    {
        os_schedule();
    }
}

void TerminateTask(void)
{
    uint32_t primask = os_irq_save();
    TCB_t *current = (TCB_t *)g_current;

    if (current != NULL)
    {
        if (current->activation_count > 0u)
        {
            current->activation_count--;
        }

        current->current_priority = current->base_priority;
        current->context_needs_init = 1u;
        current->sp = NULL;

        if (current->activation_count > 0u)
        {
            current->state = OS_READY;
            os_ready_add(current->id, current->current_priority);
        }
        else
        {
            current->state = OS_DORMANT;
        }
    }

    os_irq_restore(primask);
    os_schedule();

    /*
     * Không được quay lại thân task vừa terminate.
     * Nếu CPU chưa switch ngay, nó sẽ đứng tại vòng NOP này.
     */
    for (;;)
    {
        __NOP();
    }
}

void WaitEvent(EventMaskType mask)
{
    if (mask == 0u)
    {
        return;
    }

    uint32_t primask = os_irq_save();
    TCB_t *current = (TCB_t *)g_current;

    if ((current == NULL) || os_is_idle_task(current))
    {
        os_irq_restore(primask);
        return;
    }

    if ((current->event_set & mask) != 0u)
    {
        os_irq_restore(primask);
        return;
    }

    current->wait_mask = mask;
    current->state = OS_WAITING;

    os_irq_restore(primask);
    os_schedule();
}

void SetEvent(uint8_t tid, EventMaskType mask)
{
    bool run_scheduler = false;

    if ((mask == 0u) || (tid >= OS_MAX_TASKS) || (tid == g_idle_task_id))
    {
        return;
    }

    uint32_t primask = os_irq_save();
    TCB_t *task = &tcb[tid];

    task->event_set |= mask;

    if ((task->state == OS_WAITING) && ((task->event_set & task->wait_mask) != 0u))
    {
        task->wait_mask = 0u;
        task->state = OS_READY;
        os_ready_add(task->id, task->current_priority);

        if (g_interrupt_nesting == 0u)
        {
            run_scheduler = true;
        }
    }

    os_irq_restore(primask);

    if (run_scheduler)
    {
        os_schedule();
    }
}

void ClearEvent(EventMaskType mask)
{
    if (mask == 0u)
    {
        return;
    }

    uint32_t primask = os_irq_save();
    TCB_t *current = (TCB_t *)g_current;

    if ((current != NULL) && !os_is_idle_task(current))
    {
        current->event_set &= ~mask;
    }

    os_irq_restore(primask);
}

void GetEvent(uint8_t tid, EventMaskType *mask)
{
    if (mask == NULL)
    {
        return;
    }

    uint32_t primask = os_irq_save();

    if (tid < OS_MAX_TASKS)
    {
        *mask = tcb[tid].event_set;
    }
    else
    {
        *mask = 0u;
    }

    os_irq_restore(primask);
}

void SetRelAlarm(uint8_t aid, uint32_t delay_ms, uint32_t cycle_ms, uint8_t target_tid)
{
    if ((aid >= OS_MAX_ALARMS) || (target_tid >= OS_MAX_TASKS) ||
        (target_tid == g_idle_task_id))
    {
        return;
    }

    uint32_t primask = os_irq_save();
    OsAlarm_t *alarm = &alarm_tbl[aid];

    alarm->active = 1u;
    alarm->target_task = target_tid;
    alarm->remain_ticks = ms_to_ticks((delay_ms == 0u) ? 1u : delay_ms);
    alarm->cycle_ticks = ms_to_ticks(cycle_ms);

    os_irq_restore(primask);
}

void CancelAlarm(uint8_t aid)
{
    if (aid >= OS_MAX_ALARMS)
    {
        return;
    }

    uint32_t primask = os_irq_save();
    alarm_tbl[aid].active = 0u;
    alarm_tbl[aid].target_task = OS_INVALID_TASK_ID;
    alarm_tbl[aid].remain_ticks = 0u;
    alarm_tbl[aid].cycle_ticks = 0u;
    os_irq_restore(primask);
}

void os_on_tick(void)
{
    os_isr_enter();
    g_tick_count++;

    /*
     * Bước 1: xử lý alarm truyền thống.
     */
    for (uint8_t i = 0u; i < OS_MAX_ALARMS; ++i)
    {
        OsAlarm_t *alarm = &alarm_tbl[i];
        if (alarm->active == 0u)
        {
            continue;
        }

        if (alarm->remain_ticks > 0u)
        {
            alarm->remain_ticks--;
        }

        if (alarm->remain_ticks == 0u)
        {
            ActivateTask(alarm->target_task);

            if (alarm->cycle_ticks > 0u)
            {
                alarm->remain_ticks = alarm->cycle_ticks;
            }
            else
            {
                alarm->active = 0u;
            }
        }
    }

    /*
     * Bước 2: xử lý layer time-triggered.
     * ScheduleTable sẽ tự bơm activation vào ready queue khi đến expiry point.
     */
    os_sched_table_on_tick();

    os_isr_exit();
}

void os_tick_handler(void)
{
    os_on_tick();
}

/* ============================================================
 * Bootstrap kernel
 * ============================================================ */
void OS_Init(void)
{
    uint32_t primask = os_irq_save();

    g_current = NULL;
    g_next = NULL;
    g_interrupt_nesting = 0u;
    g_tick_count = 0u;
    g_idle_task_id = OS_INVALID_TASK_ID;

    os_port_init();
    os_ready_queue_reset();
    os_alarm_reset_all();
    os_sched_table_reset_all();

    /*
     * Dựng toàn bộ TCB từ bảng config tĩnh.
     */
    for (uint8_t tid = 0u; tid < OS_MAX_TASKS; ++tid)
    {
        os_task_setup_from_cfg(tid);
    }

    /*
     * Nạp các task autostart.
     * Thông thường chỉ có Task_Init và Task_Idle là autostart, còn lại để
     * alarm/schedule table kích hoạt sau.
     */
    for (uint8_t tid = 0u; tid < OS_MAX_TASKS; ++tid)
    {
        if (g_os_task_cfg[tid].autostart != 0u)
        {
            tcb[tid].activation_count = 1u;
            tcb[tid].state = OS_READY;
            os_ready_add(tid, tcb[tid].current_priority);
        }
    }

    /*
     * Nếu bảng config quên autostart idle task, ta cưỡng bức đưa idle vào queue
     * để scheduler luôn có ngữ cảnh fallback.
     */
    if ((g_idle_task_id != OS_INVALID_TASK_ID) && !task_queued[g_idle_task_id] &&
        (tcb[g_idle_task_id].state == OS_DORMANT))
    {
        tcb[g_idle_task_id].activation_count = 1u;
        tcb[g_idle_task_id].state = OS_READY;
        os_ready_add(g_idle_task_id, tcb[g_idle_task_id].current_priority);
    }

    /*
     * Chọn task đầu tiên có priority cao nhất để bootstrap qua SVC.
     */
    uint8_t first_id = os_ready_pop_highest();
    if (first_id == OS_INVALID_TASK_ID)
    {
        first_id = g_idle_task_id;
    }

    if (first_id == OS_INVALID_TASK_ID)
    {
        /*
         * Tình huống này chỉ xảy ra nếu config bị lỗi nghiêm trọng:
         * không có autostart task nào và cũng không có idle task hợp lệ.
         * Ta vẫn chọn task 0 như một guard cuối cùng để firmware không dùng
         * con trỏ NULL trong g_current.
         */
        first_id = 0u;
    }

    g_current = &tcb[first_id];
    ((TCB_t *)g_current)->state = OS_RUNNING;

    /*
     * Nạp các nguồn activation tĩnh.
     */
    os_alarm_autostart_all();
    os_sched_table_autostart_all();

    os_irq_restore(primask);
}

void OS_Start(void)
{
    __ASM volatile("svc 0");
}

/* ============================================================
 * Helper phục vụ trace/debug/test
 * ============================================================ */
uint32_t OS_GetTickCount(void)
{
    return g_tick_count;
}

OsSchedulerMode_e OS_GetSchedulerMode(void)
{
    return g_scheduler_mode;
}

const char *OS_GetSchedulerModeName(void)
{
    switch (g_scheduler_mode)
    {
    case OS_SCHEDMODE_PRIORITY_FIFO:
        return "PRIORITY_FIFO";
    case OS_SCHEDMODE_FULL_PREEMPTIVE:
        return "FULL_PREEMPTIVE";
    case OS_SCHEDMODE_NON_PREEMPTIVE:
        return "NON_PREEMPTIVE";
    case OS_SCHEDMODE_MIXED:
        return "MIXED";
    default:
        return "UNKNOWN_SCHEDULER_MODE";
    }
}

const char *OS_GetTaskName(uint8_t tid)
{
    if (tid >= OS_MAX_TASKS)
    {
        return "INVALID_TASK";
    }

    return (tcb[tid].name != NULL) ? tcb[tid].name : "UNNAMED_TASK";
}
