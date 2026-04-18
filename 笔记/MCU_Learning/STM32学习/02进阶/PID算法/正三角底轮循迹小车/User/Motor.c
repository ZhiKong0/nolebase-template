#include "Motor.h"

void Motor_Init(void)
{
	GPIO_InitTypeDef GPIO_InitStructure;
	TIM_TimeBaseInitTypeDef TIM_TimeBaseStructure;
	TIM_OCInitTypeDef TIM_OCInitStructure;
	
	// 开启时钟
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_GPIOB, ENABLE);
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_TIM1, ENABLE);
	
	// 配置 STBY 引脚 (PB0)
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_0;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_Init(GPIOB, &GPIO_InitStructure);
	
	// 配置电机方向引脚
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_4 | GPIO_Pin_5; // PA4 (AIN1), PA5 (AIN2)
	GPIO_Init(GPIOA, &GPIO_InitStructure);
	
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_1 | GPIO_Pin_10; // PB1 (BIN1), PB10 (BIN2)
	GPIO_Init(GPIOB, &GPIO_InitStructure);
	
	// 配置PWM引脚
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_8 | GPIO_Pin_9; // PA8 (PWMA), PA9 (PWMB)
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;
	GPIO_Init(GPIOA, &GPIO_InitStructure);
	
	// 初始化TIM1
	TIM_TimeBaseStructure.TIM_Period = 1999; // 5kHz
	TIM_TimeBaseStructure.TIM_Prescaler = 71;
	TIM_TimeBaseStructure.TIM_ClockDivision = 0;
	TIM_TimeBaseStructure.TIM_CounterMode = TIM_CounterMode_Up;
	TIM_TimeBaseStructure.TIM_RepetitionCounter = 0;
	TIM_TimeBaseInit(TIM1, &TIM_TimeBaseStructure);
	
	// 初始化TIM1通道1
	TIM_OCInitStructure.TIM_OCMode = TIM_OCMode_PWM1;
	TIM_OCInitStructure.TIM_OutputState = TIM_OutputState_Enable;
	TIM_OCInitStructure.TIM_Pulse = 0;
	TIM_OCInitStructure.TIM_OCPolarity = TIM_OCPolarity_High;
	TIM_OCInitStructure.TIM_OCIdleState = TIM_OCIdleState_Set;
	TIM_OC1Init(TIM1, &TIM_OCInitStructure);
	
	// 初始化TIM1通道2
	TIM_OC2Init(TIM1, &TIM_OCInitStructure);
	
	// 启动TIM1
	TIM_Cmd(TIM1, ENABLE);
	TIM_CtrlPWMOutputs(TIM1, ENABLE);
	
	// 使能电机驱动
	GPIO_SetBits(GPIOB, GPIO_Pin_0);
}

void Motor_Set(int16_t left_speed, int16_t right_speed)
{
	// ====================== 左电机已改向前 ======================
	if(left_speed > 0)
	{
		GPIO_ResetBits(GPIOA, GPIO_Pin_4);
		GPIO_SetBits(GPIOA, GPIO_Pin_5);
	}
	else
	{
		GPIO_SetBits(GPIOA, GPIO_Pin_4);
		GPIO_ResetBits(GPIOA, GPIO_Pin_5);
		left_speed = -left_speed;
	}
	
	// ====================== 右电机已改向前 ======================
	if(right_speed > 0)
	{
		GPIO_ResetBits(GPIOB, GPIO_Pin_1);
		GPIO_SetBits(GPIOB, GPIO_Pin_10);
	}
	else
	{
		GPIO_SetBits(GPIOB, GPIO_Pin_1);
		GPIO_ResetBits(GPIOB, GPIO_Pin_10);
		right_speed = -right_speed;
	}
	
	// 设置PWM值
	if(left_speed > 1999)  left_speed = 1999;
	if(right_speed > 1999) right_speed = 1999;
	if(left_speed < 0)  left_speed = 0;
	if(right_speed < 0) right_speed = 0;
	
	TIM_SetCompare1(TIM1, left_speed);
	TIM_SetCompare2(TIM1, right_speed);
}

void Motor_Stop(void)
{
	TIM_SetCompare1(TIM1, 0);
	TIM_SetCompare2(TIM1, 0);
	
	GPIO_ResetBits(GPIOA, GPIO_Pin_4 | GPIO_Pin_5);
	GPIO_ResetBits(GPIOB, GPIO_Pin_1 | GPIO_Pin_10);
}