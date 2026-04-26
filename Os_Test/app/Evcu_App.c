#include "Evcu_App.h"
#include "Evcu_Log.h"
#include "os_kernel.h"

#define EVCU_DTC_ENGINE_STATUS_MISSING 0x123456u
#define EVCU_DTC_ENGINE_TEMP_HIGH      0x234567u
#define EVCU_DTC_BATTERY_LOW           0x345678u
#define EVCU_DTC_CANTP_LONG_TEST       0x456789u
#define EVCU_DTC_EXTENDED_TEST         0x56789Au

#define EVCU_ENGINE_STATUS_TIMEOUT_MS 100u

static Evcu_State_t s_evcu;

static void prv_set_dtc(uint32 code, uint8 status, uint8 active)
{
    for (uint8 i = 0u; i < EVCU_DTC_COUNT_MAX; ++i) {
        if (s_evcu.dtcs[i].code == code) {
            if ((s_evcu.dtcs[i].active == 0u) && (active != 0u)) {
                Evcu_LogString("[EVCU][DTC] SET 0x");
                Evcu_LogHex8((uint8)(code >> 16u));
                Evcu_LogHex8((uint8)(code >> 8u));
                Evcu_LogHex8((uint8)code);
                Evcu_LogString("\r\n");
            }
            s_evcu.dtcs[i].status = status;
            s_evcu.dtcs[i].active = active;
            return;
        }
    }
}

void Evcu_Init(void)
{
    s_evcu.ignition_state = 1u;
    s_evcu.vehicle_speed = 0u;
    s_evcu.engine_rpm = 0u;
    s_evcu.engine_temp = 25u;
    s_evcu.engine_torque_actual = 0u;
    s_evcu.engine_state = 0u;
    s_evcu.battery_voltage_raw = 120u;
    s_evcu.torque_request = 80u;
    s_evcu.throttle_request = 0u;
    s_evcu.engine_start_request = 1u;
    s_evcu.alive_counter = 0u;
    s_evcu.communication_state = 0u;
    s_evcu.last_engine_status_tick = OS_GetTickCount();
    s_evcu.session = EVCU_SESSION_DEFAULT;

    s_evcu.dtcs[0] = (Evcu_Dtc_t){ EVCU_DTC_ENGINE_STATUS_MISSING, 0x2Fu, 1u };
    s_evcu.dtcs[1] = (Evcu_Dtc_t){ EVCU_DTC_ENGINE_TEMP_HIGH,      0x2Fu, 1u };
    s_evcu.dtcs[2] = (Evcu_Dtc_t){ EVCU_DTC_BATTERY_LOW,           0x2Fu, 1u };
    s_evcu.dtcs[3] = (Evcu_Dtc_t){ EVCU_DTC_CANTP_LONG_TEST,       0x09u, 1u };
    s_evcu.dtcs[4] = (Evcu_Dtc_t){ EVCU_DTC_EXTENDED_TEST,         0x09u, 1u };
}

void Evcu_MainFunction(void)
{
    uint32 now = OS_GetTickCount();
    uint32 age = now - s_evcu.last_engine_status_tick;

    s_evcu.alive_counter = (uint8)((s_evcu.alive_counter + 1u) & 0x0Fu);
    s_evcu.communication_state = (age <= EVCU_ENGINE_STATUS_TIMEOUT_MS) ? 1u : 0u;

    if (age > EVCU_ENGINE_STATUS_TIMEOUT_MS) {
        prv_set_dtc(EVCU_DTC_ENGINE_STATUS_MISSING, 0x2Fu, 1u);
    }
    if (s_evcu.engine_temp > 110u) {
        prv_set_dtc(EVCU_DTC_ENGINE_TEMP_HIGH, 0x2Fu, 1u);
    }
    if (s_evcu.battery_voltage_raw < 100u) {
        prv_set_dtc(EVCU_DTC_BATTERY_LOW, 0x2Fu, 1u);
    }
}

void Evcu_OnEngineStatus(uint16 rpm, uint8 temp, uint8 torque, uint8 engine_state, uint8 alive, uint8 crc)
{
    (void)crc;
    s_evcu.engine_rpm = rpm;
    s_evcu.engine_temp = temp;
    s_evcu.engine_torque_actual = torque;
    s_evcu.engine_state = engine_state;
    s_evcu.last_engine_status_tick = OS_GetTickCount();
    s_evcu.communication_state = 1u;

    Evcu_LogString("[EVCU][COM] RX EngineStatus rpm=");
    Evcu_LogU16(rpm);
    Evcu_LogString(" temp=");
    Evcu_LogU16(temp);
    Evcu_LogString(" alive=");
    Evcu_LogHex8(alive);
    Evcu_LogString("\r\n");
}

const Evcu_State_t *Evcu_GetState(void)
{
    return &s_evcu;
}

void Evcu_SetSession(Evcu_DiagnosticSession_e session)
{
    s_evcu.session = session;
}

Evcu_DiagnosticSession_e Evcu_GetSession(void)
{
    return s_evcu.session;
}

void Evcu_ClearAllDtcs(void)
{
    for (uint8 i = 0u; i < EVCU_DTC_COUNT_MAX; ++i) {
        s_evcu.dtcs[i].active = 0u;
    }
    Evcu_LogLine("[EVCU][DTC] CLEAR ALL");
}

uint8 Evcu_GetActiveDtcCount(void)
{
    uint8 count = 0u;
    for (uint8 i = 0u; i < EVCU_DTC_COUNT_MAX; ++i) {
        if (s_evcu.dtcs[i].active != 0u) {
            count++;
        }
    }
    return count;
}

uint8 Evcu_CopyActiveDtcs(Evcu_Dtc_t *dst, uint8 max_count)
{
    uint8 count = 0u;
    if (dst == 0) {
        return 0u;
    }

    for (uint8 i = 0u; (i < EVCU_DTC_COUNT_MAX) && (count < max_count); ++i) {
        if (s_evcu.dtcs[i].active != 0u) {
            dst[count++] = s_evcu.dtcs[i];
        }
    }

    return count;
}
