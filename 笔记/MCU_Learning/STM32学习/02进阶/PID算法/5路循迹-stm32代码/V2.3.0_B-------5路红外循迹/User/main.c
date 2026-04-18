/**
  *****************************************************************************
  * @file    : main.c
  * @author  : 至善电子科技（淘宝店铺同名）
  * @version : V2.3.0-B
  * @date    : 2025-04-22
  * @brief   : 主函数
  *****************************************************************************
  * @attention :  
	*  芯片    ：STM32F103C8T6
	*  主板    : SmartCar V2.3.0_B
  *  电源    ：2节18650电池
  *  升压模块：XL6009
  *  电机驱动：DRV8833
	*  三轮小车：前边2个电机驱动轮，后边1个万向轮。
  *****************************************************************************
	* COPYRIGHT：本程序只供学习使用，未经作者许可，不得用于其它任何用途。
	*****************************************************************************
**/

/*** 基本头文件 ***/
#include "stm32f10x.h" 
#include "bsp_systick.h"
#include "bsp_afio.h"
#include "bsp_motor_drv.h"
#include "bsp_GeneralTim3.h" 

/*** 基本人机交互头文件 ***/
#include "bsp_key.h"
#include "bsp_led.h"
#include "bsp_beep.h"
#include "bsp_usart.h" 
#include "bsp_ui.h"


/* 循迹头文件 */
#include "bsp_track.h"



int main(void)
{	  
/***------------------基本初始化----------------------***/
	 /*** 设置2位抢占优先级,2位子优先级 ***/
   NVIC_PriorityGroupConfig(NVIC_PriorityGroup_2);
	
   /*** 配置SysTick节拍为1us ***/
	 SysTick_Init();
		
	 /*** 关闭STM32f103c8t6的JTAG功能，使PB3、PB4、PA15用作普通的IO管脚 ***/
	 JTAGDisable_Config();
		
	 /*** 电机初始化 ***/
	 Motor_Init();	

	
/***----------------人机交互初始化--------------------***/	
	 /*** 按键初始化 ***/
	 Key_GPIO_Config();
	
	 /*** LED灯初始化 ***/
	 LED0_GPIO_Config();
	
	 /*** 蜂鸣器初始化 ***/
	 Beep0_GPIO_Config();
	
	 /*** 串口初始化:115200 8-N-1 ***/
	 USART_Config();
	 	    	

/***-----------------运动模式初始化----------------------***/	 			 	 
	 /*** 循迹初始化 ***/
   Track_Init();	
	 	
   /**********20ms定时节拍初始化************/
   GENERAL_TIM3_Init();	 
 
 
	 /***全部初始化成功，蜂鸣器响一声。***/
	 Beep0_short();	
	 	 

	while(1)
	{
		
     			
   if(clock.tick)//20ms节拍
		{
			clock.tick = 0;//复位定时标记			
			
			/*** 按键处理 ***/
		  Key_Handler();
		 
		  /***** 电机驱动 *****/
		  motor_drive_Handler();
		  		 		 			
		}
		
	}	 

 }

	







