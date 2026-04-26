#include "PduR.h"
#include "PduR_Cfg.h"
#include "PduR_Com.h"
#include "PduR_CanIf.h"
#include "PduR_CanTp.h"
#include "CanIf_Cfg.h"
#include "Dcm.h"

#include <stddef.h>

extern Std_ReturnType CanIf_Transmit(PduIdType CanIfTxPduId, const PduInfoType* PduInfoPtr);
extern Std_ReturnType CanTp_Transmit(PduIdType CanTpTxSduId, const PduInfoType* CanTpTxInfoPtr);
extern void CanTp_TxConfirmation(PduIdType CanTpTxPduId);

extern void Com_RxIndication(PduIdType ComRxPduId, const PduInfoType* PduInfoPtr);
extern void Com_TxConfirmation(PduIdType ComTxPduId);
extern Std_ReturnType Com_TriggerTransmit(PduIdType ComTxPduId, PduInfoType* PduInfoPtr);

static PduR_StateType s_State = PDUR_UNINIT;
static const PduR_PBConfigType* s_Cfg = NULL;
static boolean s_RoutingEnabled = FALSE;

static int32 prv_find_com_tx_route(PduIdType src)
{
    const PduR_Route1to1Type* tbl = (const PduR_Route1to1Type*)s_Cfg->ComTxRoutingTable;
    for (uint16 i = 0u; i < PDUR_NUM_COM_TX_ROUTES; ++i) {
        if (tbl[i].SrcPduId == src) {
            return (int32)i;
        }
    }
    return -1;
}

static int32 prv_find_callback_route(const PduR_CallbackRouteType* tbl, uint16 n, PduIdType src)
{
    for (uint16 i = 0u; i < n; ++i) {
        if (tbl[i].SrcPduId == src) {
            return (int32)i;
        }
    }
    return -1;
}

void PduR_Init(const PduR_PBConfigType* ConfigPtr)
{
    if (ConfigPtr == NULL) {
        s_State = PDUR_UNINIT;
        s_Cfg = NULL;
        s_RoutingEnabled = FALSE;
        return;
    }

    s_Cfg = ConfigPtr;
    s_State = PDUR_ONLINE;
    s_RoutingEnabled = TRUE;
}

Std_ReturnType PduR_EnableRouting(PduR_RoutingPathGroupIdType id)
{
    (void)id;
    if (s_State == PDUR_UNINIT) {
        return E_NOT_OK;
    }
    s_RoutingEnabled = TRUE;
    return E_OK;
}

Std_ReturnType PduR_DisableRouting(PduR_RoutingPathGroupIdType id)
{
    (void)id;
    if (s_State == PDUR_UNINIT) {
        return E_NOT_OK;
    }
    s_RoutingEnabled = FALSE;
    return E_OK;
}

PduR_StateType PduR_GetState(void)
{
    return s_State;
}

Std_ReturnType PduR_ComTransmit(PduIdType ComTxPduId, const PduInfoType* PduInfoPtr)
{
    if ((s_State == PDUR_UNINIT) || !s_RoutingEnabled ||
        (s_Cfg == NULL) || (s_Cfg->ComTxRoutingTable == NULL) ||
        (PduInfoPtr == NULL) || (PduInfoPtr->SduDataPtr == NULL)) {
        return E_NOT_OK;
    }

    int32 idx = prv_find_com_tx_route(ComTxPduId);
    if (idx < 0) {
        return E_NOT_OK;
    }

    const PduR_Route1to1Type* tbl = (const PduR_Route1to1Type*)s_Cfg->ComTxRoutingTable;
    const PduR_Route1to1Type* route = &tbl[(uint16)idx];

    if (route->DestModule == PDUR_DEST_CANIF) {
        return CanIf_Transmit(route->DstPduId, PduInfoPtr);
    }
    if (route->DestModule == PDUR_DEST_CANTP) {
        return CanTp_Transmit(route->DstPduId, PduInfoPtr);
    }

    return E_NOT_OK;
}

void PduR_CanIfRxIndication(PduIdType CanIfRxPduId, const PduInfoType* PduInfoPtr)
{
    if ((s_State == PDUR_UNINIT) || !s_RoutingEnabled ||
        (s_Cfg == NULL) || (s_Cfg->CanIfRxRoutingTable == NULL)) {
        return;
    }

    int32 idx = prv_find_callback_route((const PduR_CallbackRouteType*)s_Cfg->CanIfRxRoutingTable,
                                        PDUR_NUM_CANIF_RX_ROUTES,
                                        CanIfRxPduId);
    if (idx >= 0) {
        const PduR_CallbackRouteType* tbl = (const PduR_CallbackRouteType*)s_Cfg->CanIfRxRoutingTable;
        Com_RxIndication(tbl[(uint16)idx].DstPduId, PduInfoPtr);
    }
}

void PduR_CanIfTxConfirmation(PduIdType CanIfTxPduId)
{
    if ((s_State == PDUR_UNINIT) || !s_RoutingEnabled) {
        return;
    }

    if ((CanIfTxPduId == CanIfConf_Pdu_DiagResp) || (CanIfTxPduId == CanIfConf_Pdu_DiagFc)) {
        CanTp_TxConfirmation(0u);
        return;
    }

    int32 idx = prv_find_callback_route(PduR_CanIfTxConfRoutes,
                                        PDUR_NUM_CANIF_TXCONF_ROUTES,
                                        CanIfTxPduId);
    if (idx >= 0) {
        Com_TxConfirmation(PduR_CanIfTxConfRoutes[(uint16)idx].DstPduId);
    }
}

Std_ReturnType PduR_CanIfTriggerTransmit(PduIdType CanIfTxPduId, PduInfoType* PduInfoPtr)
{
    if ((s_State == PDUR_UNINIT) || !s_RoutingEnabled ||
        (PduInfoPtr == NULL) || (PduInfoPtr->SduDataPtr == NULL)) {
        return E_NOT_OK;
    }

    int32 idx = prv_find_callback_route(PduR_CanIfTrigTxRoutes,
                                        PDUR_NUM_CANIF_TRIGTX_ROUTES,
                                        CanIfTxPduId);
    if (idx < 0) {
        return E_NOT_OK;
    }

    return Com_TriggerTransmit(PduR_CanIfTrigTxRoutes[(uint16)idx].DstPduId, PduInfoPtr);
}

void PduR_CanTpRxIndication(PduIdType RxPduId, NotifResultType Result)
{
    Dcm_TpRxIndication(RxPduId, Result);
}

void PduR_CanTpTxConfirmation(PduIdType TxPduId, NotifResultType Result)
{
    (void)TxPduId;
    (void)Result;
}

BufReq_ReturnType PduR_CanTpStartOfReception(
    PduIdType id,
    const PduInfoType* info,
    PduLengthType TpSduLength,
    PduLengthType* bufferSizePtr)
{
    return Dcm_StartOfReception(id, info, TpSduLength, bufferSizePtr);
}

BufReq_ReturnType PduR_CanTpCopyRxData(
    PduIdType id,
    const PduInfoType* info,
    PduLengthType* bufferSizePtr)
{
    return Dcm_CopyRxData(id, info, bufferSizePtr);
}
