#ifndef OS_PORT_H
#define OS_PORT_H

/*
 * ============================================================
 *  os_port.h
 *  - Giao diện của lớp port phụ thuộc kiến trúc Cortex-M3.
 *  - Nói ngắn gọn: kernel quyết định "chạy ai", port quyết định
 *    "CPU phải save/restore và nhảy như thế nào".
 *
 *  Các trách nhiệm chính:
 *  - cấu hình priority của các exception hệ thống
 *  - dựng stack frame giả cho task mới
 *  - kích hoạt PendSV để đổi ngữ cảnh
 *  - phát SysTick định kỳ để kernel xử lý time base
 * ============================================================
 */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ====== Thuộc tính/attribute tiện ích ====== */
#ifndef OS_NORETURN
#  if defined(__GNUC__)
#    define OS_NORETURN __attribute__((noreturn))
#  else
#    define OS_NORETURN
#  endif
#endif

/* ====== Tham số cấu hình (đến từ os_config.h) ====== */
/* - OS_TICK_HZ: tần số tick của OS (mặc định 1000 Hz nếu không define) */
#ifndef OS_TICK_HZ
#  define OS_TICK_HZ 1000u
#endif

/* Kernel sẽ hiện thực hàm này; port chỉ gọi lại từ SysTick ISR. */
void os_on_tick(void);

/* ====== API của lớp port ====== */

/* Khởi tạo lớp port:
 *  - Đặt ưu tiên SVCall / PendSV / SysTick
 *  - Bật căn chỉnh stack 8-byte khi vào ngắt (STKALIGN)
 *  - Cấu hình SysTick theo OS_TICK_HZ (có thể gọi lại sau nếu muốn)
 */
void os_port_init(void);

/* Cấu hình lại SysTick với tần số bất kỳ, phục vụ reuse/debug nếu cần. */
void os_port_start_systick(uint32_t tick_hz);

/* Yêu cầu PendSV xảy ra ở thời điểm an toàn khi thoát ISR hiện tại. */
void os_trigger_pendsv(void);

/* Nếu task lỡ return khỏi entry thì rơi về đây và không bao giờ quay lại. */
OS_NORETURN void os_task_exit(void);

/*
 * Dựng stack PSP ban đầu theo đúng layout mà PendSV/SVC mong đợi.
 * Hàm trả về địa chỉ của R4 trong SW-frame để TCB->sp lưu lại.
 */
uint32_t *os_task_stack_init(void (*entry)(void *),
                             void *arg,
                             uint32_t *top);

#ifdef __cplusplus
}
#endif

#endif /* OS_PORT_H */
