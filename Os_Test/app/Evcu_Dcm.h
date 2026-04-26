#ifndef EVCU_DCM_H
#define EVCU_DCM_H

#include "Std_Types.h"
#include "ComStack_Types.h"

void Evcu_DcmInit(void);
void Evcu_DcmProcessRequest(const uint8 *request, PduLengthType request_len);

#endif /* EVCU_DCM_H */
