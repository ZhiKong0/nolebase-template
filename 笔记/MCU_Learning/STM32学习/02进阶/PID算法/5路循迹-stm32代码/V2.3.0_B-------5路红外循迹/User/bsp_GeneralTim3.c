/**
  *****************************************************************************
  * @file    : bsp_GeneralTim3.c
  * @author  : 至善电子科技
  * @version : V2.3.0-B
  * @date    : 2025-04-22
  * @brief   : TIM3初始化程序.  
  *****************************************************************************
  * @attention
	*  主板    : SmartCar V2.3.0_B
  *****************************************************************************
	* COPYRIGHT：本程序只供学习使用，未经作者许可，不得用于其它任何用途。
	*****************************************************************************
**/

#include "bsp_GeneralTim3.h" 
#include "bsp_key.h"

#include "bsp_pid_control.h"	
#include "bsp_track.h"

#include "bsp_motor_drv.h"


clock_TypeDef      clock = {  0,  //tick
                              0,  //hour
															0,  //min
															0,  //sec
															0 };//ms_20
                                   

/**
  * @brief  TIM3中断优先级配置
  * @param  无
  * @retval 无   
  */ 
static void GENERAL_TIM3_NVIC_Config(void)
{
    NVIC_InitTypeDef NVIC_InitStructure; 
    	
		/* 设置TIM3为中断源 */
    NVIC_InitStructure.NVIC_IRQChannel = GENERAL_TIM3_IRQ ;	
		/* 设置抢占优先级为1 */
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 1;	 
	  /* 设置子优先级为0 */
    NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0;	
	  /* 使能中断 */
    NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
	
    NVIC_Init(&NVIC_InitStructure);
}


 /**
  * @brief  Tim3模式配置,ch1配置	          
  * @param  无
  * @retval 无
  */
static void GENERAL_TIM3_Mode_Config(void)
{ 
	/*--------------------时基结构体-------------------------*/
	TIM_TimeBaseInitTypeDef  TIM_TimeBaseStructure;
		
	
   // 开启定时器时钟,即内部时钟CK_INT=72M
	GENERAL_TIM3_APBxClock_FUN(GENERAL_TIM3_CLK,ENABLE);
	
	/*--------------------时基结构体初始化-------------------------*/
  	
	// 预分频系数，驱动计数器的时钟 = Fck_int/(psc+1)=72M/(71+1)=1M,单个脉冲的周期为1us。
	TIM_TimeBaseStructure.TIM_Prescaler= GENERAL_TIM3_Prescaler;	
	// 自动重装载寄存器的值，累计TIM_Period+1个时钟后产生一个更新或者中断
	TIM_TimeBaseStructure.TIM_Period=GENERAL_TIM3_Period;	
	// 时钟分频因子 ，没用到不用管
	TIM_TimeBaseStructure.TIM_ClockDivision=TIM3_CKD_DIV1;		
	// 计数器计数模式，设置为向上计数
	TIM_TimeBaseStructure.TIM_CounterMode=TIM3_CounterMode_Up;		
	// 重复计数器的值，没用到，默认0。
	TIM_TimeBaseStructure.TIM_RepetitionCounter=0;	
	// 定时器3时基结构体初始化
	TIM_TimeBaseInit(GENERAL_TIM3, &TIM_TimeBaseStructure);
	
	
	//使能TIM3的预装载寄存器
	TIM_ARRPreloadConfig(TIM3, ENABLE);	
	// 清除计数器(更新)中断标志位
  TIM_ClearFlag(GENERAL_TIM3, TIM_FLAG_Update);		
  // 开启更新中断
	TIM_ITConfig(GENERAL_TIM3, TIM_IT_Update, ENABLE);
	
	// 使能计数器
	TIM_Cmd(GENERAL_TIM3, ENABLE);
}

/**
  * @brief  Tim3初始化
  * @param  无
  * @retval 无
  */
void GENERAL_TIM3_Init(void)
{
	GENERAL_TIM3_NVIC_Config();
	GENERAL_TIM3_Mode_Config();		
}



/********定时器3中断服务程序*******************/
/**
  * @brief  This function handles TIM3 interrupt request.
  * @param  None
  * @retval None
  */
void TIM3_IRQHandler(void)   //TIM3 20ms定时中断 clock_20ms
{
	
	if (TIM_GetITStatus(TIM3, TIM_IT_Update) != RESET)  //检查TIM3更新中断发生与否
	{
     clock.tick = 1;
					 
		/***********清除TIM3更新中断标志*************/ 
		TIM_ClearITPendingBit(TIM3, TIM_IT_Update );  

	}
	
}




