#ifndef __CONTROL_H
#define __CONTROL_H

#include "stm32f10x.h"

typedef struct
{
    volatile uint8_t running;
    volatile uint8_t speedLevel;
    volatile uint8_t sensorBits;
    volatile float trackError;
    volatile float targetLeft;
    volatile float targetRight;
    volatile float actualLeft;
    volatile float actualRight;
    volatile float outLeft;
    volatile float outRight;
    volatile uint32_t tickMs;
} ControlTelemetry_t;

void Control_Init(void);
void Control_Tick(void);
void Control_TimerIRQHandler(void);
const ControlTelemetry_t *Control_GetTelemetry(void);

#endif
