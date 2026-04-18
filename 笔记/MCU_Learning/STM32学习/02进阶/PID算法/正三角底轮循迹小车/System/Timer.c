#include "Timer.h"
#include "misc.h"
#include "stm32f10x_tim.h"
#include "stm32f10x.h"

// 10ms 定时标志
uint8_t Time10ms_Flag = 0;

/**
  * 功能：定时器4初始化 10ms中断一次
  * 主频：72MHz
  * 分频：72-1    → 1MHz
  * 重装载：10000 → 10ms
  */
void TIM4_Init(void)
{
    TIM_TimeBaseInitTypeDef TIM_TimeBaseStructure;
    NVIC_InitTypeDef NVIC_InitStructure;

    // 1. 开 TIM4 时钟
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM4, ENABLE);

    // 2. 定时器配置
    TIM_TimeBaseStructure.TIM_Period = 10000 - 1;    // 10ms
    TIM_TimeBaseStructure.TIM_Prescaler = 72 - 1;    // 72分频
    TIM_TimeBaseStructure.TIM_ClockDivision = TIM_CKD_DIV1;
    TIM_TimeBaseStructure.TIM_CounterMode = TIM_CounterMode_Up;

    TIM_TimeBaseInit(TIM4, &TIM_TimeBaseStructure);

    // 3. 开启更新中断
    TIM_ITConfig(TIM4, TIM_IT_Update, ENABLE);

    // 4. 中断优先级配置
    NVIC_InitStructure.NVIC_IRQChannel = TIM4_IRQn;  // TIM4 中断
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 1;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0;
    NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&NVIC_InitStructure);

    // 5. 启动定时器
    TIM_Cmd(TIM4, ENABLE);
}

// TIM4 中断服务函数
void TIM4_IRQHandler(void)
{
    if(TIM_GetITStatus(TIM4, TIM_IT_Update) != RESET)
    {
        Time10ms_Flag = 1;  // 10ms 到
        TIM_ClearITPendingBit(TIM4, TIM_IT_Update);
    }
}
