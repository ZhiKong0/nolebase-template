#include "Motor.h"

static uint16_t Motor_ClampCompare(int32_t compare)
{
    if (compare <= 0)
    {
        return 0U;
    }

    if (compare > 999)
    {
        return 999U;
    }

    return (uint16_t)compare;
}

void Motor_Init(void)
{
    GPIO_InitTypeDef gpio_init;
    TIM_TimeBaseInitTypeDef tim_base_init;
    TIM_OCInitTypeDef tim_oc_init;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_AFIO | RCC_APB2Periph_GPIOA |
                           RCC_APB2Periph_GPIOB | RCC_APB2Periph_TIM1,
                           ENABLE);

    gpio_init.GPIO_Speed = GPIO_Speed_50MHz;

    gpio_init.GPIO_Mode = GPIO_Mode_Out_PP;
    gpio_init.GPIO_Pin = GPIO_Pin_4 | GPIO_Pin_5;
    GPIO_Init(GPIOA, &gpio_init);

    gpio_init.GPIO_Pin = GPIO_Pin_0 | GPIO_Pin_1 | GPIO_Pin_10;
    GPIO_Init(GPIOB, &gpio_init);

    gpio_init.GPIO_Mode = GPIO_Mode_AF_PP;
    gpio_init.GPIO_Pin = GPIO_Pin_8 | GPIO_Pin_9;
    GPIO_Init(GPIOA, &gpio_init);

    TIM_DeInit(TIM1);
    tim_base_init.TIM_Prescaler = 71U;
    tim_base_init.TIM_CounterMode = TIM_CounterMode_Up;
    tim_base_init.TIM_Period = 999U;
    tim_base_init.TIM_ClockDivision = TIM_CKD_DIV1;
    tim_base_init.TIM_RepetitionCounter = 0U;
    TIM_TimeBaseInit(TIM1, &tim_base_init);

    TIM_OCStructInit(&tim_oc_init);
    tim_oc_init.TIM_OCMode = TIM_OCMode_PWM1;
    tim_oc_init.TIM_OutputState = TIM_OutputState_Enable;
    tim_oc_init.TIM_OutputNState = TIM_OutputNState_Disable;
    tim_oc_init.TIM_Pulse = 0U;
    tim_oc_init.TIM_OCPolarity = TIM_OCPolarity_High;
    tim_oc_init.TIM_OCNPolarity = TIM_OCNPolarity_High;
    tim_oc_init.TIM_OCIdleState = TIM_OCIdleState_Reset;
    tim_oc_init.TIM_OCNIdleState = TIM_OCNIdleState_Reset;
    TIM_OC1Init(TIM1, &tim_oc_init);
    TIM_OC2Init(TIM1, &tim_oc_init);
    TIM_OC1PreloadConfig(TIM1, TIM_OCPreload_Enable);
    TIM_OC2PreloadConfig(TIM1, TIM_OCPreload_Enable);
    TIM_ARRPreloadConfig(TIM1, ENABLE);
    TIM_CtrlPWMOutputs(TIM1, ENABLE);
    TIM_Cmd(TIM1, ENABLE);

    Motor_Stop();
}

void Motor_SetLeft(int16_t speed)
{
    uint16_t compare = Motor_ClampCompare((speed < 0) ? -speed : speed);

    GPIO_SetBits(GPIOB, GPIO_Pin_0);

    if (speed > 0)
    {
        GPIO_SetBits(GPIOA, GPIO_Pin_4);
        GPIO_ResetBits(GPIOA, GPIO_Pin_5);
    }
    else if (speed < 0)
    {
        GPIO_ResetBits(GPIOA, GPIO_Pin_4);
        GPIO_SetBits(GPIOA, GPIO_Pin_5);
    }
    else
    {
        GPIO_ResetBits(GPIOA, GPIO_Pin_4 | GPIO_Pin_5);
    }

    TIM_SetCompare1(TIM1, compare);
}

void Motor_SetRight(int16_t speed)
{
    uint16_t compare = Motor_ClampCompare((speed < 0) ? -speed : speed);

    GPIO_SetBits(GPIOB, GPIO_Pin_0);

    if (speed > 0)
    {
        GPIO_SetBits(GPIOB, GPIO_Pin_1);
        GPIO_ResetBits(GPIOB, GPIO_Pin_10);
    }
    else if (speed < 0)
    {
        GPIO_ResetBits(GPIOB, GPIO_Pin_1);
        GPIO_SetBits(GPIOB, GPIO_Pin_10);
    }
    else
    {
        GPIO_ResetBits(GPIOB, GPIO_Pin_1 | GPIO_Pin_10);
    }

    TIM_SetCompare2(TIM1, compare);
}

void Motor_Stop(void)
{
    TIM_SetCompare1(TIM1, 0U);
    TIM_SetCompare2(TIM1, 0U);

    GPIO_ResetBits(GPIOA, GPIO_Pin_4 | GPIO_Pin_5);
    GPIO_ResetBits(GPIOB, GPIO_Pin_0 | GPIO_Pin_1 | GPIO_Pin_10);
}
