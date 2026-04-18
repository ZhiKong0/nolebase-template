#include "Timer.h"

void Timer_Init(void)
{
    TIM_TimeBaseInitTypeDef tim_base_init;
    NVIC_InitTypeDef nvic_init;

    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM4, ENABLE);

    tim_base_init.TIM_Prescaler = 71U;
    tim_base_init.TIM_CounterMode = TIM_CounterMode_Up;
    tim_base_init.TIM_Period = 999U;
    tim_base_init.TIM_ClockDivision = TIM_CKD_DIV1;
    tim_base_init.TIM_RepetitionCounter = 0U;
    TIM_TimeBaseInit(TIM4, &tim_base_init);

    TIM_ClearITPendingBit(TIM4, TIM_IT_Update);
    TIM_ITConfig(TIM4, TIM_IT_Update, ENABLE);

    nvic_init.NVIC_IRQChannel = TIM4_IRQn;
    nvic_init.NVIC_IRQChannelPreemptionPriority = 2;
    nvic_init.NVIC_IRQChannelSubPriority = 1;
    nvic_init.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&nvic_init);

    TIM_Cmd(TIM4, ENABLE);
}
