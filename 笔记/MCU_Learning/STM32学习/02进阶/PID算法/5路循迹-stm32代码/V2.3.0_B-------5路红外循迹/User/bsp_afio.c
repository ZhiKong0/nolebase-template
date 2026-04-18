/**
  *****************************************************************************
  * @file    : bsp_afio.c
  * @author  : 至善电子科技
  * @version : V2.3.0-B
  * @date    : 2025-04-22
  * @brief   : 使PB3、PB4、PA15用作普通的IO管脚
  *****************************************************************************
  * @attention
	*  主板    : SmartCar V2.3.0_B
  *****************************************************************************
	* COPYRIGHT：本程序只供学习使用，未经作者许可，不得用于其它任何用途。
	*****************************************************************************
**/
#include "bsp_afio.h" 

/**
  * @brief  关闭STM32f103c8t6的JTAG功能            
  * @param  无
  * @retval 无
  */
void JTAGDisable_Config(void)
{
	
    /***使能复用功能的时钟***/
	  RCC_APB2PeriphClockCmd(RCC_APB2Periph_AFIO,ENABLE);
	  /***关闭STM32f103c8t6的JTAG功能，使PB3、PB4、PA15用作普通的IO管脚***/
	  GPIO_PinRemapConfig(GPIO_Remap_SWJ_JTAGDisable,ENABLE);
	
	
}


/************************END OF FILE****************************/

