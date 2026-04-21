# Hướng Dẫn Kế Hoạch Test Full Preemptive (Renode + GDB)

## Mục Đích
Kiểm chứng tường tận luồng chạy của thuật toán **Full Preemptive** bằng trình gỡ lỗi theo từng dòng lệnh thay vì chỉ dựa vào test script tự động. Qua đó, ta sẽ bắt được tận tay khoảnh khắc CPU đang nằm trong `Task_FP_Low` bất ngờ "đẩy" register vào stack và nhảy vào `Task_FP_High`.

*(Lưu ý: Do phát triển nhúng lớp Firmware Bare-metal nên ta chạy `arm-none-eabi-gdb` thay vì `adb` vốn dùng cho thiết bị Android).*

## Các Bước Chuẩn Bị
Đảm bảo bạn đã lưu cấu hình và build lại Project bằng lệnh sau tại thư mục gốc:
```bash
make clean && make -j4 all
```

## Khởi Động Máy Ảo
Mở **Terminal 1** và chạy máy ảo (nhưng chưa cấu hình CPU chạy ngay, để còn kết nối GDB):
```bash
renode --plain
```
Bên trong giao diện dòng lệnh của Renode:
```renode
(machine-0) include @renode.resc
(machine-0) machine StartGdbServer 3333
```
*Ghi chú: Lệnh trên mở cổng gỡ lỗi 3333. Hệ thống ảo hoá đã tạm dừng chờ kết nối.*

## Kết Nối GDB Và Bắt Lỗi (Breakpoint)
Mở **Terminal 2**, khởi động bộ gỡ lỗi:
```bash
arm-none-eabi-gdb build/os_test/os_test.elf
```
Bên trong môi trường (gdb), hãy dán thứ tự các lệnh cực kỳ quan trọng sau:
```gdb
# Kết nối vào Renode
(gdb) target remote localhost:3333

# Cắm cờ - Điểm dừng 1: Lúc Task Low bắt đầu (khoảng tick 20)
(gdb) break Task_FP_Low

# Cắm cờ - Điểm dừng 2: Lúc Task High xông vào chiếm quyền
(gdb) break Task_FP_High

# Cắm cờ - Điểm dừng 3 (Siêu phân tích): Ngay hàm cốt lõi của RTOS Preemptive
(gdb) break os_should_preempt

# Ra lệnh cho phần cứng ảo bắt đầu chạy
(gdb) continue
```

## Kỳ Vọng Phân Tích Thực Tế

1. **Lần Dừng Lại Đầu Tiên (Hit 1):** Code sẽ dừng tại hàm `Task_FP_Low` (Điểm dừng 1). Điều này xảy ra do Cấu hình báo thức `Alarm_FP_Low` nổ ở mốc 20ms.
   - Bạn gõ `continue` để hệ thống chạy qua cái vòng lặp tốn thời gian.
2. **Lần Dừng Thứ Hai (Hit 2):** Bùm! `os_should_preempt` được gọi (Điểm dừng 3). Đây là do ngắt SysTick lần thứ 40 đếm trúng `Alarm_FP_High` dẫn đến kích hoạt cờ Ready cho `Task_FP_High`. Ngay trong hàm này, nó sẽ so đo mức priority. Bạn có thể gõ lệnh `print candidate->name` để xem. Căn cứ theo comment trong code, nó sẽ bung dòng `return true;`.
3. **Lần Dừng Thứ Ba (Hit 3):** Trình gỡ lỗi sẽ dừng ngay trong lòng `Task_FP_High` (Điểm dừng 2). Lúc này nếu bạn gõ lệnh `bt` (backtrace), bạn sẽ thấy nó hoàn toàn không được gọi bởi `Task_FP_Low` mà do ngắt hệ thống sinh ra context switch!

## Chi Tiết Kế Hoạch Test Python Tham Khảo
Theo yêu cầu, script `test_os.py` cũng đã được refactor và chú giải cặn kẽ 100% tiếng việt.
- Lệnh chạy: `python3 test_os.py`
- Đây là **Regression Test**, hãy tham khảo kịch bản Regex của nó để viết bài test tương lai của riêng bạn khi nhúng Continuous Integration.
