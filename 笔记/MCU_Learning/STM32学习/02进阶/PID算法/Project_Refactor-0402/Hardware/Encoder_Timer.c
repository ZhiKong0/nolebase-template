#include "stm32f10x.h"
#include "Encoder_Timer.h"

static Encoder_Config_t g_encoderCfg = {
    ENCODER_DEFAULT_PPR,
    ENCODER_DEFAULT_RATIO,
    ENCODER_DEFAULT_QUAD_MULTIPLIER,
    -1,
    1,
    ENCODER_DEFAULT_MAX_DELTA_PER_PERIOD
};

#define ENCODER_IC_FILTER   12

static int16_t abs_i16(int16_t v) {
    return (v < 0) ? (int16_t)(-v) : v;
}

static uint32_t Encoder_GetCountsPerOutputRev(void) {
    uint32_t ppr = g_encoderCfg.ppr ? g_encoderCfg.ppr : ENCODER_DEFAULT_PPR;
    uint32_t ratio = g_encoderCfg.ratio ? g_encoderCfg.ratio : ENCODER_DEFAULT_RATIO;
    uint32_t quad = g_encoderCfg.quadMultiplier ? g_encoderCfg.quadMultiplier : ENCODER_DEFAULT_QUAD_MULTIPLIER;
    return ppr * ratio * quad;
}

static int16_t Encoder_ReadDeltaAndClear(TIM_TypeDef *tim) {
    int16_t d = (int16_t)TIM_GetCounter(tim);
    TIM_SetCounter(tim, 0);
    return d;
}

void Encoder_Timer_Init(void) {
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM2, ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM3, ENABLE);
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);

    GPIO_InitTypeDef g;
    g.GPIO_Mode = GPIO_Mode_IPU;
    g.GPIO_Speed = GPIO_Speed_50MHz;
    g.GPIO_Pin = GPIO_Pin_0 | GPIO_Pin_1 | GPIO_Pin_6 | GPIO_Pin_7;
    GPIO_Init(GPIOA, &g);

    TIM_TimeBaseInitTypeDef tb;
    tb.TIM_ClockDivision = TIM_CKD_DIV1;
    tb.TIM_CounterMode = TIM_CounterMode_Up;
    tb.TIM_Period = 65535;
    tb.TIM_Prescaler = 0;
    tb.TIM_RepetitionCounter = 0;

    TIM_TimeBaseInit(TIM2, &tb);
    TIM_EncoderInterfaceConfig(TIM2, TIM_EncoderMode_TI12, TIM_ICPolarity_Rising, TIM_ICPolarity_Rising);
    TIM2->CCMR1 = (uint16_t)((TIM2->CCMR1 & (uint16_t)~((uint16_t)(0x0F << 4) | (uint16_t)(0x0F << 12))) |
                             (uint16_t)((ENCODER_IC_FILTER & 0x0F) << 4) |
                             (uint16_t)((ENCODER_IC_FILTER & 0x0F) << 12));

    TIM_TimeBaseInit(TIM3, &tb);
    TIM_EncoderInterfaceConfig(TIM3, TIM_EncoderMode_TI12, TIM_ICPolarity_Rising, TIM_ICPolarity_Rising);
    TIM3->CCMR1 = (uint16_t)((TIM3->CCMR1 & (uint16_t)~((uint16_t)(0x0F << 4) | (uint16_t)(0x0F << 12))) |
                             (uint16_t)((ENCODER_IC_FILTER & 0x0F) << 4) |
                             (uint16_t)((ENCODER_IC_FILTER & 0x0F) << 12));

    TIM_Cmd(TIM2, ENABLE);
    TIM_Cmd(TIM3, ENABLE);
    TIM_SetCounter(TIM2, 0);
    TIM_SetCounter(TIM3, 0);
}

void Encoder_SetConfig(const Encoder_Config_t *cfg) {
    if (!cfg) return;

    if (cfg->ppr > 0u) g_encoderCfg.ppr = cfg->ppr;
    if (cfg->ratio > 0u) g_encoderCfg.ratio = cfg->ratio;
    if (cfg->quadMultiplier > 0u) g_encoderCfg.quadMultiplier = cfg->quadMultiplier;
    g_encoderCfg.leftSign = (cfg->leftSign >= 0) ? 1 : -1;
    g_encoderCfg.rightSign = (cfg->rightSign >= 0) ? 1 : -1;
    g_encoderCfg.maxDeltaPerPeriod = cfg->maxDeltaPerPeriod ? cfg->maxDeltaPerPeriod : ENCODER_DEFAULT_MAX_DELTA_PER_PERIOD;
}

void Encoder_GetConfig(Encoder_Config_t *cfg) {
    if (!cfg) return;
    *cfg = g_encoderCfg;
}

int16_t Encoder_GetLeft(void) {
    return (int16_t)TIM_GetCounter(TIM2);
}

int16_t Encoder_GetRight(void) {
    return (int16_t)TIM_GetCounter(TIM3);
}

void Encoder_SetLeftSign(int8_t sign) {
    g_encoderCfg.leftSign = (sign >= 0) ? 1 : -1;
}

void Encoder_SetRightSign(int8_t sign) {
    g_encoderCfg.rightSign = (sign >= 0) ? 1 : -1;
}

int8_t Encoder_GetLeftSign(void) {
    return g_encoderCfg.leftSign;
}

int8_t Encoder_GetRightSign(void) {
    return g_encoderCfg.rightSign;
}

void Encoder_SetMaxDeltaPerPeriod(uint16_t maxDeltaPerPeriod) {
    g_encoderCfg.maxDeltaPerPeriod = maxDeltaPerPeriod ? maxDeltaPerPeriod : ENCODER_DEFAULT_MAX_DELTA_PER_PERIOD;
}

uint16_t Encoder_GetMaxDeltaPerPeriod(void) {
    return g_encoderCfg.maxDeltaPerPeriod;
}

void Encoder_UpdateSpeed(Encoder_Data_t *data, uint16_t periodMs) {
    int16_t dl = Encoder_ReadDeltaAndClear(TIM2);
    int16_t dr = Encoder_ReadDeltaAndClear(TIM3);

    dl = (int16_t)(dl * g_encoderCfg.leftSign);
    dr = (int16_t)(dr * g_encoderCfg.rightSign);

    data->rawLeftDelta = dl;
    data->rawRightDelta = dr;
    data->leftDeltaClamped = 0;
    data->rightDeltaClamped = 0;

    if (abs_i16(dl) > (int16_t)g_encoderCfg.maxDeltaPerPeriod) {
        dl = 0;
        data->leftDeltaClamped = 1;
    }
    if (abs_i16(dr) > (int16_t)g_encoderCfg.maxDeltaPerPeriod) {
        dr = 0;
        data->rightDeltaClamped = 1;
    }

    data->leftSpeed = dl;
    data->rightSpeed = dr;

    data->leftCount += data->leftSpeed;
    data->rightCount += data->rightSpeed;

    data->leftRPM = Encoder_CountToRPM(data->leftSpeed, periodMs);
    data->rightRPM = Encoder_CountToRPM(data->rightSpeed, periodMs);
}

void Encoder_Reset(void) {
    TIM_SetCounter(TIM2, 0);
    TIM_SetCounter(TIM3, 0);
}

int16_t Encoder_GetSpeedDiff(void) {
    int16_t left = (int16_t)(Encoder_GetLeft() * g_encoderCfg.leftSign);
    int16_t right = (int16_t)(Encoder_GetRight() * g_encoderCfg.rightSign);
    return left - right;
}

float Encoder_CountToRPM(int16_t countPerPeriod, uint16_t periodMs) {
    uint32_t countsPerRev;

    if (periodMs == 0) return 0.0f;
    countsPerRev = Encoder_GetCountsPerOutputRev();
    if (countsPerRev == 0u) return 0.0f;
    return (float)countPerPeriod / (float)countsPerRev * (60000.0f / periodMs);
}
