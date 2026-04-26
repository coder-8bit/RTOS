#include "Evcu_Log.h"

#include "stm32f10x_gpio.h"
#include "stm32f10x_rcc.h"
#include "stm32f10x_usart.h"

static uint8 s_log_ready = 0u;

void Evcu_LogInit(void)
{
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_USART1, ENABLE);

    GPIO_InitTypeDef io;
    io.GPIO_Pin = GPIO_Pin_9;
    io.GPIO_Speed = GPIO_Speed_50MHz;
    io.GPIO_Mode = GPIO_Mode_AF_PP;
    GPIO_Init(GPIOA, &io);

    USART_InitTypeDef us;
    USART_StructInit(&us);
    us.USART_BaudRate = 115200u;
    us.USART_WordLength = USART_WordLength_8b;
    us.USART_StopBits = USART_StopBits_1;
    us.USART_Parity = USART_Parity_No;
    us.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    us.USART_Mode = USART_Mode_Tx;
    USART_Init(USART1, &us);
    USART_Cmd(USART1, ENABLE);

    s_log_ready = 1u;
}

static void prv_putc(char c)
{
    if (s_log_ready == 0u) {
        return;
    }
    while (USART_GetFlagStatus(USART1, USART_FLAG_TXE) == RESET) {}
    USART_SendData(USART1, (uint16)c);
}

void Evcu_LogString(const char *str)
{
    if (str == 0) {
        return;
    }
    while (*str != '\0') {
        prv_putc(*str++);
    }
}

void Evcu_LogLine(const char *str)
{
    Evcu_LogString(str);
    Evcu_LogString("\r\n");
}

void Evcu_LogHex8(uint8 value)
{
    static const char hex[] = "0123456789ABCDEF";
    prv_putc(hex[(value >> 4u) & 0x0Fu]);
    prv_putc(hex[value & 0x0Fu]);
}

void Evcu_LogU16(uint16 value)
{
    char buf[6];
    uint8 len = 0u;
    char tmp[5];

    if (value == 0u) {
        prv_putc('0');
        return;
    }

    while (value > 0u) {
        tmp[len++] = (char)('0' + (value % 10u));
        value = (uint16)(value / 10u);
    }

    for (uint8 i = 0u; i < len; ++i) {
        buf[i] = tmp[len - 1u - i];
    }
    buf[len] = '\0';
    Evcu_LogString(buf);
}

void Log_Print(const char *str)
{
    Evcu_LogString(str);
}
