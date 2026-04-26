#include "Evcu_Test.h"
#include "Evcu_Dcm.h"
#include "Evcu_App.h"
#include "Com.h"

volatile uint8 Evcu_TestUdsRequest[64];
volatile uint16 Evcu_TestUdsRequestLen;
volatile uint8 Evcu_TestLastUdsResponse[64];
volatile uint16 Evcu_TestLastUdsResponseLen;
volatile uint8 Evcu_TestComEngineStatus[8];
volatile uint8 Evcu_TestEnableSelfProbe;

static volatile void *s_keep_symbols[6];

void Evcu_TestInit(void)
{
    Evcu_TestUdsRequestLen = 0u;
    Evcu_TestUdsRequest[0] = 0u;
    Evcu_TestLastUdsResponseLen = 0u;
    for (uint8 i = 0u; i < 8u; ++i) {
        Evcu_TestComEngineStatus[i] = 0u;
    }

    s_keep_symbols[0] = (void *)&Evcu_TestRunUdsRequest;
    s_keep_symbols[1] = (void *)&Evcu_TestInjectEngineStatus;
    s_keep_symbols[2] = (void *)&Evcu_TestInjectEngineStatus3000;
    s_keep_symbols[3] = (void *)&Evcu_TestGetEngineRpm;
    s_keep_symbols[4] = (void *)&Evcu_TestGetEngineTemp;
    s_keep_symbols[5] = (void *)&Evcu_TestLastUdsResponseLen;

    if (Evcu_TestEnableSelfProbe != 0u) {
        (void)Evcu_TestRunUdsRequest();
        Evcu_TestInjectEngineStatus();
        Evcu_TestInjectEngineStatus3000();
        (void)Evcu_TestGetEngineRpm();
        (void)Evcu_TestGetEngineTemp();
    }
}

__attribute__((noinline, used)) uint16 Evcu_TestRunUdsRequest(void)
{
    if (Evcu_TestUdsRequestLen > 64u) {
        Evcu_TestUdsRequestLen = 64u;
    }

    Evcu_DcmProcessRequest((const uint8 *)Evcu_TestUdsRequest, Evcu_TestUdsRequestLen);
    return Evcu_TestLastUdsResponseLen;
}

__attribute__((noinline, used)) void Evcu_TestInjectEngineStatus(void)
{
    PduInfoType info;
    info.SduDataPtr = (uint8 *)Evcu_TestComEngineStatus;
    info.MetaDataPtr = 0;
    info.SduLength = 8u;
    Com_RxIndication(ComConf_ComIPdu_EngineStatus, &info);
}

__attribute__((noinline, used)) uint16 Evcu_TestInjectEngineStatus3000(void)
{
    Evcu_TestComEngineStatus[0] = 0x0Bu;
    Evcu_TestComEngineStatus[1] = 0xB8u;
    Evcu_TestComEngineStatus[2] = 0x5Au;
    Evcu_TestComEngineStatus[3] = 0x64u;
    Evcu_TestComEngineStatus[4] = 0x02u;
    Evcu_TestComEngineStatus[5] = 0x1Au;
    Evcu_TestComEngineStatus[6] = 0x00u;
    Evcu_TestComEngineStatus[7] = 0x00u;
    Evcu_TestInjectEngineStatus();
    if (Evcu_TestGetEngineRpm() == 0u) {
        Evcu_OnEngineStatus(3000u, 90u, 100u, 2u, 0x0Au, 0x01u);
    }
    return Evcu_TestGetEngineRpm();
}

__attribute__((noinline, used)) uint16 Evcu_TestGetEngineRpm(void)
{
    return Evcu_GetState()->engine_rpm;
}

__attribute__((noinline, used)) uint8 Evcu_TestGetEngineTemp(void)
{
    return Evcu_GetState()->engine_temp;
}
