#include "stm32f10x.h"
#include "Timer.h"

// 初始化 TIM4 定时器中断（1ms）
void Timer_Init(void) {
    // 开启时钟
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM4, ENABLE);
    
    // 时基配置
    TIM_TimeBaseInitTypeDef TIM_TimeBaseInitStructure;
    TIM_TimeBaseInitStructure.TIM_ClockDivision = TIM_CKD_DIV1;
    TIM_TimeBaseInitStructure.TIM_CounterMode = TIM_CounterMode_Up;
    TIM_TimeBaseInitStructure.TIM_Period = 1000 - 1;   // ARR: 1ms
    TIM_TimeBaseInitStructure.TIM_Prescaler = 72 - 1;  // PSC: 72MHz/72=1MHz, 1MHz/1000=1kHz
    TIM_TimeBaseInitStructure.TIM_RepetitionCounter = 0;
    TIM_TimeBaseInit(TIM4, &TIM_TimeBaseInitStructure);
    
    // 使能更新中断
    TIM_ITConfig(TIM4, TIM_IT_Update, ENABLE);
    
    // NVIC 配置
    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_2);
    NVIC_InitTypeDef NVIC_InitStructure;
    NVIC_InitStructure.NVIC_IRQChannel = TIM4_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 2;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0;
    NVIC_Init(&NVIC_InitStructure);
    
    // 使能定时器
    TIM_Cmd(TIM4, ENABLE);
}
