/*
 * ============================================================
 *  App_Task.c
 *  - Chứa các task minh họa cho thuật toán Non-Preemptive.
 * ============================================================
 */

#include "os_kernel.h"
#include "stm32f10x.h"
#include "stm32f10x_gpio.h"
#include "stm32f10x_rcc.h"
#include "stm32f10x_usart.h"

/* ------------------------------------------------------------
 * Cờ kiểm tra tính sẵn sàng của UART
 * ------------------------------------------------------------ */
static uint8_t g_uart_ready = 0u;

/* ============================================================
 * BSP: LED PC13 (BluePill, active-low)
 * ============================================================ */
static void gpio_init_led(void)
{
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOC, ENABLE);
    GPIO_InitTypeDef io;
    io.GPIO_Pin = GPIO_Pin_13;
    io.GPIO_Speed = GPIO_Speed_2MHz;
    io.GPIO_Mode = GPIO_Mode_Out_PP;
    GPIO_Init(GPIOC, &io);
    GPIO_SetBits(GPIOC, GPIO_Pin_13);
}

/* ============================================================
 * BSP: UART1 TX (PA9) – dùng cho trace/test
 * ============================================================ */
static void uart1_init_115200(void)
{
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_USART1, ENABLE);
    GPIO_InitTypeDef io;
    io.GPIO_Pin = GPIO_Pin_9;
    io.GPIO_Speed = GPIO_Speed_50MHz;
    io.GPIO_Mode = GPIO_Mode_AF_PP;
    GPIO_Init(GPIOA, &io);

    USART_InitTypeDef us;
    USART_StructInit(&us);
    us.USART_BaudRate = 115200;
    us.USART_WordLength = USART_WordLength_8b;
    us.USART_StopBits = USART_StopBits_1;
    us.USART_Parity = USART_Parity_No;
    us.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    us.USART_Mode = USART_Mode_Tx;
    USART_Init(USART1, &us);
    USART_Cmd(USART1, ENABLE);
}

static void uart1_send_char(char c)
{
    while (USART_GetFlagStatus(USART1, USART_FLAG_TXE) == RESET) {}
    USART_SendData(USART1, (uint16_t)c);
}

static void uart1_send_string(const char *s)
{
    while (*s != '\0') uart1_send_char(*s++);
}

/* ============================================================
 * Helper trace tối giản
 * ============================================================ */
static void app_u32_to_dec(char *buf, uint32_t value)
{
    char temp[11];
    uint8_t len = 0u;
    if (value == 0u) { buf[0] = '0'; buf[1] = '\0'; return; }
    while (value > 0u) { temp[len++] = (char)('0' + (value % 10u)); value /= 10u; }
    for (uint8_t i = 0u; i < len; ++i) buf[i] = temp[len - 1u - i];
    buf[len] = '\0';
}

static void app_log_tick_prefix(void)
{
    char tick_buf[11];
    if (g_uart_ready == 0u) return;
    app_u32_to_dec(tick_buf, OS_GetTickCount());
    uart1_send_string("[T=");
    uart1_send_string(tick_buf);
    uart1_send_string("] ");
}

static void app_log_line(const char *text)
{
    if (g_uart_ready == 0u) return;
    app_log_tick_prefix();
    uart1_send_string(text);
    uart1_send_string("\r\n");
}

/* Hàm tạo chờ thời gian thực CPU (tính theo tick) */
static void app_busy_wait_ticks(uint32_t duration_ticks)
{
    uint32_t start_tick = OS_GetTickCount();
    while ((OS_GetTickCount() - start_tick) < duration_ticks) { __NOP(); }
}

static void app_log_boot_banner(void)
{
    if (g_uart_ready == 0u) return;
    app_log_tick_prefix();
    uart1_send_string("[BOOT] OS_SCHED_MODE_DEFAULT=NON_PREEMPTIVE\r\n");
}

/* ============================================================
 * TASKS MINH HỌA NON PREEMPTIVE
 * ============================================================ */

/* Task chạy nền, tốn một lượng CPU để tạo tình huống chờ cho Task High bị đẩy vào ready queue */
void Task_NP_Low(void *arg)
{
    (void)arg;

    app_log_line("[NP_LOW] Task Low Bat Dau (Dang chiem dung CPU)");
    
    /* 
     * Task Low chạy 60 ticks. 
     * Alarm của Task High sẽ nổ ở tick 40. Nếu là Full Preemptive, Task Low sẽ bị ngắt ở tick 40.
     * Vì đang cấu hình Non-Preemptive, nó tiếp tục chạy xong mà không ai ngắt,
     * sau đó mới thả CPU ra bằng TerminateTask().
     */
    app_busy_wait_ticks(60u);

    /* Phần code này chạy trước ngay cả khi Alarm High đã nổ */
    app_log_line("[NP_LOW] Task Low Tiep Tuc va Ket Thuc (Sau khi Alarm High no, nhung van KHONG bi cat ngang)");
    TerminateTask();
}

/* Task khẩn cấp, có quyền ưu tiên cao nhất, nhưng vẫn phải đứng xếp hàng chờ */
void Task_NP_High(void *arg)
{
    (void)arg;

    /* 
     * Task này có Priority cao hơn. Alarm nổ lúc tick 40, nhưng TerminateTask
     * của Task Low mới xảy ra lúc tick 80. Khi đó Task này mới được gọi.
     */
    app_log_line("[NP_HIGH] => Task High Nhay Vao (Chi duoc chay sau khi Task Low ket thuc!)");
    
    TerminateTask();
}

/* Task khởi tạo cấu hình HW và phần mềm */
void Task_Init(void *arg)
{
    (void)arg;
    gpio_init_led();
    uart1_init_115200();
    g_uart_ready = 1u;

    app_log_boot_banner();
    app_log_line("[BOOT] Khoi tao phan cung thanh cong.");
    TerminateTask();
}

/* Fallback task: Nhiệm vụ nghỉ lúc rảnh */
void Task_Idle(void *arg)
{
    (void)arg;
    for (;;) {
#if OS_IDLE_USE_WFI
        __WFI();
#else
        __NOP();
#endif
    }
}
