/*
 * ============================================================
 *  main.c
 *  - Đây là điểm vào đầu tiên của firmware sau startup assembly.
 *  - Vai trò của file này cố tình giữ tối giản:
 *      1. Khởi tạo clock/hệ thống cơ bản của STM32.
 *      2. In vài dòng debug để xác nhận firmware đã chạy tới main.
 *      3. Khởi tạo mini-kernel.
 *      4. Chuyển hẳn quyền điều khiển sang OS.
 *
 *  Ý quan trọng:
 *  - Sau khi gọi OS_Start(), luồng chạy không còn quay về main() nữa.
 *  - Task đầu tiên sẽ được launch qua SVC_Handler ở tầng assembly.
 * ============================================================
 */

#include "os_kernel.h"
#include "stm32f10x.h"
#include <stdio.h>

int main(void)
{
    /*
     * SystemInit() đến từ CMSIS / system_stm32f10x.c.
     * Hàm này thiết lập clock tree, PLL, vector relocation nếu cần,
     * và cập nhật nền tảng phần cứng để các peripheral phía sau dùng được.
     */
    SystemInit();

    /*
     * Các dòng puts/printf dưới đây chủ yếu phục vụ debug:
     * - Nếu semihosting hoặc debugger đang hoạt động, ta sẽ thấy log trên host.
     * - Nếu không có debugger, _write() trong semihost.c sẽ bỏ qua an toàn.
     */
    puts("SEMIO ready\n");
    printf("Hello GDB! SysClk=%lu Hz\n", (unsigned long)SystemCoreClock);
    printf("Back to main()!\n");

    /*
     * OS_Init():
     * - dựng dữ liệu kernel
     * - cấu hình Port Cortex-M3
     * - tạo TCB/stack cho toàn bộ task
     * - nạp ready queue, alarm và schedule table theo profile cấu hình
     */
    OS_Init();

    /*
     * OS_Start():
     * - phát lệnh SVC
     * - từ đây CPU sẽ rời main và nhảy vào task đầu tiên qua exception return.
     */
    OS_Start();

    /*
     * Về mặt thiết kế sẽ không quay lại đây.
     * Vòng lặp được giữ lại như một guard cuối cùng nếu có lỗi logic bootstrap.
     */
    for (;;)
    {
        /* never here */
    }
}
