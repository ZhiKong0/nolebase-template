#ifndef __DRV_MOTOR_H
#define __DRV_MOTOR_H

#include "stm32f10x.h"

void DrvMotor_Init(void);
void DrvMotor_SetEnabled(uint8_t enabled);
void DrvMotor_Stop(void);
void DrvMotor_Apply(int16_t leftPwm, int16_t rightPwm);

#endif
