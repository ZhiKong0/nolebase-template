#include "stm32f10x.h"



void Encoder_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure;
    TIM_TimeBaseInitTypeDef TIM_TimeBaseStructure;

    // 1. 开时钟：GPIOA + TIM2 + TIM3
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM2, ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM3, ENABLE);

    // ======================
    // 左编码器：PA0(TIM2_CH1)、PA1(TIM2_CH2)
    // ======================
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_0 | GPIO_Pin_1;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN_FLOATING; // 编码器悬空输入
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    // TIM2 编码器模式（AB双相）
    TIM_EncoderInterfaceConfig(TIM2, TIM_EncoderMode_TI12, TIM_ICPolarity_Rising, TIM_ICPolarity_Rising);
    
    // 定时器基础配置（自动重装载最大值）
    TIM_TimeBaseStructure.TIM_Period = 65535;
    TIM_TimeBaseStructure.TIM_Prescaler = 0;
    TIM_TimeBaseStructure.TIM_ClockDivision = TIM_CKD_DIV1;
    TIM_TimeBaseStructure.TIM_CounterMode = TIM_CounterMode_Up;
    TIM_TimeBaseInit(TIM2, &TIM_TimeBaseStructure);

    // ======================
    // 右编码器：PA6(TIM3_CH1)、PA7(TIM3_CH2)
    // ======================
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_6 | GPIO_Pin_7;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    // TIM3 编码器模式
    TIM_EncoderInterfaceConfig(TIM3, TIM_EncoderMode_TI12, TIM_ICPolarity_Rising, TIM_ICPolarity_Rising);
    
    TIM_TimeBaseStructure.TIM_Period = 65535;
    TIM_TimeBaseStructure.TIM_Prescaler = 0;
    TIM_TimeBaseInit(TIM3, &TIM_TimeBaseStructure);

    // 启动定时器
    TIM_Cmd(TIM2, ENABLE);
    TIM_Cmd(TIM3, ENABLE);

    // 计数器清零
    TIM_SetCounter(TIM2, 0);
    TIM_SetCounter(TIM3, 0);
}


int16_t Encoder_GetLeft(void)
{
    int16_t cnt = TIM_GetCounter(TIM2);
    TIM_SetCounter(TIM2, 0); 
    return cnt;
}


int16_t Encoder_GetRight(void)
{
    int16_t cnt = TIM_GetCounter(TIM3);
    TIM_SetCounter(TIM3, 0);
    return cnt;
}