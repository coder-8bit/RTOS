#include "Evcu_Com.h"
#include "Evcu_App.h"
#include "Evcu_Log.h"
#include "Com.h"

static uint8 prv_crc4(uint8 b0, uint8 b1, uint8 b2, uint8 alive)
{
    return (uint8)((b0 ^ b1 ^ b2 ^ alive) & 0x0Fu);
}

void Evcu_ComInit(void)
{
}

void Evcu_ComOnRxPdu(PduIdType pdu_id)
{
    if (pdu_id != ComConf_ComIPdu_EngineStatus) {
        return;
    }

    uint16 rpm = 0u;
    uint8 temp = 0u;
    uint8 torque = 0u;
    uint8 state = 0u;
    uint8 alive = 0u;
    uint8 crc = 0u;

    if (Com_ReceiveSignal(ComSig_Engine_RPM, &rpm) != E_OK) return;
    if (Com_ReceiveSignal(ComSig_Engine_Temp, &temp) != E_OK) return;
    if (Com_ReceiveSignal(ComSig_Engine_TorqueActual, &torque) != E_OK) return;
    if (Com_ReceiveSignal(ComSig_Engine_State, &state) != E_OK) return;
    if (Com_ReceiveSignal(ComSig_Engine_Alive, &alive) != E_OK) return;
    if (Com_ReceiveSignal(ComSig_Engine_CRC, &crc) != E_OK) return;

    Evcu_OnEngineStatus(rpm, temp, torque, state, alive, crc);
}

void Evcu_ComMainFunction(void)
{
    const Evcu_State_t *state = Evcu_GetState();
    uint8 throttle = state->throttle_request;
    boolean start = (state->engine_start_request != 0u) ? TRUE : FALSE;
    uint8 torque_limit = state->torque_request;
    uint8 alive = state->alive_counter & 0x0Fu;
    uint8 crc = prv_crc4(throttle, start ? 1u : 0u, torque_limit, alive);

    (void)Com_SendSignal(ComSig_Vehicle_ThrottleReq, &throttle);
    (void)Com_SendSignal(ComSig_Vehicle_EngineStartReq, &start);
    (void)Com_SendSignal(ComSig_Vehicle_TorqueLimit, &torque_limit);
    (void)Com_SendSignal(ComSig_Vehicle_Alive, &alive);
    (void)Com_SendSignal(ComSig_Vehicle_CRC, &crc);

    if (Com_TriggerIPDUSend(ComConf_ComIPdu_VehicleCommand) == E_OK) {
        Evcu_LogString("[EVCU][COM] TX VehicleCommand alive=");
        Evcu_LogHex8(alive);
        Evcu_LogString("\r\n");
    }
}
