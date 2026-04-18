#include "Encoder_Timer.h"

void Encoder_Timer_Init(void)
{
    GPIO_InitTypeDef gpio_init;
    TIM_TimeBaseInitTypeDef tim_base_init;
    TIM_ICInitTypeDef tim_ic_init;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM2 | RCC_APB1Periph_TIM3, ENABLE);

    gpio_init.GPIO_Pin = GPIO_Pin_0 | GPIO_Pin_1 | GPIO_Pin_6 | GPIO_Pin_7;
    gpio_init.GPIO_Mode = GPIO_Mode_IPU;
    gpio_init.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &gpio_init);

    tim_base_init.TIM_Prescaler = 0U;
    tim_base_init.TIM_CounterMode = TIM_CounterMode_Up;
    tim_base_init.TIM_Period = 0xFFFFU;
    tim_base_init.TIM_ClockDivision = TIM_CKD_DIV1;
    tim_base_init.TIM_RepetitionCounter = 0U;

    TIM_TimeBaseInit(TIM2, &tim_base_init);
    TIM_TimeBaseInit(TIM3, &tim_base_init);

    TIM_EncoderInterfaceConfig(TIM2, TIM_EncoderMode_TI12, TIM_ICPolarity_Rising, TIM_ICPolarity_Rising);
    TIM_EncoderInterfaceConfig(TIM3, TIM_EncoderMode_TI12, TIM_ICPolarity_Rising, TIM_ICPolarity_Rising);

    TIM_ICStructInit(&tim_ic_init);
    tim_ic_init.TIM_ICPolarity = TIM_ICPolarity_Rising;
    tim_ic_init.TIM_ICSelection = TIM_ICSelection_DirectTI;
    tim_ic_init.TIM_ICPrescaler = TIM_ICPSC_DIV1;
    tim_ic_init.TIM_ICFilter = 0x0FU;

    tim_ic_init.TIM_Channel = TIM_Channel_1;
    TIM_ICInit(TIM2, &tim_ic_init);
    TIM_ICInit(TIM3, &tim_ic_init);

    tim_ic_init.TIM_Channel = TIM_Channel_2;
    TIM_ICInit(TIM2, &tim_ic_init);
    TIM_ICInit(TIM3, &tim_ic_init);

    TIM_SetCounter(TIM2, 0U);
    TIM_SetCounter(TIM3, 0U);
    TIM_Cmd(TIM2, ENABLE);
    TIM_Cmd(TIM3, ENABLE);
}

int16_t Encoder_GetLeft(void)
{
    int16_t count = (int16_t)TIM_GetCounter(TIM2);
    TIM_SetCounter(TIM2, 0U);
    return count;
}

int16_t Encoder_GetRight(void)
{
    int16_t count = (int16_t)TIM_GetCounter(TIM3);
    TIM_SetCounter(TIM3, 0U);
    return count;
}

void Encoder_Reset(void)
{
    TIM_SetCounter(TIM2, 0U);
    TIM_SetCounter(TIM3, 0U);
}
