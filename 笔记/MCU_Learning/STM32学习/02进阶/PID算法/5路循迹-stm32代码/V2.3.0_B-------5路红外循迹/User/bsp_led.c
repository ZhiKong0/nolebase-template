/**
  *****************************************************************************
  * @file    : bsp_led.c
  * @author  : 至善电子科技
  * @version : V2.3.0-B
  * @date    : 2025-04-22
  * @brief   : LED灯驱动程序
  *****************************************************************************
  * @attention
	*  主板    : SmartCar V2.3.0_B
  *****************************************************************************
	* COPYRIGHT：本程序只供学习使用，未经作者许可，不得用于其它任何用途。
	*****************************************************************************
**/

#include "bsp_led.h"
#include "bsp_systick.h"

/**
  * @brief  LED0 GPIO配置
  * @param  无
  * @retval 无
 **/
void LED0_GPIO_Config(void)
{
	GPIO_InitTypeDef  GPIO_InitStruct;
	
	RCC_APB2PeriphClockCmd(LED0_GPIO_CLK, ENABLE);
	
	GPIO_InitStruct.GPIO_Pin   = LED0_GPIO_PIN;
	GPIO_InitStruct.GPIO_Mode  = GPIO_Mode_Out_PP;
	GPIO_InitStruct.GPIO_Speed = GPIO_Speed_2MHz;
	/*调用库函数，初始化GPIO*/
	GPIO_Init(LED0_GPIO_PORT, &GPIO_InitStruct);	
	
	
	/* 关闭led0	*/
	LED0_OFF;
	
}

/**
  * @brief  LED0闪一下
  * @param  无
  * @retval 无
 **/
void LED0_flicker(void)
{
   LED0_ON;
   SysTick_Delay_ms(80);
   LED0_OFF;

}	

/**
  * @brief  LED0长亮一下
  * @param  无
  * @retval 无
 **/
void LED0_long(void)
{
   LED0_ON;
   SysTick_Delay_ms(200);
   LED0_OFF;

}	


