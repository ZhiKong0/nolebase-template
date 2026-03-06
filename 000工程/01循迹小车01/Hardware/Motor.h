#ifndef __MOTOR_H
#define __MOTOR_H

#include "stm32f10x.h"                  // Device header

typedef enum
{
	MOTOR_DIR_FORWARD = 0,
	MOTOR_DIR_BACKWARD
} MotorDir_t;

void Motor_Init(void);

void Motor_Enable(void);
void Motor_Disable(void);

void MotorA_SetDir(MotorDir_t Dir);
void MotorB_SetDir(MotorDir_t Dir);

void MotorA_SetSpeed(uint16_t Speed);
void MotorB_SetSpeed(uint16_t Speed);

#endif
