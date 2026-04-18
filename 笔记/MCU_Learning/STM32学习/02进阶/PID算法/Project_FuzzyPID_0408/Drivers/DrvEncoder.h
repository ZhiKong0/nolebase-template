#ifndef __DRV_ENCODER_H
#define __DRV_ENCODER_H

#include "stm32f10x.h"

#define DRV_ENCODER_DEFAULT_PPR                11u
#define DRV_ENCODER_DEFAULT_RATIO              30u
#define DRV_ENCODER_DEFAULT_QUAD_MULTIPLIER    4u
#define DRV_ENCODER_DEFAULT_MAX_DELTA          120u

typedef struct {
    uint16_t ppr;
    uint16_t ratio;
    uint8_t quadMultiplier;
    int8_t leftSign;
    int8_t rightSign;
    uint16_t maxDeltaPerPeriod;
} DrvEncoderConfig_t;

typedef struct {
    int32_t leftCount;
    int32_t rightCount;
    int16_t leftSpeed;
    int16_t rightSpeed;
    int16_t rawLeftDelta;
    int16_t rawRightDelta;
    uint8_t leftDeltaClamped;
    uint8_t rightDeltaClamped;
    float leftRpm;
    float rightRpm;
} DrvEncoderSample_t;

void DrvEncoder_Init(void);
void DrvEncoder_Reset(void);
void DrvEncoder_SetConfig(const DrvEncoderConfig_t *cfg);
void DrvEncoder_GetConfig(DrvEncoderConfig_t *cfg);
void DrvEncoder_Sample(DrvEncoderSample_t *sample, uint16_t periodMs);
float DrvEncoder_CountToRpm(int32_t countDelta, uint16_t periodMs);

#endif
