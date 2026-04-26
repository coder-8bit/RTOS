#pragma once

#include <stdint.h>
#include "os_types.h"

typedef enum {
    TASK_EVENT_BOOT = 0,
    TASK_EVENT_CONSUMER,
    TASK_EVENT_PRODUCER,
    TASK_IDLE,
    TASK_COUNT
} TaskId_e;

typedef enum {
    ALARM_UNUSED = 0,
    ALARM_COUNT
} AlarmId_e;

typedef enum {
    SCHEDULE_TABLE_UNUSED = 0,
    SCHEDULE_TABLE_COUNT
} ScheduleTableId_e;

enum {
    EVENT_RX_DONE = (1u << 0),
    EVENT_TIMEOUT = (1u << 1)
};

void Task_EventBoot(void *arg);
void Task_EventConsumer(void *arg);
void Task_EventProducer(void *arg);
void Task_Idle(void *arg);

extern const OsTaskConfig_t g_os_task_cfg[TASK_COUNT];
extern const OsAlarmConfig_t g_os_alarm_cfg[ALARM_COUNT];
extern const OsScheduleTableConfig_t g_os_schedule_table_cfg[SCHEDULE_TABLE_COUNT];

const char *App_GetActiveProfileName(void);
