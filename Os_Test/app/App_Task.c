#include "os_kernel.h"

#include "Evcu_Log.h"
#include "os_app_cfg.h"
#include "stm32f10x.h"

static void log_tick_prefix(void)
{
    Evcu_LogString("[T=");
    Evcu_LogU16((uint16)OS_GetTickCount());
    Evcu_LogString("] ");
}

static void log_event_line(const char *text)
{
    log_tick_prefix();
    Evcu_LogLine(text);
}

static void log_event_mask(const char *text, EventMaskType mask)
{
    log_tick_prefix();
    Evcu_LogString(text);
    Evcu_LogString("0x");
    Evcu_LogHex8((uint8)((mask >> 24u) & 0xFFu));
    Evcu_LogHex8((uint8)((mask >> 16u) & 0xFFu));
    Evcu_LogHex8((uint8)((mask >> 8u) & 0xFFu));
    Evcu_LogHex8((uint8)(mask & 0xFFu));
    Evcu_LogString("\r\n");
}

void Task_EventBoot(void *arg)
{
    (void)arg;

    Evcu_LogInit();
    Evcu_LogString("\r\n");
    log_event_line("[BOOT] PROFILE=EVENT_API_DEMO");
    log_event_line("[BOOT] APIs=WaitEvent SetEvent ClearEvent GetEvent");
    log_event_line("[BOOT] Task_EventConsumer waits before producer runs");

    TerminateTask();
}

void Task_EventConsumer(void *arg)
{
    (void)arg;

    EventMaskType events = 0u;

    log_event_line("[CONSUMER] WaitEvent(EVENT_RX_DONE)");
    WaitEvent(EVENT_RX_DONE);

    GetEvent(TASK_EVENT_CONSUMER, &events);
    log_event_mask("[CONSUMER] GetEvent after RX mask=", events);

    ClearEvent(EVENT_RX_DONE);
    GetEvent(TASK_EVENT_CONSUMER, &events);
    log_event_mask("[CONSUMER] GetEvent after ClearEvent(RX) mask=", events);

    log_event_line("[CONSUMER] WaitEvent(EVENT_TIMEOUT)");
    WaitEvent(EVENT_TIMEOUT);

    GetEvent(TASK_EVENT_CONSUMER, &events);
    log_event_mask("[CONSUMER] GetEvent after TIMEOUT mask=", events);

    ClearEvent(EVENT_TIMEOUT);
    GetEvent(TASK_EVENT_CONSUMER, &events);
    log_event_mask("[CONSUMER] GetEvent after ClearEvent(TIMEOUT) mask=", events);

    log_event_line("[CONSUMER] Event demo complete");
    TerminateTask();
}

void Task_EventProducer(void *arg)
{
    (void)arg;

    log_event_line("[PRODUCER] SetEvent(EVENT_RX_DONE)");
    SetEvent(TASK_EVENT_CONSUMER, EVENT_RX_DONE);

    log_event_line("[PRODUCER] resumed after consumer blocked again");
    log_event_line("[PRODUCER] SetEvent(EVENT_TIMEOUT)");
    SetEvent(TASK_EVENT_CONSUMER, EVENT_TIMEOUT);

    log_event_line("[PRODUCER] producer complete");
    TerminateTask();
}

void Task_Idle(void *arg)
{
    (void)arg;
    for (;;)
    {
#if OS_IDLE_USE_WFI
        __WFI();
#else
        __NOP();
#endif
    }
}
