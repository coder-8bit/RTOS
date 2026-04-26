#ifndef EVCU_COM_H
#define EVCU_COM_H

#include "Std_Types.h"
#include "ComStack_Types.h"

void Evcu_ComInit(void);
void Evcu_ComMainFunction(void);
void Evcu_ComOnRxPdu(PduIdType pdu_id);

#endif /* EVCU_COM_H */
