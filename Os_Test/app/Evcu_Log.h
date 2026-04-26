#ifndef EVCU_LOG_H
#define EVCU_LOG_H

#include "Std_Types.h"

void Evcu_LogInit(void);
void Evcu_LogString(const char *str);
void Evcu_LogHex8(uint8 value);
void Evcu_LogU16(uint16 value);
void Evcu_LogLine(const char *str);

void Log_Print(const char *str);

#endif /* EVCU_LOG_H */
