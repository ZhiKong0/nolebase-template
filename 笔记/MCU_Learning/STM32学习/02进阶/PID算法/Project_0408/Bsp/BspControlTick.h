#ifndef __BSP_CONTROL_TICK_H
#define __BSP_CONTROL_TICK_H

#include "stm32f10x.h"

void BspControlTick_Init(void);
void BspControlTick_IrqHandler(void);
uint8_t BspControlTick_ConsumeOneTick(void);
uint32_t BspControlTick_GetMillis(void);

#endif
