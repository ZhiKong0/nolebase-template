/**
  *****************************************************************************
  * @file    : bsp_key.c
  * @author  : 至善电子科技
  * @version : V2.3.0-B
  * @date    : 2025-04-22
  * @brief   : 按键初始化、按键处理程序
  *****************************************************************************
  * @attention
	*  主板    : SmartCar V2.3.0_B
  *****************************************************************************
	* COPYRIGHT：本程序只供学习使用，未经作者许可，不得用于其它任何用途。
	*****************************************************************************
**/
#include "bsp_key.h"
#include "bsp_beep.h"
#include "bsp_led.h"
#include "bsp_systick.h"
#include "bsp_usart.h"
#include "bsp_motor_drv.h "

#include "bsp_ui.h"


/**
  * @brief  按键初始化 	
  * @param  无
  * @retval 无
  */
void Key_GPIO_Config(void)
{
	GPIO_InitTypeDef  GPIO_InitStruct;
	
	RCC_APB2PeriphClockCmd(KEY1_GPIO_CLK , ENABLE);	
	GPIO_InitStruct.GPIO_Pin = KEY1_GPIO_PIN   ;
	GPIO_InitStruct.GPIO_Mode = GPIO_Mode_IN_FLOATING;	
	GPIO_Init(KEY1_GPIO_PORT, &GPIO_InitStruct);	
	
	
	RCC_APB2PeriphClockCmd(KEY2_GPIO_CLK , ENABLE);	
	GPIO_InitStruct.GPIO_Pin = KEY2_GPIO_PIN   ;
	GPIO_InitStruct.GPIO_Mode = GPIO_Mode_IN_FLOATING;	
	GPIO_Init(KEY2_GPIO_PORT, &GPIO_InitStruct);	
	
	
	RCC_APB2PeriphClockCmd(KEY3_GPIO_CLK , ENABLE);	
	GPIO_InitStruct.GPIO_Pin = KEY3_GPIO_PIN   ;
	GPIO_InitStruct.GPIO_Mode = GPIO_Mode_IN_FLOATING;	
	GPIO_Init(KEY3_GPIO_PORT, &GPIO_InitStruct);
	
	
	RCC_APB2PeriphClockCmd(KEY4_GPIO_CLK , ENABLE);	
	GPIO_InitStruct.GPIO_Pin = KEY4_GPIO_PIN   ;
	GPIO_InitStruct.GPIO_Mode = GPIO_Mode_IN_FLOATING;	
	GPIO_Init(KEY4_GPIO_PORT, &GPIO_InitStruct);
	
}

/**
  * @brief  按下按键发送消息。 	
  * @param  KEY_mes
  * @retval 无
  */
void Key_Press_SendMes(uint8_t KEY_mes)
{
  if( (short_mes == drive_mes_no)||(short_mes == drive_mes_stop) )	
	{  motor_drv.control_mes = ( KEY_mes_ID<<8 | KEY_mes);	}																					
	else
	{ 			              
		 motor_Stop();	                                  
		 motor_drv.control_mes = ( KEY_mes_ID<<8 |drive_mes_stop);               
	} 	
			
}

/**
  * @brief  按键处理程序。 	
  * @param  无
  * @retval 无
  */
void Key_Handler(void)
{
	uint8_t key_value = 0x00;//按键逻辑值
	static	uint8_t last_key_value = 0x00;
	static	uint8_t now_key_value = 0x00;
	
	
	if( GPIO_ReadInputDataBit(KEY1_GPIO_PORT, KEY1_GPIO_PIN) == KEY_Down )	//key1按下	
	  key_value |=  0x01;
	else 
	  key_value &= ~0x01;
	
	if( GPIO_ReadInputDataBit(KEY2_GPIO_PORT, KEY2_GPIO_PIN) == KEY_Down )	//key2按下	
	  key_value |=  0x02;
	else 
	  key_value &= ~0x02;
	
	if( GPIO_ReadInputDataBit(KEY3_GPIO_PORT, KEY3_GPIO_PIN) == KEY_Down )	//key3按下	
	  key_value |=  0x04;
	else 
	  key_value &= ~0x04;
	
	if( GPIO_ReadInputDataBit(KEY4_GPIO_PORT, KEY4_GPIO_PIN) == KEY_Down )	//key4按下	
	  key_value |=  0x08;
	else 
	  key_value &= ~0x08;
	
	
	 last_key_value = now_key_value;
	  now_key_value = key_value ;
	
	//如果这一次的按键值 与 上一次的按键值 相等 且不为0，执行按键处理程序。
	if( ( last_key_value == now_key_value ) && ( now_key_value != 0 ) )
	{
	 switch( now_key_value )
	  {
		case 0x01 : //key1按下，小车停止。
              response();	             		     					
              motor_drv.control_mes = ( KEY_mes_ID<<8 | drive_mes_stop );	    		
						  break;
																	          		
		case 0x02 : //key2按下，停止状态下-小车自动循迹；运动状态下-小车停止。 
							response();
              Key_Press_SendMes(drive_mes_auto_track);            
		          break;
		
		case 0x04 : //key3按下，
							response();		            
              motor_drv.control_mes = ( KEY_mes_ID<<8 | drive_mes_stop );	    		
		          break;		
		
		case 0x08 : //key4按下，
							response();	
              motor_drv.control_mes = ( KEY_mes_ID<<8 | drive_mes_stop );	    		
		          break;				
				
		default : 
			      break;				
	  }
	 
  }
	
}



/************************END OF FILE****************************/

