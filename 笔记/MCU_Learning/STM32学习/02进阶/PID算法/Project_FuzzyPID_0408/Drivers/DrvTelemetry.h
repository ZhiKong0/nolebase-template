#ifndef __DRV_TELEMETRY_H
#define __DRV_TELEMETRY_H

#include "stm32f10x.h"

void DrvTelemetry_Init(void);
void DrvTelemetry_SendText(const char *text);
void DrvTelemetry_SendFloat3(float ch0, float ch1, float ch2);
void DrvTelemetry_SendFloat5(float ch0, float ch1, float ch2, float ch3, float ch4);
uint8_t DrvTelemetry_TryReadCommand(char *out, uint16_t outSize);

#endif
