/**
  *****************************************************************************
  * @file    : bsp_beep.c
  * @author  : 至善电子科技
  * @version : V2.3.0-B
  * @date    : 2025-04-22
  * @brief   : 蜂鸣器驱动程序
  *****************************************************************************
  * @attention
	*  主板    : SmartCar V2.3.0_B
  *****************************************************************************
	* COPYRIGHT：本程序只供学习使用，未经作者许可，不得用于其它任何用途。
	*****************************************************************************
**/
#include "bsp_beep.h"
#include "bsp_systick.h"

/**
  * @brief  蜂鸣器初始化 PA5
            本程序中采用的是有源蜂鸣器	
  * @param  无
  * @retval 无
  */
	
void Beep0_GPIO_Config(void)
{
	GPIO_InitTypeDef  GPIO_InitStruct;
	
	RCC_APB2PeriphClockCmd(Beep0_GPIO_CLK, ENABLE);
	
	GPIO_InitStruct.GPIO_Pin = Beep0_GPIO_PIN;
	GPIO_InitStruct.GPIO_Mode = GPIO_Mode_Out_PP;
	GPIO_InitStruct.GPIO_Speed = GPIO_Speed_50MHz;
	/*调用库函数，初始化GPIO*/
	GPIO_Init(Beep0_GPIO_PORT, &GPIO_InitStruct);	
	
	
	/* 关闭Beep0	*/
	Beep0_OFF;
	
}

/**
  * @brief  蜂鸣器短鸣一声。 	
  * @param  无
  * @retval 无
  */

void Beep0_short(void)
{
	 Beep0_ON;//
	 SysTick_Delay_ms(80);
	 Beep0_OFF;
	
}

/**
  * @brief  蜂鸣器长鸣一声。 	
  * @param  无
  * @retval 无
  */

void Beep0_long(void)
{
	 Beep0_ON;
	 SysTick_Delay_ms(200);
	 Beep0_OFF;
	
}



/************************END OF FILE****************************/

