#ifndef EVCU_APP_H
#define EVCU_APP_H

#include "Std_Types.h"

#define EVCU_DTC_COUNT_MAX 5u

typedef enum {
    EVCU_SESSION_DEFAULT = 1u,
    EVCU_SESSION_EXTENDED = 3u
} Evcu_DiagnosticSession_e;

typedef struct {
    uint32 code;
    uint8 status;
    uint8 active;
} Evcu_Dtc_t;

typedef struct {
    uint8 ignition_state;
    uint8 vehicle_speed;
    uint16 engine_rpm;
    uint8 engine_temp;
    uint8 engine_torque_actual;
    uint8 engine_state;
    uint8 battery_voltage_raw;
    uint8 torque_request;
    uint8 throttle_request;
    uint8 engine_start_request;
    uint8 alive_counter;
    uint8 communication_state;
    uint32 last_engine_status_tick;
    Evcu_DiagnosticSession_e session;
    Evcu_Dtc_t dtcs[EVCU_DTC_COUNT_MAX];
} Evcu_State_t;

void Evcu_Init(void);
void Evcu_MainFunction(void);
void Evcu_OnEngineStatus(uint16 rpm, uint8 temp, uint8 torque, uint8 engine_state, uint8 alive, uint8 crc);

const Evcu_State_t *Evcu_GetState(void);
void Evcu_SetSession(Evcu_DiagnosticSession_e session);
Evcu_DiagnosticSession_e Evcu_GetSession(void);
void Evcu_ClearAllDtcs(void);
uint8 Evcu_GetActiveDtcCount(void);
uint8 Evcu_CopyActiveDtcs(Evcu_Dtc_t *dst, uint8 max_count);

#endif /* EVCU_APP_H */
