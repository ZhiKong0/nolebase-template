#ifndef __MOTOR_DRIVER_H
#define __MOTOR_DRIVER_H

#include "stm32f10x.h"

typedef struct {
    int32_t leftCount;
    int32_t rightCount;
    int16_t leftSpeed;
    int16_t rightSpeed;
    int16_t rawLeftDelta;
    int16_t rawRightDelta;
    float   filteredLeftSpeed;
    float   filteredRightSpeed;
    uint8_t leftClamped;
    uint8_t rightClamped;
} Encoder_Data_t;

void MotorDriver_Init(void);
void MotorDriver_Enable(void);
void MotorDriver_Disable(void);
void MotorDriver_Stop(void);
void MotorDriver_SetDiffPWM(int16_t left, int16_t right, int16_t dPostDZ);

void Encoder_Init(void);
void Encoder_Reset(void);
void Encoder_Update(Encoder_Data_t *data);

#endif
