# Phân tích Luồng Thực Thi Trực Tiếp của Full Preemptive (Từ Boot đến Kết Thúc)

Tài liệu này mô tả chi tiết luồng chạy của chương trình firmware OSEK-RTOS từ lúc bắt đầu (`main`) cho đến khi hoàn thành kịch bản kiểm thử độc quyền của thuật toán **Full Preemptive**.

---

## 1. Quá trình Khởi động Hệ thống (Boot & OS Init)

**📍 Vị trí bắt đầu:** Hàm `main()` trong `main.c`.

1. **`SystemInit()`**:
   - CMSIS cấp phát cấu hình xung nhịp (clock tree) cho vi điều khiển STM32. 
   - Đảm bảo CPU hoạt động đúng tần số (ví dụ 72MHz) trước khi vào Kernel.
2. **Setup Debug (`printf`)**:
   - In ra dòng `Hello GDB! SysClk=...` và dòng khởi động ban đầu. Các thao tác này chủ yếu hỗ trợ Debug giao diện semihosting (chưa ảnh hưởng đến OS).
3. **`OS_Init()`** (trong `os_kernel.c`):
   - Reset toàn bộ CTDL của Kernel: `os_ready_queue_reset()`, `os_alarm_reset_all()`.
   - Quét qua bảng cấu hình tĩnh `g_os_task_cfg` (từ `os_app_cfg.c`) để tạo _Task Control Block (TCB)_ cho 4 task: `Task_Init`, `Task_FP_Low`, `Task_FP_High`, `Task_Idle`.
   - **Xử lý Autostart Task**: Kernel phát hiện `Task_Init` và `Task_Idle` có cờ `.autostart = 1`. Do đó, nạp thẳng 2 Task này vào Ready Queue.
   - **Xử lý Autostart Alarm**: Kernel kích hoạt ngay 2 Alarm (`Alarm_FP_Low` ở Tick 20 và `Alarm_FP_High` ở Tick 40).
   - Kernel dò ở Ready Queue, thấy `Task_Init` có độ ưu tiên cao trần (`CRITICAL`) nên trỏ con trỏ `g_current` vào TCB của `Task_Init`.
4. **`OS_Start()`** (trong `os_kernel.c`):
   - Gọi ngắt lõi Cortex: `__ASM volatile("svc 0");`.
   - Từ điểm này, Luồng điều khiển **vĩnh viễn rời khỏi** hàm `main()`. Khối xử lý chuyển sang Assembly của `SVC_Handler`.
   - Cốt lõi của SVC là lấy con trỏ stack (`sp`) trong `g_current` ra để phục hồi biến và ép thanh ghi PC nhảy vào thân hàm `Task_Init`.

---

## 2. Thiết lập Ứng Dụng (Tick = 0)

Ngay sau lệnh nhảy SVC, phần mềm đang đứng ở đầu chu kỳ với `g_tick_count = 0`:

**📍 Hàm đang chạy:** `Task_Init()` trong `App_Task.c`

1. **Khởi tạo Ngoại Vi (Peripheral):**
   - Gọi `gpio_init_led()` và `uart1_init_115200()`. Bật cờ `g_uart_ready = 1`.
2. **In Boot Banner Log:**
   - In: `[T=0] [BOOT] OS_SCHED_MODE_DEFAULT=FULL_PREEMPTIVE`
   - In: `[T=0] [BOOT] Khoi tao phan cung thanh cong.`
3. **`TerminateTask()`:**
   - Khi chạy xong nhiệm vụ mồi, Task gọi thư viện OS để tự kết liễu.
   - Trạng thái `Task_Init` chuyển từ **RUNNING** sang **DORMANT**.
   - Scheduler (`os_schedule`) được gọi. Nhận thấy `Task_Init` đã bỏ cuộc, CPU dò vào Ready Queue và thấy ứng viên duy nhất còn lại là `Task_Idle`. Cập nhật `g_next = Task_Idle` và kích hoạt ngắt PendSV.
   - Ngắt PendSV sẽ đẩy CPU từ `Task_Init` sang thẳng `Task_Idle()`.

---

## 3. Quá trình Nghỉ chờ ngắt (Tick từ 1 đến 19)

**📍 Hàm đang chạy:** `Task_Idle()` trong `App_Task.c`

1. Tại đây, CPU đi vào vòng lặp vô tận. Nếu có macro SLEEP (`__WFI`), lõi CPU sẽ đi vào dấc ngủ tiết kiệm năng lượng.
2. Hệ thống đếm nhịp thông qua SysTick Interrupt - `os_on_tick()`. Cứ mỗi 1 milisecond, biến `g_tick_count` sẽ tăng lên 1, đồng thời đếm lùi thời gian các Alarm. Tại mốc thời gian này, các Alarm vẫn đang đếm lùi, chưa đến giờ kích hoạt.

---

## 4. Kích hoạt Task Low (Tick 20)

Khi `g_tick_count == 20`, ngắt SysTick (`os_tick_handler() -> os_on_tick()`) phát hiện ra Alarm đầu tiên `Alarm_FP_Low` hết số Tick chờ.

1. **`ActivateTask(TASK_FP_LOW)`:** 
   - `os_on_tick` nạp `Task_FP_Low` vào danh sách sẵn sàng (READY).
2. **Kết thúc gọi ngắt SysTick (`os_isr_exit`):** 
   - SysTick thấy `Task_FP_Low` đang ở trạng thái READY, và nhận ra `Priority = LOW` lớn hơn `Priority = IDLE` của `Task_Idle`.
   - Kernel kích PendSV để ép Context Switch. CPU đẩy trạng thái `Task_Idle` vào stack và chuyển sang `Task_FP_Low`.
   - **📍 Chấn lưu chương trình đứng tại:** `Task_FP_Low()` trong `App_Task.c`.
3. Nhảy vào `Task_FP_Low`:
   - Task này in ra log: `[T=20] [FP_LOW] Task Low Bat Dau (Dang chiem dung CPU)`.
   - Sau đó gọi một hàm bận: `app_busy_wait_ticks(60u);`. Có nghĩa là CPU bị vòng lặp for "chiếm lấy, hút 100% công suất" một cách chủ đích kéo dài trong vòng 60 Tick. 
   - Quá trình này sẽ diễn ra từ **Tick 20 đến Tick 80**. 

---

## 5. Hoạt động Nội Sinh (Under-the-hood) của Bộ Lập Lịch

Trước khi đi đến sự kiện vĩ đại ở Tick 40, hãy nhìn lướt qua cách mà hàm `os_schedule()` xử lý đằng sau cánh gà. Bất cứ khi nào có ngắt, báo thức, hoặc một task tự kết liễu, `os_schedule()` đều sẽ được triệu gọi. Thuật toán xử lý của hàm này chia làm 2 nhánh chính:

1. **Nhánh A (Task cũ đã nhường CPU):** Khi `g_current` hiện tại có trạng thái `!= OS_RUNNING` (do nó vừa gọi `TerminateTask()` hoặc `WaitEvent()`).
   - Scheduler sẽ không cần kiểm tra quyền ưu tiên Preemption làm gì.
   - Nó lập tức chọc vào Ready Queue bằng hàm vòng `os_ready_pop_highest()` để lấy số ID của Task đang chực chờ có mức độ ưu tiên cao nhất.
   - Đẩy Task ID mới đó vào khay `g_next`, trỏ trạng thái thành `OS_RUNNING` và cờ Dispatch kích hoạt PendSV.
   
2. **Nhánh B (Task cũ VẪN ĐANG chiễm chệ trên CPU):** Khi `g_current` có trạng thái `== OS_RUNNING` (Ví dụ đang bận xử lý vòng lặp thì bị ngắt SysTick cắt ngang kích hoạt `ActivateTask` cho ứng viên mới).
   - Module so sánh `os_should_preempt(current, candidate)` sẽ chạy.
   - **Luật 1:** Nếu mức Prioriy của Candidate <= mức Current: Trả về **FALSE** (Cấm vượt).
   - **Luật 2:** Nếu mức Priority Candidate > Current, lúc này tuỳ thuộc vào Mode của Kernel. Chế độ **FULL_PREEMPTIVE** sẽ lập tức nhả **TRUE**. Chế độ NON_PREEMPTIVE sẽ nhả FALSE.
   - Nếu trả về TRUE: Scheduler hạ trạng thái `current` xuống `OS_READY`, rồi nhét nó trả lại vào **Đầu hàng (head) của Ready Queue** (`os_ready_add_front`). Sau đó, đẩy `candidate` lên làm `g_next`, cập nhật trạng thái `OS_RUNNING` và phát lệnh nổ súng PendSV để ép Context Switch.

---

## 6. Sự Vượt Mặt - Tâm Điểm Của Full Preemptive (Tick 40)

**📍 Hàm đang chạy hiện tại:** `Task_FP_Low()` (Đang kẹt trong vòng lặp `app_busy_wait_ticks`).

Ngay tại thời điểm `g_tick_count == 40`, một ngắt SysTick nổ. Cơn bão bắt đầu từ đây:

1. `Alarm_FP_High` đã đếm lùi xong. Nó lập tức gọi `ActivateTask(TASK_FP_HIGH)`.
   - Hàm `ActivateTask` cho Task_FP_High cập nhật trạng thái của nó từ DORMANT lên READY. Và nhét Task này vào Ready Queue.
2. Hàm ngắt SysTick gọi `os_isr_exit()` -> Gọi **`os_schedule()`** đi vào nhánh B (như vừa nói ở trên).
   - **Thẩm định Preemption (`os_should_preempt`):**
     - Candidate (`Task_FP_High`) có độ ưu tiên là `CRITICAL`.
     - Current (`Task_FP_Low`) đang trọn quyền điều khiển CPU, có ưu tiên là `LOW`.
     - Chế độ "Full Preemptive" khiến hàm lập tức trả về giá trị **TRUE - Chiếm quyền bằng mọi giá**.
3. **Cắt ngang phần mềm (Ngắt PendSV):**
   - Vòng lặp For trong `Task_FP_Low` đang chạy dở bị ngắt điện lạnh lùng.
   - Trạng thái các thanh ghi Core Register (R0-R12, PC, LR...) của `Task_FP_Low` được ném hết vào RAM thông qua cơ chế Context Save của Cortex-M. Con trỏ SP của `Task_FP_Low` được lưu trú an toàn.
   - Vi điều khiển ép nạp thanh ghi SP của `Task_FP_High` lên làm Context chính yếu.

**📍 Hàm vừa mới nhảy sang:** `Task_FP_High()`.

4. Nhảy vào `Task_FP_High`:
   - In ra bằng chứng chiếm quyền: `[T=40] [FP_HIGH] => Task High Nhay Vao Cat Ngang Thanh Cong!`.
5. **Gọi `TerminateTask()`:**
   - Chạy xong nhiệm vụ ngắt ngang, `Task_FP_High` ngoan ngoãn chết đi (`DORMANT`).
   - Lệnh `TerminateTask()` lại gọi **`os_schedule()`** đi vào nhánh A (Task hiện tại đã nhường CPU). Nó móc cái Task Ready cao nhất - lúc này chính là ông cụ `Task_FP_Low` hồi nãy chưa làm xong việc - lôi lên, nhồi Context cũ của ông cụ vào lại các thanh ghi Cortex-M3.

---

## 7. Phục Hồi Và Kết Thúc Kịch Bản (Tick từ 40 đến 80)

**📍 Hàm được lấy lại quyền:** `Task_FP_Low()`.

1. Nhờ ma thuật của stack Context Restore, `Task_FP_Low` hoàn toàn ngây ngất tiếp tục như chưa từng có cuộc chia ly. Mã nguồn máy (Assembly) lại tiếp tay đếm vòng lặp `app_busy_wait_ticks(60u)`. Bất chấp thực tại bên ngoài nãy giờ mọi thứ đã lộn tùng phèo, đối với tác vụ Low thì thời gian như ngưng đọng.
2. **Tại chu kì Tick 80:** 
   - Vòng lặp chặn 60 Ticks hoàn tất (80 - 20 = 60).
   - In ra thông báo: `[T=80] [FP_LOW] Task Low Tiep Tuc va Ket Thuc (Sau khi Task High xong)`.
3. **`Task_FP_Low` tự vĩnh biệt bằng lệnh `TerminateTask()`**.
4. Context nhả lại về thanh ghi cho `Task_Idle` theo một kịch bản Scheduler nhánh A tương tự.
5. Lúc này hệ thống duy trì `IDLE` miên man, do không còn Alarm nào được setup thêm. Bài test cơ bản đã hoàn tất xuất sắc chu trình thẩm định Preemption tại mốc Tick 200 quy định bởi Script của Python Renode Automation.
