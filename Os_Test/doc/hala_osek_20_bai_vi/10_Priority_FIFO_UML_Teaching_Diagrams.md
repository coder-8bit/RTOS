# Bài 10 - Priority FIFO: Bộ Sơ Đồ UML Chi Tiết Để Dạy Học

## Mục tiêu
- Dùng bài [09_Priority_FIFO_Manual_Test_Flow.md](./09_Priority_FIFO_Manual_Test_Flow.md) làm nền.
- Chuyển toàn bộ flow `priority_fifo` sang dạng sơ đồ để dễ dạy trên lớp.
- Cho phép người học nhìn được:
  - cấu hình nào tạo ra scenario
  - luồng kích hoạt từ `alarm` sang `task`
  - đường đi của `ready queue`
  - quyết định của `os_schedule()`
  - điểm khác nhau giữa `READY` và `RUNNING`
  - vì sao `Task_Fifo2` phải chờ

---

## 1. Phạm vi của bài này

Scenario được mô tả ở đây là:
- profile: `PRIORITY_FIFO`
- scheduler mode: `OS_SCHEDMODE_PRIORITY_FIFO`
- task test:
  - `Task_Fifo1`
  - `Task_Fifo2`
- alarm test:
  - `ALARM_FIFO_1` kích ở `20 ms`
  - `ALARM_FIFO_2` kích ở `21 ms`

Log runtime mong đợi:

```text
[T=0]   [BOOT] PROFILE=PRIORITY_FIFO MODE=PRIORITY_FIFO
[T=0]   [BOOT] Peripherals initialized
[T=20]  [FIFO] Task_Fifo1 START
[T=40]  [FIFO] Task_Fifo1 END
[T=40]  [FIFO] Task_Fifo2 RUN
```

---

## 2. Cách dùng tài liệu này trên lớp

Thứ tự giảng nên là:
1. Sơ đồ tổng thể kiến trúc.
2. Sơ đồ boot và autostart.
3. Sơ đồ tick `20`.
4. Sơ đồ tick `21`.
5. Sơ đồ tick `40`.
6. State diagram của task.
7. Sơ đồ activity của `os_schedule()`.
8. Sơ đồ ready queue snapshot.

Lý do:
- học viên cần thấy bức tranh tổng trước
- sau đó mới hiểu từng lát cắt theo thời gian

---

## 3. Sơ đồ tổng thể kiến trúc của scenario

### 3.1. Thành phần chính

```mermaid
flowchart LR
    APP_CFG[Config/os_app_cfg.c<br/>profile PRIORITY_FIFO] --> AL1[ALARM_FIFO_1<br/>delay = 20 ms]
    APP_CFG --> AL2[ALARM_FIFO_2<br/>delay = 21 ms]
    APP_CFG --> T1[Task_Fifo1<br/>prio = MEDIUM]
    APP_CFG --> T2[Task_Fifo2<br/>prio = MEDIUM]

    AL1 --> TICK[os_on_tick]
    AL2 --> TICK
    TICK --> ACT[ActivateTask]
    ACT --> RQ[Ready Queue<br/>prio MEDIUM]
    RQ --> SCH[os_schedule]
    SCH --> DISP[os_dispatch]
    DISP --> PSV[PendSV_Handler]
    PSV --> CPU[CPU chay task duoc chon]
```

### 3.2. Ý nghĩa sư phạm

- `Config` chỉ mô tả hệ thống.
- `Alarm` chỉ tạo activation.
- `Ready Queue` chỉ giữ task đã sẵn sàng.
- `Scheduler` chỉ quyết định "ai chạy tiếp".
- `PendSV` mới là nơi CPU đổi context thật.

Đây là một điểm dạy rất quan trọng:

**`ActivateTask()` không đồng nghĩa với "CPU chạy task đó ngay".**

---

## 4. Sơ đồ boot và autostart

### 4.1. Sequence diagram giai đoạn boot

```mermaid
sequenceDiagram
    participant MAIN as main
    participant K as OS_Init
    participant CFG as Task/Alarm Config
    participant RQ as Ready Queue
    participant AL as Alarm Runtime
    participant SVC as SVC_Handler
    participant INIT as Task_Init
    participant IDLE as Task_Idle

    MAIN->>K: OS_Init()
    K->>CFG: Doc bang task config
    K->>CFG: Doc bang alarm config
    K->>RQ: ReadyQueue reset
    K->>AL: Nap ALARM_FIFO_1 = 20 ms
    K->>AL: Nap ALARM_FIFO_2 = 21 ms
    K->>RQ: Enqueue Task_Init
    K->>RQ: Enqueue Task_Idle
    K->>RQ: Pop highest -> Task_Init
    K->>MAIN: g_current = Task_Init
    MAIN->>SVC: OS_Start()
    SVC->>INIT: Launch Task_Init
    INIT->>INIT: init LED + UART
    INIT->>K: TerminateTask()
    K->>RQ: Pop next -> Task_Idle
    K->>IDLE: dispatch Idle
```

### 4.2. Điều cần nhấn mạnh khi dạy

- `Task_Fifo1` và `Task_Fifo2` chưa có mặt trong ready queue lúc boot.
- Chúng chỉ xuất hiện khi alarm hết hạn.
- `Task_Idle` là ngữ cảnh nền để CPU luôn có thứ chạy.

---

## 5. Sơ đồ cấu hình của scenario `priority_fifo`

### 5.1. Quan hệ giữa profile, alarm và task

```mermaid
classDiagram
    class PriorityFifoProfile {
        +profile = PRIORITY_FIFO
        +scheduler_mode = PRIORITY_FIFO
    }

    class Task_Fifo1 {
        +priority = MEDIUM
        +autostart = false
        +body = START -> busy_wait(20) -> END -> TerminateTask
    }

    class Task_Fifo2 {
        +priority = MEDIUM
        +autostart = false
        +body = RUN -> TerminateTask
    }

    class Alarm_Fifo1 {
        +delay = 20 ms
        +cycle = 0
        +target = Task_Fifo1
    }

    class Alarm_Fifo2 {
        +delay = 21 ms
        +cycle = 0
        +target = Task_Fifo2
    }

    PriorityFifoProfile --> Alarm_Fifo1
    PriorityFifoProfile --> Alarm_Fifo2
    Alarm_Fifo1 --> Task_Fifo1
    Alarm_Fifo2 --> Task_Fifo2
```

### 5.2. Ý nghĩa

- hai task cùng priority là điều kiện để lộ rõ FIFO
- hai alarm lệch nhau `1 ms` là điều kiện để task sau đến khi task trước vẫn đang chạy

---

## 6. Sơ đồ timeline tổng thể theo tick

```mermaid
sequenceDiagram
    participant T as Tick Timeline
    participant IDLE as Task_Idle
    participant F1 as Task_Fifo1
    participant F2 as Task_Fifo2

    Note over T,IDLE: T = 0..19
    IDLE->>IDLE: CPU ranh / doi alarm

    Note over T,F1: T = 20
    T->>F1: ALARM_FIFO_1 kich hoat
    F1->>F1: START

    Note over T,F1: T = 21..39
    T->>F2: ALARM_FIFO_2 kich hoat o T = 21
    Note over F2: F2 = READY, chua duoc chay
    F1->>F1: busy_wait_ticks(20)

    Note over T,F1: T = 40
    F1->>F1: END
    F1->>F2: CPU chuyen sang F2
    F2->>F2: RUN
    F2->>IDLE: ket thuc, quay ve idle
```

### 6.1. Điểm cần dạy

- `Task_Fifo2` được kích ở `T=21`, nhưng không chạy tại `T=21`.
- Điều học viên thường hiểu sai là "được kích" đồng nghĩa "được chạy".
- Timeline này giúp tách rõ:
  - **activation time**
  - **dispatch time**

---

## 7. Sequence diagram chi tiết tại tick 20

### 7.1. Tick 20: `Task_Fifo1` được đưa từ `DORMANT` sang `RUNNING`

```mermaid
sequenceDiagram
    participant ST as SysTick_Handler
    participant TICK as os_on_tick
    participant AL as alarm_tbl[ALARM_FIFO_1]
    participant ACT as ActivateTask(TASK_FIFO_1)
    participant RQ as Ready Queue MEDIUM
    participant EXIT as os_isr_exit
    participant SCH as os_schedule
    participant DISP as PendSV/SVC flow
    participant F1 as Task_Fifo1

    ST->>TICK: tick interrupt
    TICK->>TICK: g_tick_count++
    TICK->>AL: remain_ticks--
    AL-->>TICK: remain_ticks == 0
    TICK->>ACT: ActivateTask(TASK_FIFO_1)
    ACT->>ACT: activation_count++
    ACT->>ACT: state DORMANT -> READY
    ACT->>RQ: enqueue tail(Task_Fifo1)
    TICK->>EXIT: os_isr_exit()
    EXIT->>SCH: scheduler duoc goi
    SCH->>SCH: current = Idle
    SCH->>SCH: candidate = Task_Fifo1
    SCH->>SCH: idle phai nhuong CPU
    SCH->>DISP: request context switch
    DISP->>F1: launch Task_Fifo1
```

### 7.2. Ý nghĩa

Ở tick `20`, `Task_Fifo1` chạy ngay không phải vì mode `priority_fifo` cho phép preempt thông thường, mà vì:
- current là `Idle`
- kernel có nhánh đặc biệt: nếu current là idle và candidate là task ứng dụng thì phải nhường CPU

Đây là điểm cần nói rõ để học viên không nhầm giữa:
- **rời idle**
- và **preempt task ứng dụng đang chạy**

---

## 8. Sequence diagram chi tiết tại tick 21

### 8.1. Tick 21: `Task_Fifo2` được kích nhưng chưa được chạy

```mermaid
sequenceDiagram
    participant F1 as Task_Fifo1 (RUNNING)
    participant ST as SysTick_Handler
    participant TICK as os_on_tick
    participant AL as alarm_tbl[ALARM_FIFO_2]
    participant ACT as ActivateTask(TASK_FIFO_2)
    participant RQ as Ready Queue MEDIUM
    participant EXIT as os_isr_exit
    participant SCH as os_schedule
    participant PRE as os_should_preempt

    F1->>F1: dang busy_wait_ticks(20)
    ST->>TICK: tick interrupt
    TICK->>TICK: g_tick_count++
    TICK->>AL: remain_ticks--
    AL-->>TICK: remain_ticks == 0
    TICK->>ACT: ActivateTask(TASK_FIFO_2)
    ACT->>ACT: activation_count++
    ACT->>ACT: state DORMANT -> READY
    ACT->>RQ: enqueue tail(Task_Fifo2)
    TICK->>EXIT: os_isr_exit()
    EXIT->>SCH: scheduler duoc goi
    SCH->>SCH: current = Task_Fifo1
    SCH->>SCH: candidate = Task_Fifo2
    SCH->>PRE: os_should_preempt(F1, F2)
    PRE-->>SCH: false (mode PRIORITY_FIFO)
    SCH-->>F1: tiep tuc chay
    Note over RQ: Task_Fifo2 van nam trong ready queue
```

### 8.2. Câu chốt để dạy

Tick `21` là nơi học viên phải hiểu được câu sau:

**`Task_Fifo2` đã READY nhưng chưa RUNNING, vì scheduler mode hiện tại không cho phép cắt ngang `Task_Fifo1`.**

---

## 9. Sequence diagram chi tiết tại tick 40

### 9.1. Tick 40: `Task_Fifo1` kết thúc, `Task_Fifo2` được lấy ra khỏi queue

```mermaid
sequenceDiagram
    participant F1 as Task_Fifo1
    participant TERM as TerminateTask
    participant SCH as os_schedule
    participant RQ as Ready Queue MEDIUM
    participant PSV as PendSV_Handler
    participant F2 as Task_Fifo2
    participant IDLE as Task_Idle

    F1->>F1: log END
    F1->>TERM: TerminateTask()
    TERM->>TERM: activation_count--
    TERM->>TERM: state RUNNING -> DORMANT
    TERM->>SCH: os_schedule()
    SCH->>RQ: pop highest ready
    RQ-->>SCH: Task_Fifo2
    SCH->>PSV: request context switch
    PSV->>F2: restore context / launch
    F2->>F2: log RUN
    F2->>TERM: TerminateTask()
    TERM->>SCH: ready queue rong
    SCH->>IDLE: quay lai idle
```

### 9.2. Điểm rất quan trọng

`Task_Fifo2 RUN` có thể xuất hiện cùng tick `40` với `Task_Fifo1 END`.

Điều này đúng vì:
- tick là đơn vị thời gian của OS
- trong cùng tick `40`, có thể xảy ra nhiều bước nhỏ:
  - log `END`
  - `TerminateTask()`
  - `os_schedule()`
  - `PendSV`
  - log `RUN`

---

## 10. State diagram của `Task_Fifo1`

```mermaid
stateDiagram-v2
    [*] --> DORMANT
    DORMANT --> READY: Tick 20 / ActivateTask
    READY --> RUNNING: os_schedule + PendSV
    RUNNING --> DORMANT: Tick 40 / TerminateTask
```

### Ý nghĩa

`Task_Fifo1` không quay về `READY` sau khi chạy xong vì:
- `max_activations = 1`
- alarm là one-shot
- không có activation pending thứ hai

---

## 11. State diagram của `Task_Fifo2`

```mermaid
stateDiagram-v2
    [*] --> DORMANT
    DORMANT --> READY: Tick 21 / ActivateTask
    READY --> READY: Tick 21..39 / cho trong ready queue
    READY --> RUNNING: Tick 40 / current task ket thuc
    RUNNING --> DORMANT: TerminateTask
```

### Ý nghĩa

Đây là state diagram quan trọng nhất của bài:

Nó cho thấy một task có thể:
- đã được kích
- đã `READY`
- nhưng vẫn chưa hề chạy

---

## 12. Sơ đồ activity của `os_schedule()` trong mode `priority_fifo`

### 12.1. Activity diagram

```mermaid
flowchart TD
    A[os_schedule duoc goi] --> B{Dang o ISR nesting<br/>hoac g_next != NULL?}
    B -->|Co| X[Thoat, khong schedule]
    B -->|Khong| C{current == NULL?}
    C -->|Co| X
    C -->|Khong| D{current.state != RUNNING?}

    D -->|Co| E[Pop task ready priority cao nhat]
    E --> F{Co task hop le?}
    F -->|Co| G[Set g_next = next]
    G --> H[Trigger PendSV]
    F -->|Khong| X

    D -->|Khong| I[Peek task ready priority cao nhat]
    I --> J{Co candidate?}
    J -->|Khong| X
    J -->|Co| K[os_should_preempt(current, candidate)]
    K -->|false| X
    K -->|true| L[Requeue current vao dau queue]
    L --> M[Pop candidate]
    M --> G
```

### 12.2. Khi gắn vào scenario `priority_fifo`

- tick `20`: current là `Idle`, nhánh `preempt` hợp lệ vì idle phải nhường CPU
- tick `21`: current là `Task_Fifo1`, candidate là `Task_Fifo2`, `os_should_preempt()` trả `false`
- tick `40`: current không còn `RUNNING`, đi sang nhánh dispatch point và pop `Task_Fifo2`

---

## 13. Sơ đồ activity của `os_should_preempt()`

```mermaid
flowchart TD
    A[os_should_preempt current candidate] --> B{current hoac candidate NULL?}
    B -->|Co| R0[false]
    B -->|Khong| C{candidate == current?}
    C -->|Co| R0
    C -->|Khong| D{current la Idle<br/>va candidate khong phai Idle?}
    D -->|Co| R1[true]
    D -->|Khong| E{candidate.prio <= current.prio?}
    E -->|Co| R0
    E -->|Khong| F{scheduler mode}
    F -->|PRIORITY_FIFO| R0
    F -->|NON_PREEMPTIVE| R0
    F -->|FULL_PREEMPTIVE| R2[true]
    F -->|MIXED| G{current preemptible?}
    G -->|Co| R2
    G -->|Khong| R0
```

### Ý nghĩa khi dạy bài này

Bài `priority_fifo` chỉ dùng đúng hai nhánh:
- `Idle -> true`
- `PRIORITY_FIFO -> false`

Bạn nên cho học viên thấy điều này để họ hiểu:
- cùng một hàm
- nhưng hành vi thay đổi theo mode

---

## 14. Sơ đồ snapshot ready queue theo từng thời điểm

### 14.1. Snapshot tại tick 19

```mermaid
flowchart LR
    subgraph P4[Priority 4]
        P4Q[(rong)]
    end
    subgraph P3[Priority 3]
        P3Q[(rong)]
    end
    subgraph P2[Priority 2]
        P2Q[(rong)]
    end
    subgraph P1[Priority 1]
        P1Q[(rong)]
    end
    subgraph P0[Priority 0]
        P0Q[(rong)]
    end
```

### 14.2. Snapshot ngay sau tick 20

```mermaid
flowchart LR
    subgraph P2[Priority MEDIUM]
        P2Q1[Task_Fifo1]
    end
```

Sau đó scheduler pop `Task_Fifo1`, nên queue lại rỗng và `Task_Fifo1` trở thành `RUNNING`.

### 14.3. Snapshot ngay sau tick 21

```mermaid
flowchart LR
    subgraph CPU[Current]
        CUR[Task_Fifo1 RUNNING]
    end

    subgraph P2[Ready Queue MEDIUM]
        H[head] --> Q1[Task_Fifo2]
        Q1 --> T[tail]
    end
```

Đây là snapshot quan trọng nhất của bài.

### 14.4. Snapshot tại tick 40 trước khi dispatch

```mermaid
flowchart LR
    subgraph CPU[Current]
        CUR[Task_Fifo1 vua goi TerminateTask]
    end

    subgraph P2[Ready Queue MEDIUM]
        H[head] --> Q1[Task_Fifo2]
        Q1 --> T[tail]
    end
```

### 14.5. Snapshot tại tick 40 sau khi dispatch

```mermaid
flowchart LR
    subgraph CPU[Current]
        CUR[Task_Fifo2 RUNNING]
    end

    subgraph P2[Ready Queue MEDIUM]
        EMPTY[(rong)]
    end
```

---

## 15. Sơ đồ so sánh 3 khái niệm dễ nhầm

```mermaid
flowchart TD
    A[Alarm het han] --> B[ActivateTask]
    B --> C[Task state = READY]
    C --> D[vao ready queue]
    D --> E[Scheduler chon task]
    E --> F[PendSV context switch]
    F --> G[Task state = RUNNING]
```

### Câu dạy học rất quan trọng

Không được gộp các bước này thành một ý.

Phải tách đúng:
- alarm hết hạn
- task được activate
- task được enqueue
- scheduler quyết định
- CPU mới thực sự chạy task

---

## 16. Sơ đồ "vì sao không preempt ở tick 21"

```mermaid
flowchart TD
    A[Tick 21] --> B[ALARM_FIFO_2 het han]
    B --> C[ActivateTask(Task_Fifo2)]
    C --> D[Task_Fifo2 -> READY]
    D --> E[enqueue vao queue MEDIUM]
    E --> F[os_schedule duoc goi]
    F --> G[current = Task_Fifo1, candidate = Task_Fifo2]
    G --> H[os_should_preempt = false]
    H --> I[Task_Fifo1 tiep tuc chay]
    I --> J[Task_Fifo2 van READY trong queue]
```

### Kết luận ngắn

Tick `21` là nơi thể hiện "bản chất logic thuật toán".

Nếu học viên hiểu được sơ đồ này, họ sẽ hiểu đúng toàn bài.

---

## 17. Sơ đồ "vì sao lại chạy ở tick 40"

```mermaid
flowchart TD
    A[Task_Fifo1 END] --> B[TerminateTask]
    B --> C[current.state != RUNNING]
    C --> D[os_schedule di vao dispatch point]
    D --> E[pop highest ready]
    E --> F[lay Task_Fifo2 tu dau queue]
    F --> G[g_next = Task_Fifo2]
    G --> H[PendSV context switch]
    H --> I[Task_Fifo2 RUN]
```

### Ý nghĩa

Ở mode `priority_fifo`, đổi task xảy ra chủ yếu tại:
- task hiện tại kết thúc
- hoặc task hiện tại không còn ở trạng thái `RUNNING`

---

## 18. UML state tổng hợp cho cả hai task

```mermaid
stateDiagram-v2
    [*] --> IdleRunning

    IdleRunning --> F1Ready: Tick 20 / ActivateTask(F1)
    F1Ready --> F1Running: Scheduler dispatch

    F1Running --> F2Ready: Tick 21 / ActivateTask(F2)
    F2Ready --> F1Running: Khong preempt, F1 tiep tuc chay

    F1Running --> F1Dormant: Tick 40 / F1 TerminateTask
    F1Dormant --> F2Running: Scheduler pop F2
    F2Running --> F2Dormant: F2 TerminateTask
    F2Dormant --> IdleRunning: Queue rong, quay lai idle
```

### Cách giảng

Sơ đồ này rất hợp để kết bài vì nó gộp:
- idle
- activation
- waiting in queue
- dispatch
- terminate

thành một mạch duy nhất.

---

## 19. Sơ đồ dạy học so sánh nhanh với `full_preemptive`

Nếu muốn dẫn nhập sang bài kế tiếp, bạn có thể dùng sơ đồ so sánh ngắn này:

```mermaid
flowchart LR
    subgraph PF[PRIORITY_FIFO]
        PF1[Tick 21: F2 READY] --> PF2[Khong preempt]
        PF2 --> PF3[Cho F1 ket thuc]
        PF3 --> PF4[Tick 40: F2 RUN]
    end

    subgraph FP[FULL_PREEMPTIVE]
        FP1[Tick 21: F2 READY] --> FP2[Neu F2 prio cao hon F1]
        FP2 --> FP3[Preempt ngay]
        FP3 --> FP4[F2 RUN tai tick 21]
    end
```

### Ý nghĩa

Cùng một cơ chế `ready queue`, khác nhau ở:
- policy preemption
- thời điểm context switch

---

## 20. Checklist giảng bài trên lớp

### 20.1. Câu mở bài

- "Hai task cùng priority, task đến sau có được chen ngang không?"
- "READY có đồng nghĩa RUNNING không?"

### 20.2. Câu hỏi giữa bài

- Vì sao tick `21` đã có `Task_Fifo2` nhưng CPU vẫn ở `Task_Fifo1`?
- FIFO nằm ở đâu trong code?
- `os_schedule()` ở tick `21` đi nhánh nào?

### 20.3. Câu kết bài

- Nếu đổi sang `FULL_PREEMPTIVE`, sơ đồ nào thay đổi?
- Nếu `Task_Fifo2` có priority cao hơn thì sequence diagram tại tick `21` khác gì?

---

## 21. Kết luận ngắn gọn để chốt bài

Bạn có thể chốt toàn bộ bài học chỉ bằng 4 câu:

1. Alarm chỉ kích hoạt task, không trực tiếp bắt CPU chạy task.
2. Ready queue giữ task sẵn sàng theo thứ tự priority và FIFO.
3. Trong mode `priority_fifo` của project này, task đang chạy không bị task mới cùng priority cắt ngang.
4. Vì vậy `Task_Fifo2` dù được activate ở tick `21` vẫn phải chờ đến khi `Task_Fifo1` terminate ở tick `40`.

---

## 22. Liên kết với bài nền

Tài liệu gốc mô tả chi tiết bằng văn bản:

- [09_Priority_FIFO_Manual_Test_Flow.md](./09_Priority_FIFO_Manual_Test_Flow.md)

Tài liệu này là bản "dạy bằng sơ đồ" của chính flow đó.
