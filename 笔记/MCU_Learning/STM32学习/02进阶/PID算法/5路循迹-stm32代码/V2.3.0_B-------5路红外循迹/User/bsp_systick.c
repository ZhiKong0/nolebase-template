/**
  *****************************************************************************
  * @file    : bsp_systick.c
  * @author  : 至善电子科技
  * @version : V2.3.0-B
  * @date    : 2025-04-22
  * @brief   : 系统滴答定时器 ，查询式延时程序。 
  *****************************************************************************
  * @attention
	*  主板    : SmartCar V2.3.0_B
  *****************************************************************************
	* COPYRIGHT：本程序只供学习使用，未经作者许可，不得用于其它任何用途。
	*****************************************************************************
**/

#include "bsp_systick.h"
#include "core_cm3.h"
#include "misc.h"
#include "bsp_usart.h"



/** SysTick是一个 24bit 的向下递减的计数器
  * @brief  启动系统滴答定时器 SysTick;
	*         设置SysTick计数周期为1us;
	*         关闭中断,采用查询式延时；
  * @param  无
  * @retval 无
  */
void SysTick_Init(void)
{
	/*** SystemCoreClock / 1000000	 1us ***/
	if (SysTick_Config(SystemCoreClock / 1000000))	// ST3.5.0库版本
	{ 
		/* Config error */ 
		while (1);
	}
	  SysTick->CTRL &= ~SysTick_CTRL_TICKINT_Msk;//关闭中断	 
	
}


/** 
  * @brief  微秒延时函数
  * @param  delay_us，延时多少us;
  * @retval 无
  */
void SysTick_Delay_us(__IO uint32_t delay_us)//delay 1us 
{ 
		
	SysTick->VAL = 0;      //清空计数器
	SysTick->CTRL ? 1:0; 	   //清除标志位，开始倒计时。
	
	while( delay_us )
	 { 	 
	   delay_us--;
		 while( !((SysTick->CTRL) & (1<<16)) );//查看countflag 标志位是否置1，等待计数时间到		 
	 }
	  
}


/** 
  * @brief  毫秒延时函数
  * @param  delay_ms，延时多少ms;
  * @retval 无
  */
void SysTick_Delay_ms(__IO uint32_t delay_ms)//delay 1ms
{ 
	
	while( delay_ms )
	 { 	 
	   delay_ms--;
		 SysTick_Delay_us(1000);		 
	 }
	 
}


/** 
  * @brief  秒延时函数
  * @param  delay_s，延时多少s;
  * @retval 无
  */
void SysTick_Delay_s(__IO uint32_t delay_s)//delay s
{
	while( delay_s )
	 {
		 delay_s--;
	   SysTick_Delay_ms(1000);
		 
	 }
		
}


/************************END OF FILE****************************/

