#pragma once

/*
 * ============================================================
 *  App_Task.h
 *  - Prototype cho các task test thuật toán Full Preemptive.
 * ============================================================
 */

void Task_Init(void *arg);
void Task_FP_Low(void *arg);
void Task_FP_High(void *arg);
void Task_Idle(void *arg);
