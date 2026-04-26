#include "Evcu_Dcm.h"
#include "Evcu_App.h"
#include "Evcu_Log.h"
#include "CanTp.h"

#include <string.h>

#define EVCU_DCM_TX_BUFFER_SIZE 64u

#define NRC_SERVICE_NOT_SUPPORTED 0x11u
#define NRC_SUBFUNCTION_NOT_SUPPORTED 0x12u
#define NRC_INCORRECT_LENGTH 0x13u
#define NRC_CONDITIONS_NOT_CORRECT 0x22u
#define NRC_REQUEST_OUT_OF_RANGE 0x31u

static uint8 s_tx[EVCU_DCM_TX_BUFFER_SIZE];

void Evcu_DcmInit(void)
{
    (void)memset(s_tx, 0, sizeof(s_tx));
}

static void prv_send(const uint8 *data, PduLengthType len)
{
    PduInfoType info;
    extern volatile uint8 Evcu_TestLastUdsResponse[64];
    extern volatile uint16 Evcu_TestLastUdsResponseLen;

    PduLengthType copy_len = (len <= 64u) ? len : 64u;
    for (PduLengthType i = 0u; i < copy_len; ++i) {
        Evcu_TestLastUdsResponse[i] = data[i];
    }
    Evcu_TestLastUdsResponseLen = copy_len;

    info.SduDataPtr = (uint8*)data;
    info.MetaDataPtr = 0;
    info.SduLength = len;

    Evcu_LogString("[EVCU][UDS] TX LEN=");
    Evcu_LogU16(len);
    Evcu_LogString("\r\n");

    (void)CanTp_Transmit(0u, &info);
}

static void prv_negative(uint8 sid, uint8 nrc)
{
    s_tx[0] = 0x7Fu;
    s_tx[1] = sid;
    s_tx[2] = nrc;
    prv_send(s_tx, 3u);
}

static PduLengthType prv_append_did(uint16 did, uint8 *dst, PduLengthType offset)
{
    const Evcu_State_t *state = Evcu_GetState();
    static const uint8 vin[17] = {
        'E','V','C','U','S','I','M','0','0','0','0','0','0','0','0','0','1'
    };
    static const uint8 sw[8] = { 'S','W','2','0','2','6','0','1' };

    if ((offset + 3u) > EVCU_DCM_TX_BUFFER_SIZE) {
        return 0u;
    }

    dst[offset++] = (uint8)(did >> 8u);
    dst[offset++] = (uint8)did;

    switch (did) {
        case 0xF190u:
            if ((offset + 17u) > EVCU_DCM_TX_BUFFER_SIZE) return 0u;
            (void)memcpy(&dst[offset], vin, 17u);
            return (PduLengthType)(offset + 17u);

        case 0xF189u:
            if ((offset + 8u) > EVCU_DCM_TX_BUFFER_SIZE) return 0u;
            (void)memcpy(&dst[offset], sw, 8u);
            return (PduLengthType)(offset + 8u);

        case 0x010Cu:
            if ((offset + 2u) > EVCU_DCM_TX_BUFFER_SIZE) return 0u;
            dst[offset++] = (uint8)(state->engine_rpm >> 8u);
            dst[offset++] = (uint8)state->engine_rpm;
            return offset;

        case 0x0105u:
            if ((offset + 1u) > EVCU_DCM_TX_BUFFER_SIZE) return 0u;
            dst[offset++] = state->engine_temp;
            return offset;

        case 0xF001u:
            if ((offset + 24u) > EVCU_DCM_TX_BUFFER_SIZE) return 0u;
            dst[offset++] = state->ignition_state;
            dst[offset++] = state->vehicle_speed;
            dst[offset++] = (uint8)(state->engine_rpm >> 8u);
            dst[offset++] = (uint8)state->engine_rpm;
            dst[offset++] = state->engine_temp;
            dst[offset++] = state->battery_voltage_raw;
            dst[offset++] = state->torque_request;
            dst[offset++] = Evcu_GetActiveDtcCount();
            dst[offset++] = state->alive_counter;
            dst[offset++] = state->communication_state;
            dst[offset++] = state->engine_torque_actual;
            dst[offset++] = state->engine_state;
            for (uint8 i = 0u; i < 12u; ++i) {
                dst[offset++] = (uint8)(0xA0u + i);
            }
            return offset;

        default:
            return 0u;
    }
}

static void prv_service_10(const uint8 *req, PduLengthType len)
{
    if (len != 2u) {
        prv_negative(0x10u, NRC_INCORRECT_LENGTH);
        return;
    }

    if (req[1] == 0x01u) {
        Evcu_SetSession(EVCU_SESSION_DEFAULT);
    } else if (req[1] == 0x03u) {
        Evcu_SetSession(EVCU_SESSION_EXTENDED);
    } else {
        prv_negative(0x10u, NRC_SUBFUNCTION_NOT_SUPPORTED);
        return;
    }

    s_tx[0] = 0x50u;
    s_tx[1] = req[1];
    s_tx[2] = 0x00u;
    s_tx[3] = 0x32u;
    s_tx[4] = 0x01u;
    s_tx[5] = 0xF4u;
    prv_send(s_tx, 6u);
}

static void prv_service_22(const uint8 *req, PduLengthType len)
{
    if ((len < 3u) || (((len - 1u) % 2u) != 0u)) {
        prv_negative(0x22u, NRC_INCORRECT_LENGTH);
        return;
    }

    PduLengthType offset = 1u;
    s_tx[0] = 0x62u;

    for (PduLengthType i = 1u; i < len; i = (PduLengthType)(i + 2u)) {
        uint16 did = (uint16)(((uint16)req[i] << 8u) | req[i + 1u]);
        PduLengthType next = prv_append_did(did, s_tx, offset);
        if (next == 0u) {
            prv_negative(0x22u, NRC_REQUEST_OUT_OF_RANGE);
            return;
        }
        offset = next;
    }

    prv_send(s_tx, offset);
}

static void prv_service_19(const uint8 *req, PduLengthType len)
{
    Evcu_Dtc_t dtcs[EVCU_DTC_COUNT_MAX];

    if ((len != 3u) || (req[1] != 0x02u)) {
        prv_negative(0x19u, NRC_SUBFUNCTION_NOT_SUPPORTED);
        return;
    }

    uint8 count = Evcu_CopyActiveDtcs(dtcs, EVCU_DTC_COUNT_MAX);
    PduLengthType offset = 0u;

    s_tx[offset++] = 0x59u;
    s_tx[offset++] = 0x02u;
    s_tx[offset++] = 0xFFu;

    for (uint8 i = 0u; i < count; ++i) {
        s_tx[offset++] = (uint8)(dtcs[i].code >> 16u);
        s_tx[offset++] = (uint8)(dtcs[i].code >> 8u);
        s_tx[offset++] = (uint8)dtcs[i].code;
        s_tx[offset++] = dtcs[i].status;
    }

    prv_send(s_tx, offset);
}

static void prv_service_14(const uint8 *req, PduLengthType len)
{
    if (len != 4u) {
        prv_negative(0x14u, NRC_INCORRECT_LENGTH);
        return;
    }

    if (Evcu_GetSession() != EVCU_SESSION_EXTENDED) {
        prv_negative(0x14u, NRC_CONDITIONS_NOT_CORRECT);
        return;
    }

    uint32 group = ((uint32)req[1] << 16u) | ((uint32)req[2] << 8u) | req[3];
    if (group != 0xFFFFFFu) {
        prv_negative(0x14u, NRC_REQUEST_OUT_OF_RANGE);
        return;
    }

    Evcu_ClearAllDtcs();
    s_tx[0] = 0x54u;
    prv_send(s_tx, 1u);
}

void Evcu_DcmProcessRequest(const uint8 *request, PduLengthType request_len)
{
    if ((request == 0) || (request_len == 0u)) {
        return;
    }

    Evcu_LogString("[EVCU][UDS] RX SID=0x");
    Evcu_LogHex8(request[0]);
    Evcu_LogString(" LEN=");
    Evcu_LogU16(request_len);
    Evcu_LogString("\r\n");

    switch (request[0]) {
        case 0x10u:
            prv_service_10(request, request_len);
            break;
        case 0x22u:
            prv_service_22(request, request_len);
            break;
        case 0x19u:
            prv_service_19(request, request_len);
            break;
        case 0x14u:
            prv_service_14(request, request_len);
            break;
        default:
            prv_negative(request[0], NRC_SERVICE_NOT_SUPPORTED);
            break;
    }
}
