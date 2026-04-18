#include "DrvEncoder.h"

#define DRV_ENCODER_IC_FILTER                  12u

static DrvEncoderConfig_t s_encoderCfg = {
    DRV_ENCODER_DEFAULT_PPR,
    DRV_ENCODER_DEFAULT_RATIO,
    DRV_ENCODER_DEFAULT_QUAD_MULTIPLIER,
    -1,
    1,
    DRV_ENCODER_DEFAULT_MAX_DELTA
};

static int16_t drv_encoder_abs(int16_t value)
{
    return (value < 0) ? (int16_t)(-value) : value;
}

static int16_t drv_encoder_read_delta_and_clear(TIM_TypeDef *timer)
{
    int16_t delta = (int16_t)TIM_GetCounter(timer);
    TIM_SetCounter(timer, 0u);
    return delta;
}

static uint32_t drv_encoder_counts_per_rev(void)
{
    uint32_t ppr = s_encoderCfg.ppr ? s_encoderCfg.ppr : DRV_ENCODER_DEFAULT_PPR;
    uint32_t ratio = s_encoderCfg.ratio ? s_encoderCfg.ratio : DRV_ENCODER_DEFAULT_RATIO;
    uint32_t quad = s_encoderCfg.quadMultiplier ? s_encoderCfg.quadMultiplier : DRV_ENCODER_DEFAULT_QUAD_MULTIPLIER;
    return ppr * ratio * quad;
}

static float drv_encoder_to_rpm(int16_t countPerPeriod, uint16_t periodMs)
{
    uint32_t countsPerRev;

    if (periodMs == 0u) {
        return 0.0f;
    }

    countsPerRev = drv_encoder_counts_per_rev();
    if (countsPerRev == 0u) {
        return 0.0f;
    }

    return ((float)countPerPeriod / (float)countsPerRev) * (60000.0f / (float)periodMs);
}

void DrvEncoder_Init(void)
{
    GPIO_InitTypeDef gpio;
    TIM_TimeBaseInitTypeDef tb;

    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM2 | RCC_APB1Periph_TIM3, ENABLE);
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);

    gpio.GPIO_Pin = GPIO_Pin_0 | GPIO_Pin_1 | GPIO_Pin_6 | GPIO_Pin_7;
    gpio.GPIO_Mode = GPIO_Mode_IPU;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &gpio);

    tb.TIM_ClockDivision = TIM_CKD_DIV1;
    tb.TIM_CounterMode = TIM_CounterMode_Up;
    tb.TIM_Period = 65535u;
    tb.TIM_Prescaler = 0u;
    tb.TIM_RepetitionCounter = 0u;

    TIM_TimeBaseInit(TIM2, &tb);
    TIM_EncoderInterfaceConfig(TIM2, TIM_EncoderMode_TI12, TIM_ICPolarity_Rising, TIM_ICPolarity_Rising);
    TIM2->CCMR1 = (uint16_t)((TIM2->CCMR1 & (uint16_t)~((uint16_t)(0x0Fu << 4) | (uint16_t)(0x0Fu << 12))) |
                             (uint16_t)((DRV_ENCODER_IC_FILTER & 0x0Fu) << 4) |
                             (uint16_t)((DRV_ENCODER_IC_FILTER & 0x0Fu) << 12));

    TIM_TimeBaseInit(TIM3, &tb);
    TIM_EncoderInterfaceConfig(TIM3, TIM_EncoderMode_TI12, TIM_ICPolarity_Rising, TIM_ICPolarity_Rising);
    TIM3->CCMR1 = (uint16_t)((TIM3->CCMR1 & (uint16_t)~((uint16_t)(0x0Fu << 4) | (uint16_t)(0x0Fu << 12))) |
                             (uint16_t)((DRV_ENCODER_IC_FILTER & 0x0Fu) << 4) |
                             (uint16_t)((DRV_ENCODER_IC_FILTER & 0x0Fu) << 12));

    TIM_Cmd(TIM2, ENABLE);
    TIM_Cmd(TIM3, ENABLE);
    TIM_SetCounter(TIM2, 0u);
    TIM_SetCounter(TIM3, 0u);
}

void DrvEncoder_Reset(void)
{
    TIM_SetCounter(TIM2, 0u);
    TIM_SetCounter(TIM3, 0u);
}

void DrvEncoder_SetConfig(const DrvEncoderConfig_t *cfg)
{
    if (cfg == 0) {
        return;
    }

    if (cfg->ppr > 0u) {
        s_encoderCfg.ppr = cfg->ppr;
    }
    if (cfg->ratio > 0u) {
        s_encoderCfg.ratio = cfg->ratio;
    }
    if (cfg->quadMultiplier > 0u) {
        s_encoderCfg.quadMultiplier = cfg->quadMultiplier;
    }
    s_encoderCfg.leftSign = (cfg->leftSign >= 0) ? 1 : -1;
    s_encoderCfg.rightSign = (cfg->rightSign >= 0) ? 1 : -1;
    s_encoderCfg.maxDeltaPerPeriod = cfg->maxDeltaPerPeriod ? cfg->maxDeltaPerPeriod : DRV_ENCODER_DEFAULT_MAX_DELTA;
}

void DrvEncoder_GetConfig(DrvEncoderConfig_t *cfg)
{
    if (cfg == 0) {
        return;
    }
    *cfg = s_encoderCfg;
}

void DrvEncoder_Sample(DrvEncoderSample_t *sample, uint16_t periodMs)
{
    int16_t deltaLeft;
    int16_t deltaRight;

    if (sample == 0) {
        return;
    }

    deltaLeft = drv_encoder_read_delta_and_clear(TIM2);
    deltaRight = drv_encoder_read_delta_and_clear(TIM3);

    deltaLeft = (int16_t)(deltaLeft * s_encoderCfg.leftSign);
    deltaRight = (int16_t)(deltaRight * s_encoderCfg.rightSign);

    sample->rawLeftDelta = deltaLeft;
    sample->rawRightDelta = deltaRight;
    sample->leftDeltaClamped = 0u;
    sample->rightDeltaClamped = 0u;

    if (drv_encoder_abs(deltaLeft) > (int16_t)s_encoderCfg.maxDeltaPerPeriod) {
        deltaLeft = 0;
        sample->leftDeltaClamped = 1u;
    }
    if (drv_encoder_abs(deltaRight) > (int16_t)s_encoderCfg.maxDeltaPerPeriod) {
        deltaRight = 0;
        sample->rightDeltaClamped = 1u;
    }

    sample->leftSpeed = deltaLeft;
    sample->rightSpeed = deltaRight;
    sample->leftCount += deltaLeft;
    sample->rightCount += deltaRight;
    sample->leftRpm = drv_encoder_to_rpm(deltaLeft, periodMs);
    sample->rightRpm = drv_encoder_to_rpm(deltaRight, periodMs);
}

float DrvEncoder_CountToRpm(int32_t countDelta, uint16_t periodMs)
{
    return drv_encoder_to_rpm((int16_t)countDelta, periodMs);
}
