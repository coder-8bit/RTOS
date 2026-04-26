#ifndef EVCU_TEST_H
#define EVCU_TEST_H

#include "Std_Types.h"

extern volatile uint8 Evcu_TestUdsRequest[64];
extern volatile uint16 Evcu_TestUdsRequestLen;
extern volatile uint8 Evcu_TestLastUdsResponse[64];
extern volatile uint16 Evcu_TestLastUdsResponseLen;
extern volatile uint8 Evcu_TestComEngineStatus[8];

void Evcu_TestInit(void);
uint16 Evcu_TestRunUdsRequest(void);
void Evcu_TestInjectEngineStatus(void);
uint16 Evcu_TestInjectEngineStatus3000(void);
uint16 Evcu_TestGetEngineRpm(void);
uint8 Evcu_TestGetEngineTemp(void);

#endif /* EVCU_TEST_H */
