#ifndef __MOTOR_H
#define __MOTOR_H

#include "stm32f10x.h"

typedef enum {
    MOTOR_DIR_FWD = 0,
    MOTOR_DIR_REV = 1
} Motor_Dir_t;

void Motor_Init(void);
void Motor_Enable(void);
void Motor_Disable(void);
void Motor_Stop(void);

void Motor_SetLeft(int16_t speed, Motor_Dir_t dir);
void Motor_SetRight(int16_t speed, Motor_Dir_t dir);
void Motor_SetDiffSpeed(int16_t leftSpeed, int16_t rightSpeed);

#endif
