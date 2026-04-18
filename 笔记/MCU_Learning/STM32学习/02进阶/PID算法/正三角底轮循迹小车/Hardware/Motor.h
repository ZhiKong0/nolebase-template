#ifndef __MOTOR_H
#define __MOTOR_H

#include "stm32f10x.h"

void Motor_Init(void);
void Motor_Set(int16_t left_speed, int16_t right_speed);
void Motor_Stop(void);

#endif