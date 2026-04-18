/**
  *****************************************************************************
  * @file    : bsp_GeneralTim4.c
  * @author  : 至善电子科技
  * @version : V2.3.0-B
  * @date    : 2025-04-22
  * @brief   : 电机PWM输出初始化程序
  *****************************************************************************
  * @attention
	*  主板    : SmartCar V2.3.0_B
  *****************************************************************************
	* COPYRIGHT：本程序只供学习使用，未经作者许可，不得用于其它任何用途。
	*****************************************************************************
**/
#include "bsp_GeneralTim4.h" 


/**
  * @brief  Tim4 模式 配置
            输出比较 通道1 初始化
            输出比较 通道2 初始化
            输出比较 通道3 初始化
            输出比较 通道4 初始化 
  * @param  无
  * @retval 无
  */
static void GENERAL_TIM4_Mode_Config(void)
{ 
	/*--------------------时基结构体-------------------------*/
	TIM_TimeBaseInitTypeDef  TIM_TimeBaseStructure;
		
	/*--------------------输出比较结构体-------------------*/	
	TIM_OCInitTypeDef  TIM_OCInitStructure;
	
	// 开启定时器时钟,即内部时钟CK_INT=72M
	GENERAL_TIM4_APBxClock_FUN(GENERAL_TIM4_CLK,ENABLE);	
	
	/*--------------------时基结构体初始化-------------------------*/
	
  // 驱动计数器的时钟 = Fck_int/(psc+1)=72M/(71+1)=1M,单个脉冲的周期为1us。
	TIM_TimeBaseStructure.TIM_Prescaler= GENERAL_TIM4_Prescaler;		
	// 自动重装载寄存器的值，累计TIM_Period+1个时钟后产生一个更新或者中断
	TIM_TimeBaseStructure.TIM_Period=GENERAL_TIM4_Period;
	
	// 时钟分频因子 ，没用到不用管
	TIM_TimeBaseStructure.TIM_ClockDivision=TIM4_CKD_DIV1;		
	// 计数器计数模式，设置为向上计数
	TIM_TimeBaseStructure.TIM_CounterMode=TIM4_CounterMode_Up;		
	// 重复计数器的值，没用到，默认0。
	TIM_TimeBaseStructure.TIM_RepetitionCounter=0;	
	// 初始化定时器
	TIM_TimeBaseInit(GENERAL_TIM4, &TIM_TimeBaseStructure);


	/*--------------------输出比较结构体初始化-------------------*/	
	// 配置为PWM模式1
	TIM_OCInitStructure.TIM_OCMode = TIM_OCMode_PWM1;
	// 先禁止输出使能
	TIM_OCInitStructure.TIM_OutputState = TIM_OutputState_Disable;
	// 配置输出通道电平极性，低电平有效。	
  TIM_OCInitStructure.TIM_OCPolarity = TIM_OCPolarity_Low;
	
  
	// 输出比较通道 1初始化
	TIM_OC1Init(GENERAL_TIM4, &TIM_OCInitStructure);
	TIM_OC1PreloadConfig(GENERAL_TIM4, TIM_OCPreload_Enable);

  // 输出比较通道 2初始化
	TIM_OC2Init(GENERAL_TIM4, &TIM_OCInitStructure);
	TIM_OC2PreloadConfig(GENERAL_TIM4, TIM_OCPreload_Enable);

	// 输出比较通道 3初始化
	TIM_OC3Init(GENERAL_TIM4, &TIM_OCInitStructure);
	TIM_OC3PreloadConfig(GENERAL_TIM4, TIM_OCPreload_Enable);
	
	// 输出比较通道 4初始化
	TIM_OC4Init(GENERAL_TIM4, &TIM_OCInitStructure);
	TIM_OC4PreloadConfig(GENERAL_TIM4, TIM_OCPreload_Enable);
	
	//使能TIM4的预装载寄存器
	TIM_ARRPreloadConfig(TIM4, ENABLE); 	
	
	// 使能计数器
	TIM_Cmd(GENERAL_TIM4, ENABLE);
}

/**
  * @brief  Tim4 初始化            
  * @param  无
  * @retval 无
  */
void GENERAL_TIM4_Init(void)
{	
	GENERAL_TIM4_Mode_Config();	
	
}





