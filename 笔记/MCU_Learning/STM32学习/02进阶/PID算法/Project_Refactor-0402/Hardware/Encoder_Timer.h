#ifndef __ENCODER_TIMER_H
#define __ENCODER_TIMER_H

#include "stm32f10x.h"

#define ENCODER_DEFAULT_PPR 11
#define ENCODER_DEFAULT_RATIO 30
#define ENCODER_DEFAULT_QUAD_MULTIPLIER 4
#define ENCODER_DEFAULT_MAX_DELTA_PER_PERIOD 2600
#define ENCODER_MAX_DELTA_PER_PERIOD ENCODER_DEFAULT_MAX_DELTA_PER_PERIOD

typedef struct {
    uint16_t ppr;
    uint16_t ratio;
    uint8_t quadMultiplier;
    int8_t leftSign;
    int8_t rightSign;
    uint16_t maxDeltaPerPeriod;
} Encoder_Config_t;

typedef struct {
    int32_t leftCount;
    int32_t rightCount;
    int16_t leftSpeed;
    int16_t rightSpeed;
    int16_t rawLeftDelta;
    int16_t rawRightDelta;
    uint8_t leftDeltaClamped;
    uint8_t rightDeltaClamped;
    float leftRPM;
    float rightRPM;
} Encoder_Data_t;

void Encoder_Timer_Init(void);
int16_t Encoder_GetLeft(void);
int16_t Encoder_GetRight(void);
void Encoder_SetConfig(const Encoder_Config_t *cfg);
void Encoder_GetConfig(Encoder_Config_t *cfg);
void Encoder_UpdateSpeed(Encoder_Data_t *data, uint16_t periodMs);
void Encoder_Reset(void);
int16_t Encoder_GetSpeedDiff(void);
float Encoder_CountToRPM(int16_t countPerPeriod, uint16_t periodMs);

void Encoder_SetMaxDeltaPerPeriod(uint16_t maxDeltaPerPeriod);
uint16_t Encoder_GetMaxDeltaPerPeriod(void);
void Encoder_SetLeftSign(int8_t sign);
void Encoder_SetRightSign(int8_t sign);
int8_t Encoder_GetLeftSign(void);
int8_t Encoder_GetRightSign(void);

#endif
