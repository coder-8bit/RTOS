#include "Dcm.h"
#include "Evcu_Dcm.h"
#include "Evcu_Log.h"

#include <string.h>

#define DCM_RX_BUFFER_SIZE 256u

static uint8 Dcm_RxBuffer[DCM_RX_BUFFER_SIZE];
static PduLengthType Dcm_RxLength = 0u;
static boolean Dcm_IsReceiving = FALSE;

void Dcm_Init(void)
{
    Dcm_RxLength = 0u;
    Dcm_IsReceiving = FALSE;
    (void)memset(Dcm_RxBuffer, 0, sizeof(Dcm_RxBuffer));
    Evcu_DcmInit();
    Evcu_LogLine("[DCM] Init");
}

BufReq_ReturnType Dcm_StartOfReception(
    PduIdType id,
    const PduInfoType* info,
    PduLengthType TpSduLength,
    PduLengthType* bufferSizePtr)
{
    (void)id;
    (void)info;

    if (TpSduLength > DCM_RX_BUFFER_SIZE) {
        return BUFREQ_E_OVFL;
    }

    Dcm_RxLength = 0u;
    Dcm_IsReceiving = TRUE;
    (void)memset(Dcm_RxBuffer, 0, sizeof(Dcm_RxBuffer));

    if (bufferSizePtr != 0) {
        *bufferSizePtr = DCM_RX_BUFFER_SIZE;
    }
    return BUFREQ_OK;
}

BufReq_ReturnType Dcm_CopyRxData(
    PduIdType id,
    const PduInfoType* info,
    PduLengthType* bufferSizePtr)
{
    (void)id;

    if ((Dcm_IsReceiving == FALSE) || (info == 0) || (info->SduDataPtr == 0)) {
        return BUFREQ_E_NOT_OK;
    }

    if ((Dcm_RxLength + info->SduLength) > DCM_RX_BUFFER_SIZE) {
        return BUFREQ_E_OVFL;
    }

    (void)memcpy(&Dcm_RxBuffer[Dcm_RxLength], info->SduDataPtr, info->SduLength);
    Dcm_RxLength = (PduLengthType)(Dcm_RxLength + info->SduLength);

    if (bufferSizePtr != 0) {
        *bufferSizePtr = (PduLengthType)(DCM_RX_BUFFER_SIZE - Dcm_RxLength);
    }
    return BUFREQ_OK;
}

void Dcm_TpRxIndication(PduIdType id, NotifResultType result)
{
    (void)id;

    if ((result == NTFRSLT_OK) && (Dcm_IsReceiving != FALSE)) {
        Evcu_DcmProcessRequest(Dcm_RxBuffer, Dcm_RxLength);
    } else {
        Evcu_LogLine("[DCM] RX error");
    }

    Dcm_IsReceiving = FALSE;
}
