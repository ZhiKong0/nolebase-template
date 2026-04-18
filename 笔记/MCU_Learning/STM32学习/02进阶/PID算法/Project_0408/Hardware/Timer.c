#include "stm32f10x.h"
#include "Timer.h"

void Timer_Init(void) {
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM4, ENABLE);

    TIM_TimeBaseInitTypeDef tb;
    tb.TIM_ClockDivision = TIM_CKD_DIV1;
    tb.TIM_CounterMode = TIM_CounterMode_Up;
    tb.TIM_Period = 1000 - 1;
    tb.TIM_Prescaler = 72 - 1;
    tb.TIM_RepetitionCounter = 0;
    TIM_TimeBaseInit(TIM4, &tb);

    TIM_ITConfig(TIM4, TIM_IT_Update, ENABLE);
    TIM_ClearITPendingBit(TIM4, TIM_IT_Update);

    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_2);
    NVIC_InitTypeDef n;
    n.NVIC_IRQChannel = TIM4_IRQn;
    n.NVIC_IRQChannelCmd = ENABLE;
    n.NVIC_IRQChannelPreemptionPriority = 2;
    n.NVIC_IRQChannelSubPriority = 0;
    NVIC_Init(&n);

    TIM_Cmd(TIM4, ENABLE);
}
