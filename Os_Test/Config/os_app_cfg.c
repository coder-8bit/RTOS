#include "os_app_cfg.h"
#include "os_config.h"

const OsTaskConfig_t g_os_task_cfg[TASK_COUNT] = {
    [TASK_EVENT_BOOT] = {
        .id = TASK_EVENT_BOOT,
        .name = "Task_EventBoot",
        .entry = Task_EventBoot,
        .arg = 0,
        .stack_words = 160u,
        .base_priority = OS_PRIO_CRITICAL,
        .max_activations = 1u,
        .autostart = 1u,
        .is_idle_task = 0u,
        .sched_class = OS_TASK_SCHED_PREEMPTIVE
    },
    [TASK_EVENT_CONSUMER] = {
        .id = TASK_EVENT_CONSUMER,
        .name = "Task_EventConsumer",
        .entry = Task_EventConsumer,
        .arg = 0,
        .stack_words = 192u,
        .base_priority = OS_PRIO_HIGH,
        .max_activations = 1u,
        .autostart = 1u,
        .is_idle_task = 0u,
        .sched_class = OS_TASK_SCHED_PREEMPTIVE
    },
    [TASK_EVENT_PRODUCER] = {
        .id = TASK_EVENT_PRODUCER,
        .name = "Task_EventProducer",
        .entry = Task_EventProducer,
        .arg = 0,
        .stack_words = 192u,
        .base_priority = OS_PRIO_MEDIUM,
        .max_activations = 1u,
        .autostart = 1u,
        .is_idle_task = 0u,
        .sched_class = OS_TASK_SCHED_PREEMPTIVE
    },
    [TASK_IDLE] = {
        .id = TASK_IDLE,
        .name = "Task_Idle",
        .entry = Task_Idle,
        .arg = 0,
        .stack_words = 128u,
        .base_priority = OS_PRIO_IDLE,
        .max_activations = 1u,
        .autostart = 1u,
        .is_idle_task = 1u,
        .sched_class = OS_TASK_SCHED_NON_PREEMPTIVE
    }
};

const OsAlarmConfig_t g_os_alarm_cfg[ALARM_COUNT] = {
    [ALARM_UNUSED] = {
        .id = ALARM_UNUSED,
        .name = "Alarm_Unused",
        .autostart = 0u,
        .delay_ms = 0u,
        .cycle_ms = 0u,
        .target_task = TASK_IDLE
    }
};

const OsScheduleTableConfig_t g_os_schedule_table_cfg[SCHEDULE_TABLE_COUNT] = {
    [SCHEDULE_TABLE_UNUSED] = {
        .id = SCHEDULE_TABLE_UNUSED,
        .name = "ScheduleTable_Unused",
        .autostart = 0u,
        .repeating = 0u,
        .start_delay_ms = 0u,
        .duration_ms = 1u,
        .expiry_points = 0,
        .num_expiry_points = 0u
    }
};

const char *App_GetActiveProfileName(void)
{
    return "EVENT_API_DEMO";
}
