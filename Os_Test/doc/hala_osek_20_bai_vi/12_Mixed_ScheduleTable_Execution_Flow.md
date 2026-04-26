# Mixed Scheduling + ScheduleTable - Luồng Chạy Step By Step

Tài liệu này mô tả chi tiết 2 lớp lập lịch đang được cấu hình trong project `Os_Test`:

1. `Mixed Scheduling`: một số task có thể bị chiếm quyền, một số task không bị chiếm quyền.
2. `ScheduleTable`: lập lịch theo thời gian, đến offset nào thì kích hoạt task tương ứng.

Mục tiêu của bài test hiện tại:

- chứng minh task preemptive đang chạy có thể bị task priority cao hơn cắt ngang
- chứng minh task non-preemptive đang chạy không bị cắt ngang, dù task mới có priority cao hơn
- chứng minh ScheduleTable kích hoạt task theo offset thời gian và lặp lại theo chu kỳ

---

## 1. Bản Đồ Source Cần Nhớ

| Vai trò | File | Hàm / đối tượng quan trọng |
|---|---|---|
| Entry firmware | `main.c` | `main()` |
| Kernel init | `OS/src/os_kernel.c` | `OS_Init()`, `OS_Start()` |
| Ready queue | `OS/src/os_kernel.c` | `os_ready_enqueue_tail()`, `os_ready_add_front()`, `os_ready_peek_highest()`, `os_ready_pop_highest()` |
| Scheduler core | `OS/src/os_kernel.c` | `os_schedule()`, `os_should_preempt()`, `os_dispatch()` |
| Task API | `OS/src/os_kernel.c` | `ActivateTask()`, `TerminateTask()` |
| Tick handler | `OS/src/os_kernel.c` | `os_on_tick()` |
| ScheduleTable module | `OS/src/os_sched_table.c` | `StartScheduleTableRel()`, `os_sched_table_on_tick()`, `os_sched_table_fire_due_points()`, `os_sched_table_finish_cycle()` |
| Cấu hình app | `Config/os_app_cfg.c` | `g_os_task_cfg`, `g_os_alarm_cfg`, `g_os_schedule_table_cfg` |
| Thân task test | `app/App_Task.c` | `Task_Mixed_PreLow()`, `Task_Mixed_NonPreLow()`, `Task_Mixed_High()`, `Task_ScheduleTable_Low()`, `Task_ScheduleTable_High()` |

---

## 2. Cấu Hình Hiện Tại

### 2.1. Scheduler mode

Trong `Config/os_config.h`:

```c
#ifndef OS_SCHED_MODE_DEFAULT
#define OS_SCHED_MODE_DEFAULT OS_SCHEDMODE_MIXED
#endif
```

Nghĩa là firmware hiện tại chạy với policy `MIXED`.

### 2.2. Task cho Mixed Scheduling

Trong `Config/os_app_cfg.c`, các task quan trọng là:

```c
TASK_MIXED_PRE_LOW
  priority    = OS_PRIO_LOW
  sched_class = OS_TASK_SCHED_PREEMPTIVE

TASK_MIXED_NONPRE_LOW
  priority    = OS_PRIO_LOW
  sched_class = OS_TASK_SCHED_NON_PREEMPTIVE

TASK_MIXED_HIGH
  priority    = OS_PRIO_CRITICAL
  sched_class = OS_TASK_SCHED_PREEMPTIVE
```

Ý nghĩa:

- `TASK_MIXED_PRE_LOW`: khi đang chạy thì được phép bị task khác cắt ngang nếu task đó có priority cao hơn.
- `TASK_MIXED_NONPRE_LOW`: khi đang chạy thì không bị cắt ngang, dù task khác có priority cao hơn.
- `TASK_MIXED_HIGH`: task ưu tiên cao dùng để kiểm tra 2 trường hợp ở trên.

### 2.3. Alarm cho Mixed Scheduling

```c
ALARM_MIXED_PRE_LOW
  delay_ms    = 20
  target_task = TASK_MIXED_PRE_LOW

ALARM_MIXED_PRE_HIGH
  delay_ms    = 40
  target_task = TASK_MIXED_HIGH

ALARM_MIXED_NONPRE_LOW
  delay_ms    = 120
  target_task = TASK_MIXED_NONPRE_LOW

ALARM_MIXED_NONPRE_HIGH
  delay_ms    = 140
  target_task = TASK_MIXED_HIGH
```

Ý nghĩa:

- tick 20: kích hoạt low task preemptive
- tick 40: kích hoạt high task để thử cắt ngang low task preemptive
- tick 120: kích hoạt low task non-preemptive
- tick 140: kích hoạt high task để thử cắt ngang low task non-preemptive

### 2.4. ScheduleTable

Trong `Config/os_app_cfg.c`:

```c
g_schedule_table_main_points[] = {
    { .offset_ticks = 0,  .task_id = TASK_SCHTBL_LOW  },
    { .offset_ticks = 10, .task_id = TASK_SCHTBL_HIGH }
};

g_os_schedule_table_cfg[SCHEDULE_TABLE_MAIN] = {
    .autostart      = 1,
    .repeating      = 1,
    .start_delay_ms = 220,
    .duration_ms    = 40,
    .expiry_points  = g_schedule_table_main_points,
    .num_expiry_points = 2
};
```

Ý nghĩa:

- tick 220: ScheduleTable bắt đầu chu kỳ đầu tiên, offset 0, activate `TASK_SCHTBL_LOW`
- tick 230: offset 10, activate `TASK_SCHTBL_HIGH`
- tick 260: hết chu kỳ 40 tick, quay về offset 0, activate `TASK_SCHTBL_LOW`
- tick 270: offset 10, activate `TASK_SCHTBL_HIGH`
- sau đó tiếp tục lặp lại: 300/310, 340/350, ...

---

## 3. Luồng Boot Từ `main()` Đến Task Đầu Tiên

### Step 1 - `main()` chạy

File: `main.c`

```c
SystemInit();
OS_Init();
OS_Start();
```

`main()` không chứa policy scheduler. Nó chỉ làm 3 việc:

1. khởi tạo hệ thống CMSIS bằng `SystemInit()`
2. khởi tạo kernel bằng `OS_Init()`
3. chuyển CPU sang OS bằng `OS_Start()`

### Step 2 - `OS_Init()` reset kernel

File: `OS/src/os_kernel.c`

`OS_Init()` làm các việc sau:

1. `g_current = NULL`
2. `g_next = NULL`
3. `g_interrupt_nesting = 0`
4. `g_tick_count = 0`
5. gọi `os_port_init()`
6. gọi `os_ready_queue_reset()`
7. gọi `os_alarm_reset_all()`
8. gọi `os_sched_table_reset_all()`

Tại thời điểm này, OS chưa chạy task nào.

### Step 3 - Tạo TCB từ config

`OS_Init()` duyệt `g_os_task_cfg`:

```c
for (uint8_t tid = 0u; tid < OS_MAX_TASKS; ++tid) {
    os_task_setup_from_cfg(tid);
}
```

Mỗi task được nạp vào `tcb[tid]`:

- entry function
- priority
- `sched_class`
- vùng stack
- state ban đầu `OS_DORMANT`
- activation count ban đầu `0`

### Step 4 - Nạp autostart task vào ready queue

Hiện tại có 2 task autostart:

- `TASK_INIT`
- `TASK_IDLE`

`OS_Init()` đặt:

```c
tcb[tid].activation_count = 1;
tcb[tid].state = OS_READY;
os_ready_add(tid, tcb[tid].current_priority);
```

Ready queue lúc này có:

```text
Priority CRITICAL: Task_Init
Priority IDLE:     Task_Idle
```

### Step 5 - Chọn task đầu tiên

`OS_Init()` gọi:

```c
first_id = os_ready_pop_highest();
g_current = &tcb[first_id];
g_current->state = OS_RUNNING;
```

Vì `Task_Init` có priority `CRITICAL`, nó được chọn trước `Task_Idle`.

### Step 6 - Autostart alarm và ScheduleTable

Cuối `OS_Init()`:

```c
os_alarm_autostart_all();
os_sched_table_autostart_all();
```

Kết quả:

- alarm bắt đầu đếm lùi cho tick 20, 40, 120, 140
- ScheduleTable được active và đặt countdown start delay 220 tick

### Step 7 - `OS_Start()` vào SVC

```c
__ASM volatile("svc 0");
```

SVC handler trong assembly sẽ:

1. lấy `g_current`
2. init stack frame nếu cần
3. set PSP
4. exception return để nhảy vào entry của `Task_Init`

Từ đây, CPU không quay lại luồng `main()` bình thường nữa.

---

## 4. Ready Queue - Nền Tảng Của Scheduler

Ready queue nằm trong `OS/src/os_kernel.c`.

Project này dùng nhiều queue theo priority:

```text
ready_queues[0] -> priority IDLE
ready_queues[1] -> priority LOW
ready_queues[2] -> priority MEDIUM
ready_queues[3] -> priority HIGH
ready_queues[4] -> priority CRITICAL
```

Giá trị priority càng lớn thì mức ưu tiên càng cao.

### 4.1. Thêm task vào queue

Task mới được activate sẽ được thêm vào cuối queue:

```c
os_ready_enqueue_tail(task->current_priority, task->id);
```

Điều này giữ FIFO nếu nhiều task có cùng priority.

### 4.2. Chọn task cao nhất

`os_ready_pop_highest()` quét từ priority cao xuống thấp:

```text
CRITICAL -> HIGH -> MEDIUM -> LOW -> IDLE
```

Gặp queue nào có task thì pop task ở đầu queue đó.

### 4.3. Khi task đang chạy bị preempt

Task đang chạy bị đưa lại vào đầu queue:

```c
os_ready_add_front(current->id, current->current_priority);
```

Lý do: task bị cắt ngang chưa xong việc, nên sau khi task cao hơn kết thúc thì task cũ nên được resume sớm.

---

## 5. Mixed Scheduling - Thuật Toán Step By Step

### 5.1. Hàm trung tâm: `os_schedule()`

`os_schedule()` có 2 nhánh lớn.

#### Nhánh A - current không còn `OS_RUNNING`

Điều kiện:

```c
if (current->state != OS_RUNNING)
```

Trường hợp này xảy ra khi:

- task vừa gọi `TerminateTask()`
- task bị đưa về `OS_DORMANT`
- task đã nhường CPU tại một dispatch point

Xử lý:

1. pop task ready có priority cao nhất
2. set task đó thành `OS_RUNNING`
3. gán task đó vào `g_next`
4. trigger PendSV để đổi ngữ cảnh

Nhánh này không cần hỏi "có được preempt không", vì task hiện tại đã không còn chạy nữa.

#### Nhánh B - current vẫn đang `OS_RUNNING`

Điều kiện:

```c
else {
    uint8_t peek_id = os_ready_peek_highest();
    ...
}
```

Trường hợp này xảy ra khi:

- current vẫn đang chạy
- SysTick, Alarm hoặc ScheduleTable vừa activate thêm task mới
- scheduler cần quyết định có cắt current hay không

Xử lý:

1. peek task ready có priority cao nhất, chưa pop ngay
2. gọi `os_should_preempt(current, candidate)`
3. nếu kết quả là `false`: không làm gì, current tiếp tục chạy
4. nếu kết quả là `true`:
   - current chuyển về `OS_READY`
   - đưa current về đầu ready queue
   - pop candidate ra khỏi ready queue
   - candidate chuyển thành `OS_RUNNING`
   - `g_next = candidate`
   - trigger PendSV để đổi ngữ cảnh

### 5.2. Hàm quyết định: `os_should_preempt()`

Pseudo-code gần với source hiện tại:

```c
bool os_should_preempt(current, candidate)
{
    if (current == NULL || candidate == NULL) return false;
    if (candidate->id == current->id) return false;

    if (current là idle và candidate không phải idle) return true;

    if (candidate->current_priority <= current->current_priority) {
        return false;
    }

    switch (g_scheduler_mode) {
    case OS_SCHEDMODE_PRIORITY_FIFO:
    case OS_SCHEDMODE_NON_PREEMPTIVE:
        return false;

    case OS_SCHEDMODE_FULL_PREEMPTIVE:
        return true;

    case OS_SCHEDMODE_MIXED:
        return current->sched_class == OS_TASK_SCHED_PREEMPTIVE;
    }
}
```

Điểm cốt lõi của Mixed:

```c
return (current->sched_class == OS_TASK_SCHED_PREEMPTIVE);
```

Nó kiểm tra **current task**, không phải candidate.

Nghĩa là:

- nếu current là preemptive: task priority cao hơn được cắt ngang
- nếu current là non-preemptive: task priority cao hơn phải chờ current kết thúc

---

## 6. Mixed Case 1 - Preemptive Low Bị High Cắt Ngang

### Mục tiêu

Chứng minh `TASK_MIXED_PRE_LOW` có `sched_class = OS_TASK_SCHED_PREEMPTIVE`, nên khi `TASK_MIXED_HIGH` được activate tại tick 40, high task được cắt ngang.

### Timeline

```text
T=20  TASK_MIXED_PRE_LOW  START
T=40  TASK_MIXED_HIGH     RUN while preemptible low is paused
T=80  TASK_MIXED_PRE_LOW  END
```

### Step by step

#### Tick 20 - Alarm low hết hạn

1. SysTick vào `os_on_tick()`.
2. `g_tick_count++`, lúc này tick là 20.
3. Alarm `ALARM_MIXED_PRE_LOW` hết hạn.
4. Kernel gọi:

```c
ActivateTask(TASK_MIXED_PRE_LOW);
```

5. `TASK_MIXED_PRE_LOW` từ `OS_DORMANT` chuyển sang `OS_READY`.
6. Task được thêm vào ready queue priority `LOW`.
7. Lúc này current thường là `Task_Idle`.
8. Khi thoát ISR, `os_isr_exit()` gọi `os_schedule()`.
9. `os_should_preempt(Idle, PreLow)` trả true vì Idle phải nhường CPU cho app task.
10. `g_next = Task_Mixed_PreLow`.
11. PendSV switch sang `Task_Mixed_PreLow`.

#### Task PreLow bắt đầu chạy

Trong `Task_Mixed_PreLow()`:

```c
app_log_line("[MIXED_PRE_LOW] START preemptible window");
app_busy_wait_ticks(60u);
app_log_line("[MIXED_PRE_LOW] END after high preempted it");
TerminateTask();
```

Task này sẽ busy wait từ tick 20 đến tick 80 nếu không bị cắt ngang.

#### Tick 40 - Alarm high hết hạn

1. `Task_Mixed_PreLow` đang ở trong `app_busy_wait_ticks(60u)`.
2. SysTick vào `os_on_tick()`.
3. Alarm `ALARM_MIXED_PRE_HIGH` hết hạn.
4. Kernel gọi:

```c
ActivateTask(TASK_MIXED_HIGH);
```

5. `TASK_MIXED_HIGH` vào ready queue priority `CRITICAL`.
6. Thoát ISR, `os_schedule()` chạy nhánh B vì current vẫn `OS_RUNNING`.
7. Scheduler peek candidate cao nhất:

```text
candidate = TASK_MIXED_HIGH
current   = TASK_MIXED_PRE_LOW
```

8. Gọi `os_should_preempt(current, candidate)`.
9. So priority:

```text
candidate priority = CRITICAL
current priority   = LOW
```

Candidate cao hơn.

10. Mode là `MIXED`, nên kiểm tra current:

```text
current->sched_class == OS_TASK_SCHED_PREEMPTIVE
```

Điều kiện đúng, nên return true.

11. `os_schedule()` xử lý:

```text
current PreLow -> OS_READY
PreLow được đưa vào đầu ready queue LOW
High được pop khỏi ready queue CRITICAL
High -> OS_RUNNING
g_next = High
```

12. `os_dispatch()` trigger PendSV.
13. PendSV save context của PreLow, restore context của High.
14. CPU nhảy vào `Task_Mixed_High()`.

#### High chạy tại tick 40

`Task_Mixed_High()` in:

```text
[T=40] [MIXED_HIGH] RUN while preemptible low is paused
```

Sau đó High gọi `TerminateTask()`.

#### High kết thúc, PreLow được resume

1. High gọi `TerminateTask()`.
2. High chuyển về `OS_DORMANT`.
3. `os_schedule()` vào nhánh A vì current không còn running.
4. Ready queue còn PreLow ở priority LOW.
5. Scheduler pop PreLow.
6. `g_next = PreLow`.
7. PendSV restore context cũ của PreLow.
8. CPU quay lại đúng vị trí trong `app_busy_wait_ticks(60u)`.

#### Tick 80 - PreLow kết thúc

PreLow busy wait đủ 60 tick tính từ tick 20. Tại tick 80, nó in:

```text
[T=80] [MIXED_PRE_LOW] END after high preempted it
```

Sau đó PreLow gọi `TerminateTask()` và chuyển về `OS_DORMANT`.

---

## 7. Mixed Case 2 - NonPre Low Không Bị High Cắt Ngang

### Mục tiêu

Chứng minh `TASK_MIXED_NONPRE_LOW` có `sched_class = OS_TASK_SCHED_NON_PREEMPTIVE`, nên khi `TASK_MIXED_HIGH` được activate tại tick 140, high task phải chờ đến tick 180.

### Timeline

```text
T=120  TASK_MIXED_NONPRE_LOW  START
T=140  TASK_MIXED_HIGH        READY, nhưng chưa chạy
T=180  TASK_MIXED_NONPRE_LOW  END
T=180  TASK_MIXED_HIGH        RUN after nonpreemptible low finished
```

### Step by step

#### Tick 120 - Alarm nonpre low hết hạn

1. SysTick vào `os_on_tick()`.
2. Alarm `ALARM_MIXED_NONPRE_LOW` hết hạn.
3. Kernel gọi:

```c
ActivateTask(TASK_MIXED_NONPRE_LOW);
```

4. Task vào ready queue priority `LOW`.
5. Current lúc này là Idle.
6. Idle bị preempt để app task chạy.
7. CPU nhảy vào `Task_Mixed_NonPreLow()`.

Task in:

```text
[T=120] [MIXED_NONPRE_LOW] START nonpreemptible window
```

Sau đó busy wait 60 tick:

```c
app_busy_wait_ticks(60u);
```

#### Tick 140 - Alarm high hết hạn

1. `Task_Mixed_NonPreLow` đang busy wait.
2. SysTick vào `os_on_tick()`.
3. Alarm `ALARM_MIXED_NONPRE_HIGH` hết hạn.
4. Kernel gọi:

```c
ActivateTask(TASK_MIXED_HIGH);
```

5. `TASK_MIXED_HIGH` vào ready queue priority `CRITICAL`.
6. Thoát ISR, `os_schedule()` vào nhánh B vì current vẫn `OS_RUNNING`.
7. Candidate là High, current là NonPreLow.
8. Gọi `os_should_preempt(current, candidate)`.
9. Candidate priority cao hơn current:

```text
CRITICAL > LOW
```

10. Mode là `MIXED`, nên kiểm tra current:

```text
current->sched_class == OS_TASK_SCHED_PREEMPTIVE
```

Điều kiện sai, vì current là `OS_TASK_SCHED_NON_PREEMPTIVE`.

11. `os_should_preempt()` return false.
12. `os_schedule()` không set `g_next`, không trigger PendSV.
13. CPU return khỏi SysTick và tiếp tục chạy `Task_Mixed_NonPreLow`.

Kết quả: High đã `READY`, nhưng không được chạy tại tick 140.

#### Tick 180 - NonPreLow kết thúc

Sau 60 tick busy wait, NonPreLow in:

```text
[T=180] [MIXED_NONPRE_LOW] END before queued high
```

Sau đó gọi `TerminateTask()`.

#### Dispatch point sau `TerminateTask()`

1. NonPreLow chuyển về `OS_DORMANT`.
2. `os_schedule()` vào nhánh A vì current không còn running.
3. Scheduler pop ready queue cao nhất.
4. High đang READY từ tick 140, priority `CRITICAL`, nên được chọn.
5. `g_next = Task_Mixed_High`.
6. PendSV switch sang High.

High in:

```text
[T=180] [MIXED_HIGH] RUN after nonpreemptible low finished
```

Đây là bằng chứng non-preemptive: High không chạy tại tick 140, mà chờ đến tick 180.

---

## 8. ScheduleTable - Thuật Toán Step By Step

### 8.1. Các struct liên quan

Trong `OS/inc/os_types.h`:

```c
typedef struct {
    uint32_t  offset_ticks;
    uint8_t   task_id;
} OsScheduleTableExpiryPointCfg_t;
```

Mỗi expiry point chỉ rõ:

- offset tính từ đầu chu kỳ
- task cần activate tại offset đó

Runtime state:

```c
typedef struct {
    uint8_t   active;
    uint8_t   repeating;
    uint8_t   current_expiry_idx;
    uint32_t  start_delay_ticks;
    uint32_t  elapsed_ticks;
    uint32_t  duration_ticks;
} OsScheduleTable_t;
```

Ý nghĩa:

- `active`: table đang chạy hay dừng
- `repeating`: hết chu kỳ thì lặp lại hay stop
- `current_expiry_idx`: expiry point tiếp theo cần xử lý
- `start_delay_ticks`: đếm lùi trước khi table bắt đầu chu kỳ đầu tiên
- `elapsed_ticks`: offset hiện tại trong chu kỳ
- `duration_ticks`: độ dài chu kỳ

### 8.2. Autostart ScheduleTable

Trong `OS_Init()`:

```c
os_sched_table_autostart_all();
```

Hàm này duyệt `g_os_schedule_table_cfg`:

```c
if (cfg->autostart != 0u) {
    StartScheduleTableRel(sid, cfg->start_delay_ms);
}
```

Với config hiện tại:

```text
autostart      = 1
start_delay_ms = 220
duration_ms    = 40
repeating      = 1
```

Nên sau `OS_Init()`, ScheduleTable active nhưng chưa fire ngay. Nó sẽ đếm lùi 220 tick.

### 8.3. Mỗi tick, ScheduleTable được gọi

Trong `os_on_tick()`:

```c
os_sched_table_on_tick();
```

Hàm này chạy sau alarm.

### 8.4. Giai đoạn start delay

Trong `os_sched_table_on_tick()`:

```c
if (rt->start_delay_ticks > 0u) {
    rt->start_delay_ticks--;
    if (rt->start_delay_ticks == 0u) {
        rt->elapsed_ticks = 0u;
        rt->current_expiry_idx = 0u;
        os_sched_table_fire_due_points(sid);
        os_sched_table_finish_cycle(sid);
    }
    continue;
}
```

Từ tick 1 đến tick 219:

- `start_delay_ticks` giảm dần
- chưa activate task nào từ ScheduleTable

Tại tick 220:

- `start_delay_ticks` về 0
- `elapsed_ticks = 0`
- gọi `os_sched_table_fire_due_points()`

### 8.5. Fire expiry point tại offset 0

`os_sched_table_fire_due_points()` xem expiry point hiện tại:

```c
point = &cfg->expiry_points[rt->current_expiry_idx];
if (point->offset_ticks > rt->elapsed_ticks) break;
ActivateTask(point->task_id);
rt->current_expiry_idx++;
```

Tại tick 220:

```text
elapsed_ticks = 0
expiry point 0 offset = 0
```

Điều kiện `offset <= elapsed` đúng, nên:

```c
ActivateTask(TASK_SCHTBL_LOW);
```

Sau khi thoát ISR, scheduler cho `Task_ScheduleTable_Low()` chạy. Log:

```text
[T=220] [SCHTBL_LOW] RUN offset=0 priority=LOW
```

### 8.6. Fire expiry point tại offset 10

Sau khi table đã qua start delay, mỗi tick:

```c
rt->elapsed_ticks++;
os_sched_table_fire_due_points(sid);
os_sched_table_finish_cycle(sid);
```

Tại tick 230:

```text
elapsed_ticks = 10
expiry point 1 offset = 10
```

Nên:

```c
ActivateTask(TASK_SCHTBL_HIGH);
```

Log:

```text
[T=230] [SCHTBL_HIGH] RUN offset=10 priority=HIGH
```

### 8.7. Hết chu kỳ và lặp lại

ScheduleTable có:

```text
duration_ticks = 40
repeating = 1
```

Tại tick 260, `elapsed_ticks` đạt 40.

`os_sched_table_finish_cycle()` thấy:

```c
if (rt->repeating != 0u) {
    rt->elapsed_ticks = 0u;
    rt->current_expiry_idx = 0u;
    os_sched_table_fire_due_points(sid);
}
```

Nó reset chu kỳ và fire ngay offset 0 của chu kỳ mới.

Log:

```text
[T=260] [SCHTBL_LOW] RUN offset=0 priority=LOW
```

Sau đó tick 270:

```text
[T=270] [SCHTBL_HIGH] RUN offset=10 priority=HIGH
```

---

## 9. Quan Hệ Giữa ScheduleTable Và Scheduler Core

Điểm rất quan trọng:

```text
ScheduleTable không trực tiếp chọn task chạy.
ScheduleTable chỉ gọi ActivateTask().
Task có chạy ngay hay không vẫn do os_schedule() quyết định.
```

Flow thực tế:

```text
SysTick
  -> os_on_tick()
    -> os_sched_table_on_tick()
      -> os_sched_table_fire_due_points()
        -> ActivateTask(TASK_X)
  -> os_isr_exit()
    -> os_schedule()
      -> os_should_preempt()
      -> os_dispatch()
        -> PendSV context switch
```

Vì vậy ScheduleTable là lớp "time-triggered activation", còn `os_schedule()` vẫn là lớp quyết định dispatch CPU.

---

## 10. Timeline Tổng Hợp Của Firmware Hiện Tại

```text
T=0
  Task_Init chạy
  In boot banner: PROFILE=MIXED_SCHEDULE_TABLE MODE=MIXED
  Task_Init TerminateTask()
  Idle chạy

T=20
  Alarm_Mixed_PreLow hết hạn
  ActivateTask(TASK_MIXED_PRE_LOW)
  Idle bị preempt
  Task_Mixed_PreLow chạy

T=40
  Alarm_Mixed_PreHigh hết hạn
  ActivateTask(TASK_MIXED_HIGH)
  Current = Task_Mixed_PreLow, sched_class = PREEMPTIVE
  os_should_preempt() return true
  Task_Mixed_High cắt ngang

T=80
  Task_Mixed_PreLow tiếp tục và kết thúc

T=120
  Alarm_Mixed_NonPreLow hết hạn
  ActivateTask(TASK_MIXED_NONPRE_LOW)
  Task_Mixed_NonPreLow chạy

T=140
  Alarm_Mixed_NonPreHigh hết hạn
  ActivateTask(TASK_MIXED_HIGH)
  Current = Task_Mixed_NonPreLow, sched_class = NON_PREEMPTIVE
  os_should_preempt() return false
  Task_Mixed_High phải chờ

T=180
  Task_Mixed_NonPreLow kết thúc
  Dispatch point xảy ra
  Task_Mixed_High mới được chạy

T=220
  ScheduleTable start delay về 0
  offset 0
  ActivateTask(TASK_SCHTBL_LOW)

T=230
  ScheduleTable offset 10
  ActivateTask(TASK_SCHTBL_HIGH)

T=260
  ScheduleTable hết chu kỳ 40 tick
  repeating = 1
  reset offset = 0
  ActivateTask(TASK_SCHTBL_LOW)

T=270
  ScheduleTable offset 10
  ActivateTask(TASK_SCHTBL_HIGH)
```

---

## 11. Các Điểm Nên Đặt Breakpoint Khi Debug

Nếu debug bằng VS Code hoặc GDB, nên đặt breakpoint tại:

```text
Task_Mixed_PreLow
Task_Mixed_NonPreLow
Task_Mixed_High
Task_ScheduleTable_Low
Task_ScheduleTable_High
os_on_tick
ActivateTask
os_schedule
os_should_preempt
os_sched_table_on_tick
os_sched_table_fire_due_points
PendSV_Handler
```

Thứ tự quan sát khuyến nghị:

1. Break tại `Task_Mixed_PreLow`, xem tick 20.
2. Break tại `os_should_preempt`, xem current/candidate tại tick 40.
3. Break tại `Task_Mixed_High`, xem high chạy chen vào PreLow.
4. Break tại `Task_Mixed_NonPreLow`, xem tick 120.
5. Break tại `os_should_preempt`, xem current non-preemptive tại tick 140.
6. Break tại `Task_Mixed_High`, xem high chỉ chạy sau tick 180.
7. Break tại `os_sched_table_fire_due_points`, xem offset 0 và offset 10.
8. Break tại `Task_ScheduleTable_Low` và `Task_ScheduleTable_High`, xem tick 220/230/260/270.

Lệnh GDB mẫu:

```gdb
target remote localhost:3333
hbreak Task_Mixed_PreLow
hbreak Task_Mixed_High
hbreak Task_Mixed_NonPreLow
hbreak os_should_preempt
hbreak os_sched_table_fire_due_points
hbreak Task_ScheduleTable_Low
hbreak Task_ScheduleTable_High
continue
```

Khi dừng ở `os_should_preempt`, có thể xem:

```gdb
p current->name
p current->current_priority
p current->sched_class
p candidate->name
p candidate->current_priority
p candidate->sched_class
p g_tick_count
```

Kỳ vọng:

```text
Tick 40:
  current->name    = Task_Mixed_PreLow
  current priority = LOW
  current class    = PREEMPTIVE
  candidate->name  = Task_Mixed_High
  result           = true

Tick 140:
  current->name    = Task_Mixed_NonPreLow
  current priority = LOW
  current class    = NON_PREEMPTIVE
  candidate->name  = Task_Mixed_High
  result           = false
```

---

## 12. Log Kỳ Vọng Từ Renode

Khi chạy:

```bash
python3 test_os.py
```

hoặc chạy Renode thủ công, log UART quan trọng sẽ có dạng:

```text
[T=0]   [BOOT] PROFILE=MIXED_SCHEDULE_TABLE MODE=MIXED
[T=0]   [BOOT] Khoi tao phan cung thanh cong.
[T=20]  [MIXED_PRE_LOW] START preemptible window
[T=40]  [MIXED_HIGH] RUN while preemptible low is paused
[T=80]  [MIXED_PRE_LOW] END after high preempted it
[T=120] [MIXED_NONPRE_LOW] START nonpreemptible window
[T=180] [MIXED_NONPRE_LOW] END before queued high
[T=180] [MIXED_HIGH] RUN after nonpreemptible low finished
[T=220] [SCHTBL_LOW] RUN offset=0 priority=LOW
[T=230] [SCHTBL_HIGH] RUN offset=10 priority=HIGH
[T=260] [SCHTBL_LOW] RUN offset=0 priority=LOW
[T=270] [SCHTBL_HIGH] RUN offset=10 priority=HIGH
```

Nếu thấy High chạy tại tick 140 thì Mixed policy sai.

Nếu không thấy `SCHTBL_LOW` tại 220/260 hoặc `SCHTBL_HIGH` tại 230/270 thì ScheduleTable bị sai start delay, offset hoặc repeating.

---

## 13. Kết Luận Ngắn Gọn

Trong project hiện tại:

- `Mixed Scheduling` là logic nằm trong `os_should_preempt()` và `os_schedule()`.
- `ScheduleTable` là logic nằm trong `os_sched_table_on_tick()` và `os_sched_table_fire_due_points()`.
- `ScheduleTable` không thay thế scheduler. Nó chỉ activate task theo thời gian.
- Scheduler core vẫn là nơi quyết định context switch dựa trên ready queue, priority và policy Mixed.

Một câu để nhớ:

```text
ScheduleTable quyết định "khi nào task vào READY".
Mixed Scheduler quyết định "task READY có được cắt task đang RUNNING hay không".
```
