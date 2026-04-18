#ifndef __ENCODER_TIMER_H
#define __ENCODER_TIMER_H

#include "stm32f10x.h"

void Encoder_Timer_Init(void);
int16_t Encoder_GetLeft(void);
int16_t Encoder_GetRight(void);
void Encoder_Reset(void);

#endif
