/*
 * ============================================================
 *  os_port.c
 *  - Hiện thực phần C của port Cortex-M3.
 *  - File này không chứa policy scheduler; nó chỉ xử lý cơ chế CPU:
 *      + cấu hình SysTick
 *      + cấu hình priority exception
 *      + dựng stack frame ban đầu
 *      + phát yêu cầu PendSV
 *
 *  Ý tưởng phân lớp:
 *  - kernel: quyết định "task nào là next"
 *  - port C + ASM: làm cho CPU chuyển thật sang task đó
 * ============================================================
 */

#include "os_port.h"
#include "os_config.h" /* nếu bạn có file cấu hình riêng; không bắt buộc */
#include "stm32f10x.h"
#include "core_cm3.h"

/* ============================================================
 *  Ghi chú về ưu tiên ngắt trên ARMv7-M (Cortex-M3):
 *  - Giá trị PRIORITY càng LỚN → ưu tiên CÀNG THẤP.
 *  - Khuyến nghị: PendSV để THẤP NHẤT để tránh preempt trong lúc đổi ngữ cảnh.
 *  - SysTick nhỉnh hơn PendSV một chút (cao hơn ưu tiên), để có thể lên lịch rồi
 *    yêu cầu PendSV thực thi sau.
 *  - SVCall ở mức trung bình (dùng để "launch" task đầu tiên và các syscall).
 *
 *  __NVIC_PRIO_BITS định nghĩa số bit ưu tiên có hiệu lực (STM32F1 thường = 4).
 *  NVIC_SetPriority() dùng giá trị "thô", không cần tự dịch bit.
 * ============================================================
 */

/*
 * Hàm tiện ích nếu sau này cần tính "priority thấp nhất" theo số bit ưu tiên
 * thật sự của vi điều khiển. Bản hiện tại chưa dùng trực tiếp, nhưng giữ lại
 * để người đọc hiểu cách mã priority được suy ra.
 */
static inline uint32_t prv_prio_lowest(void)
{
    return (1u << __NVIC_PRIO_BITS) - 1u; /* ví dụ: 4 bit → 0x0F */
}

/* ============================================================
 *  Khởi tạo lớp port:
 *   - Đặt ưu tiên SVC / PendSV / SysTick
 *   - Bật STKALIGN để HW đảm bảo stack 8-byte khi vào ISR
 *   - Cấu hình SysTick theo OS_TICK_HZ
 * ============================================================
 */
void os_port_init(void)
{
    /*
     * 1) Thiết lập priority cho các system exception.
     *
     * Chiến lược hiện tại:
     * - PendSV thấp nhất: để nó chỉ chạy khi mọi ISR quan trọng hơn đã xong
     * - SysTick cao hơn PendSV một chút: tick có thể set cờ reschedule
     * - SVCall ngang/tương đương SysTick: chỉ dùng bootstrap task đầu tiên
     *
     * Ở Cortex-M, số priority càng lớn thì mức ưu tiên thực càng thấp.
     */
    NVIC_SetPriority(PendSV_IRQn, 0xFF);
    NVIC_SetPriority(SVCall_IRQn, 0xFE);
    NVIC_SetPriority(SysTick_IRQn, 0xFE);

    /* Không dùng chặn ngắt theo BASEPRI ở bản hiện tại. */
    __set_BASEPRI(0);

    /*
     * 2) Yêu cầu hardware luôn căn stack 8-byte khi vào exception.
     * Điều này giúp:
     * - phù hợp AAPCS
     * - tránh lỗi khó debug khi save/restore context có alignment xấu
     */
    SCB->CCR |= SCB_CCR_STKALIGN_Msk;

    /* 3) Bật time base định kỳ cho OS. */
    os_port_start_systick(OS_TICK_HZ);
}

/* ============================================================
 *  Cấu hình SysTick theo tick_hz (ví dụ 1000 Hz = 1ms)
 *  - Lưu ý: SystemCoreClock phải được cập nhật đúng (SystemInit).
 * ============================================================
 */
void os_port_start_systick(uint32_t tick_hz)
{
    /*
     * Luôn tắt SysTick trước khi đổi cấu hình để tránh:
     * - reload cũ còn hiệu lực dở dang
     * - tạo ngắt trong lúc LOAD/VAL đang thay đổi
     */
    SysTick->CTRL = 0;

    /* LOAD = (HCLK / tick_hz) - 1.
     * Ví dụ: HCLK=72MHz, tick=1kHz → LOAD = 72_000_000/1000 - 1 = 71999.
     */
    uint32_t reload = (SystemCoreClock / tick_hz) - 1u;

    /* Ghi reload mới và xóa bộ đếm hiện tại. */
    SysTick->LOAD = reload;
    SysTick->VAL = 0;

    /*
     * CLKSOURCE = HCLK:
     * - dùng clock hệ thống trực tiếp để tính tick.
     *
     * TICKINT = 1:
     * - cho phép phát exception SysTick.
     *
     * ENABLE = 1:
     * - bắt đầu đếm ngay sau khi cấu hình xong.
     */
    SysTick->CTRL = SysTick_CTRL_CLKSOURCE_Msk |
                    SysTick_CTRL_TICKINT_Msk |
                    SysTick_CTRL_ENABLE_Msk;
}

/* ============================================================
 *  Kích hoạt PendSV (yêu cầu đổi ngữ cảnh)
 *  - Việc đổi thực sự sẽ diễn ra khi thoát ISR hiện tại.
 *  - DSB/ISB đảm bảo hiệu lực ghi trước khi rẽ nhánh/thoát ngắt.
 * ============================================================
 */
void os_trigger_pendsv(void)
{
    /*
     * Nếu trước đó có cơ chế mask bằng BASEPRI thì gỡ ra để PendSV không bị
     * chặn ngoài ý muốn. Bản hiện tại chủ yếu dùng PRIMASK trong critical section,
     * nhưng lệnh này vẫn là guard tốt ở ranh giới port.
     */
    __set_BASEPRI(0);
    __DSB();
    __ISB();

    /* Ghi bit PENDSVSET để yêu cầu CPU chạy PendSV khi thích hợp. */
    SCB->ICSR = SCB_ICSR_PENDSVSET_Msk;

    __DSB();
    __ISB();
}

/* ============================================================
 *  Task exit (nếu thân task return)
 *  - Thiết kế kernel: task KHÔNG được return; nếu có → rơi vào vòng WFI.
 *  - Có thể thay bằng ShutdownOS() tuỳ kiến trúc hệ thống.
 * ============================================================
 */
OS_NORETURN void os_task_exit(void)
{
    /*
     * Nếu task return thay vì gọi TerminateTask(), ta không để nó tiếp tục
     * chạy "rơi" vào vùng nhớ ngẫu nhiên. Giải pháp an toàn nhất là ngủ vĩnh viễn.
     */
    for (;;)
    {
        __WFI(); /* ngủ vĩnh viễn */
    }
}

/* ------------------------------------------------------------
 *  "task_bootstrap_exit": được gán vào LR của HW-frame ban đầu.
 *  Nếu entry() return → CPU sẽ nhảy vào đây → thoát vĩnh viễn.
 * ------------------------------------------------------------
 */
static void task_bootstrap_exit(void) OS_NORETURN;
static void task_bootstrap_exit(void)
{
    /* LR của task mới sẽ trỏ về đây. Nếu entry() return, CPU sẽ rơi vào hàm này. */
    os_task_exit();
    /* không bao giờ chạy tới đây */
    __builtin_unreachable();
}

/* ============================================================
 *  Dựng stack PSP ban đầu cho task entry(void *arg)
 *
 *  Layout PSP (địa chỉ tăng xuống dưới):
 *
 *    ... [thấp địa chỉ] ...
 *      R4  R5  R6  R7  R8  R9  R10 R11     (SW-frame do phần mềm save/restore)
 *      R0  R1  R2  R3  R12 LR  PC  xPSR    (HW-frame do phần cứng pop khi EXC_RETURN)
 *    ... [cao địa chỉ] ...
 *
 *  Sau khi PendSV restore {r4-r11}, con trỏ PSP sẽ trỏ tới R0 (đầu HW-frame).
 *  Khi "BX EXC_RETURN", phần cứng tự pop HW-frame và nhảy vào PC=entry|1.
 * ============================================================
 */


uint32_t *os_task_stack_init(void (*entry)(void *),
                             void *arg,
                             uint32_t *top)
{
    /*
     * Bước 1:
     * Căn top stack về biên 8 byte để phù hợp ABI của ARM.
     * Việc căn này đặc biệt quan trọng vì stack của task sẽ được hardware và
     * assembly cùng thao tác trong quá trình exception entry/return.
     */
    uint32_t *sp = (uint32_t *)((uintptr_t)top & ~((uintptr_t)0x7));


    /*
     * Bước 2:
     * Dựng HW-frame giả đúng theo thứ tự hardware sẽ tự POP khi exception return.
     *
     * Thứ tự "unstack" của HW khi EXC_RETURN:
     *    R0, R1, R2, R3, R12, LR, PC, xPSR
     *
     *  Ta push theo thứ tự ngược lại để khi pop ra đúng:
     *    xPSR → PC → LR → R12 → R3 → R2 → R1 → R0
     */
    *(--sp) = 0x01000000u;                          /* xPSR: T-bit=1 (Thumb) */
    *(--sp) = ((uint32_t)entry) | 1u;               /* PC: địa chỉ hàm entry | 1 */
    *(--sp) = ((uint32_t)task_bootstrap_exit) | 1u; /* LR: nếu entry return → thoát */
    *(--sp) = 0x00000000u;                          /* R12 */
    *(--sp) = 0x00000000u;                          /* R3  */
    *(--sp) = 0x00000000u;                          /* R2  */
    *(--sp) = 0x00000000u;                          /* R1  */
    *(--sp) = (uint32_t)arg;                        /* R0  (tham số truyền vào entry) */

    /*
     * Bước 3:
     * Dựng SW-frame là phần register do PendSV save/restore bằng phần mềm.
     * Khi LDMIA {r4-r11}, con trỏ PSP sẽ tiến
     *  đến &R0 (đầu HW-frame), đúng kỳ vọng của PendSV restore.
     *  Giá trị khởi tạo 0 là đủ (không bắt buộc).
     */
    *(--sp) = 0x00000000u; /* R11 */
    *(--sp) = 0x00000000u; /* R10 */
    *(--sp) = 0x00000000u; /* R9  */
    *(--sp) = 0x00000000u; /* R8  */
    *(--sp) = 0x00000000u; /* R7  */
    *(--sp) = 0x00000000u; /* R6  */
    *(--sp) = 0x00000000u; /* R5  */
    *(--sp) = 0x00000000u; /* R4  */




    /*
     * Bước 4:
     * Trả về địa chỉ của R4 trong SW-frame.
     * Đây chính là giá trị mà TCB->sp phải giữ để assembly restore đúng layout.
     */
    return sp;
}
