/**
  *****************************************************************************
  * @file    : bsp_motor_drv.c
  * @author  : 至善电子科技
  * @version : V2.3.0-B
  * @date    : 2025-04-22
  * @brief   : 电机驱动程序
  *****************************************************************************
  * @attention
	*  主板    : SmartCar V2.3.0_B
  *****************************************************************************
	* COPYRIGHT：本程序只供学习使用，未经作者许可，不得用于其它任何用途。
	*****************************************************************************
**/
#include "bsp_motor_drv.h"
#include "bsp_systick.h"
#include "bsp_usart.h"
#include "bsp_GeneralTim4.h"

#include "bsp_track.h"
#include "bsp_beep.h"
#include "bsp_ui.h"



Motor_Drive_TypeDef motor_drv = {    0,//control_mes
	                                1600,//forward_PWM_L
	                                1600,//forward_PWM_R
                                  1600,//backward_PWM_L
                                  1600,//backward_PWM_R	
	                                1800 };//turn_pwm    
                                 
 
/**
  * @brief  电机驱动GPIO初始化
  * @param  无
  * @retval 无
  */
void Motor_GPIO_Config(void)
{
	 
	/******************电机的PWM控制端口使用TIM4的4个通道**********************/	
	GPIO_InitTypeDef GPIO_InitStructure;
	
	// TIM4输出比较通道1---小车右电机控制端口 GPIO-PB6 ,复用-推挽输出
	RCC_APB2PeriphClockCmd(BIN2_GPIO_CLK, ENABLE);
  GPIO_InitStructure.GPIO_Pin =  BIN2_GPIO_PIN;
  GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;
  GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
  GPIO_Init(BIN2_GPIO_PORT, &GPIO_InitStructure);
	
	// TIM4输出比较通道2---小车右电机控制端口 GPIO-PB7 ,复用-推挽输出
	RCC_APB2PeriphClockCmd(BIN1_GPIO_CLK, ENABLE);
  GPIO_InitStructure.GPIO_Pin =  BIN1_GPIO_PIN;
  
  GPIO_Init(BIN1_GPIO_PORT, &GPIO_InitStructure);
	
	// TIM4输出比较通道3---小车左电机控制端口 GPIO-PB8 ,复用-推挽输出
	RCC_APB2PeriphClockCmd(AIN1_GPIO_CLK, ENABLE);
  GPIO_InitStructure.GPIO_Pin =  AIN1_GPIO_PIN;
  
  GPIO_Init(AIN1_GPIO_PORT, &GPIO_InitStructure);
	
	// TIM4输出比较通道4---小车左电机控制端口 GPIO-PB9 ,复用-推挽输出
	RCC_APB2PeriphClockCmd(AIN2_GPIO_CLK, ENABLE);
  GPIO_InitStructure.GPIO_Pin =  AIN2_GPIO_PIN;
  
  GPIO_Init(AIN2_GPIO_PORT, &GPIO_InitStructure);

		
	 /***********为防止误动作，使DRV8833模块停止***********/
	 /******************电机停止**********************/
	 GPIO_ResetBits(DRV8833_EN_PORT,DRV8833_EN_PIN);
	 	 
}

/**
  * @brief  电机驱动初始化
  * @param  无
  * @retval 无  
 **/
void Motor_Init(void)
{
	Motor_GPIO_Config();
	GENERAL_TIM4_Init();
		
}	
	

/**************TIM4 PWM有效电平为低电平***************/
/***********前进--- 1:PWM, PWM低电平有效; 1440:7200= 20%***************/
/***********后退--- PWM:1, PWM低电平有效; 1440:7200= 20%***************/

/**
  * @brief  左电机运动控制 
  * @param  state: 0,1,2,3.
            0: 停止
						1: 前进
						2: 后退
						3: 制动
            pwm_val: PWM波形输出的占空比，低电平有效。
  * @retval 无
 **/
static void motor_L_Motion_control(uint8_t state, uint16_t pwm_val)
{
  switch( state )
	 {
		 
		case 0 : //停止
			      TIM_SetCompare3( TIM4 , 7210);//AIN1:0
		        TIM_CCxCmd( TIM4, TIM_Channel_3, TIM_CCx_Enable); 
 
		        TIM_SetCompare4( TIM4 , 7210);//AIN2:0
		        TIM_CCxCmd( TIM4, TIM_Channel_4, TIM_CCx_Enable);
		
		        break; 
	
		case 1 : //前进
			      TIM_SetCompare3( TIM4 , 0);//AIN1:1
			      TIM_CCxCmd( TIM4, TIM_Channel_3, TIM_CCx_Enable);
		  
		        TIM_SetCompare4( TIM4 , pwm_val);//AIN2:PWM
		        TIM_CCxCmd( TIM4, TIM_Channel_4, TIM_CCx_Enable);
		
		        break; 
		
		case 2 : //后退
			      TIM_SetCompare3( TIM4 , pwm_val);//AIN1:PWM
			      TIM_CCxCmd( TIM4, TIM_Channel_3, TIM_CCx_Enable);
		  
		        TIM_SetCompare4( TIM4 , 0);//AIN2:1
		        TIM_CCxCmd( TIM4, TIM_Channel_4, TIM_CCx_Enable);
		
		        break;

    case 3 : //制动
			      TIM_SetCompare3( TIM4 , 0);//AIN1:1
			      TIM_CCxCmd( TIM4, TIM_Channel_3, TIM_CCx_Enable);
			      TIM_SetCompare4( TIM4 , 0);//AIN2:1
		        TIM_CCxCmd( TIM4, TIM_Channel_4, TIM_CCx_Enable);
		        break; 	
		
	  default : 
			      break;
	}
	
	
}

/**
  * @brief  右电机运动控制 
  * @param  state: 0,1,2,3.
            0: 停止
						1: 前进
						2: 后退
						3: 制动
            pwm_val: PWM波形输出的占空比，低电平有效。
  * @retval 无
 **/
static void motor_R_Motion_control(uint8_t state,uint16_t pwm_val)
{
  switch( state )
	 {
		 
		case 0 : //停止
			      TIM_SetCompare2( TIM4 , 7210);//BIN1:0
		        TIM_CCxCmd( TIM4, TIM_Channel_2, TIM_CCx_Enable); 

		        TIM_SetCompare1( TIM4 , 7210);//BIN2:0
		        TIM_CCxCmd( TIM4, TIM_Channel_1, TIM_CCx_Enable);
		
		        break; 
	
		case 1 : //前进
			      TIM_SetCompare2( TIM4 , 0);//BIN1:1
			      TIM_CCxCmd( TIM4, TIM_Channel_2, TIM_CCx_Enable);
		  
		        TIM_SetCompare1( TIM4 , pwm_val);//BIN2:PWM
		        TIM_CCxCmd( TIM4, TIM_Channel_1, TIM_CCx_Enable);
		
		        break; 
		
		case 2 : //后退
			      TIM_SetCompare2( TIM4 , pwm_val);//BIN1:PWM
			      TIM_CCxCmd( TIM4, TIM_Channel_2, TIM_CCx_Enable);
		  
		        TIM_SetCompare1( TIM4 , 0);//BIN2:1
		        TIM_CCxCmd( TIM4, TIM_Channel_1, TIM_CCx_Enable);
		
		        break;

    case 3 : //制动
			      TIM_SetCompare2( TIM4 , 0);//BIN1:1
			      TIM_CCxCmd( TIM4, TIM_Channel_2, TIM_CCx_Enable);
		
		        TIM_SetCompare1( TIM4 , 0);//BIN2:1
		        TIM_CCxCmd( TIM4, TIM_Channel_1, TIM_CCx_Enable);
		        break; 	
		
	  default : 
			      break;
	}
	
	
}


/**
  * @brief  小车前进。 
  * @param  uint16_t L_pwm_val
            uint16_t R_pwm_val     
  * @retval 无
 **/
void motor_Forward(uint16_t L_pwm_val, uint16_t R_pwm_val)
{
		
	 motor_L_Motion_control( 1,L_pwm_val );
   motor_R_Motion_control( 1,R_pwm_val );
	 GPIO_SetBits(DRV8833_EN_PORT,DRV8833_EN_PIN);
		
}


/**
  * @brief  小车后退。 
  * @param  uint16_t L_pwm_val 
            uint16_t R_pwm_val         
  * @retval 无
 **/
void motor_Backward(uint16_t L_pwm_val, uint16_t R_pwm_val)
{
	 motor_L_Motion_control( 2,L_pwm_val );
   motor_R_Motion_control( 2,R_pwm_val );
	
   GPIO_SetBits(DRV8833_EN_PORT,DRV8833_EN_PIN);
	
}

/**
  * @brief  小车快速左转,左轮后退，右轮前进。 
  * @param  uint16_t turn_pwm            
  * @retval 无
 **/
void motor_Turn_Left_A(uint16_t turn_pwm)
{
	 motor_L_Motion_control( 2,turn_pwm );
   motor_R_Motion_control( 1,turn_pwm );
	
	 GPIO_SetBits(DRV8833_EN_PORT,DRV8833_EN_PIN);
	
}


/**
  * @brief  小车快速左转,左轮后退，右轮前进。 
  * @param  uint16_t turn_pwm_l
            uint16_t turn_pwm_r     
  * @retval 无
 **/
void motor_Turn_Left_B(uint16_t turn_pwm_l , uint16_t turn_pwm_r)
{
	 motor_L_Motion_control( 2,turn_pwm_l );
   motor_R_Motion_control( 1,turn_pwm_r );
	
	 GPIO_SetBits(DRV8833_EN_PORT,DRV8833_EN_PIN);
	
}


/**
  * @brief  小车快速右转,右轮后退，左轮前进。 
  * @param  uint16_t turn_pwm            
  * @retval 无
 **/
void motor_Turn_Right_A(uint16_t turn_pwm)
{				
	motor_L_Motion_control( 1,turn_pwm );
  motor_R_Motion_control( 2,turn_pwm );
	
	GPIO_SetBits(DRV8833_EN_PORT,DRV8833_EN_PIN);
	
}


/**
  * @brief  小车快速右转,右轮后退，左轮前进。 
  * @param  uint16_t turn_pwm_l
            uint16_t turn_pwm_r     
  * @retval 无
 **/
void motor_Turn_Right_B(uint16_t turn_pwm_l , uint16_t turn_pwm_r)
{				
	motor_L_Motion_control( 1,turn_pwm_l );
  motor_R_Motion_control( 2,turn_pwm_r );
	
	GPIO_SetBits(DRV8833_EN_PORT,DRV8833_EN_PIN);
	
}


/**
  * @brief  小车标准左转90°。 
  * @param  无            
  * @retval 无
 **/
void std_motor_Turn_Left(void)
{

	    motor_Stop();        //停-->左转-->停。
			SysTick_Delay_ms(30);
	
	    motor_Turn_Left_A(1800);
			SysTick_Delay_ms(580);
	
			motor_Stop();
			SysTick_Delay_ms(30);	 

}

/**
  * @brief  小车标准右转90°。 
  * @param  无            
  * @retval 无
 **/
void std_motor_Turn_Right(void)
{

    	motor_Stop();            //停-->右转-->停。
			SysTick_Delay_ms(30);
	   	
			motor_Turn_Right_A(1800);
			SysTick_Delay_ms(580);
		   				 
			motor_Stop();
		  SysTick_Delay_ms(30);                    

			
}

/**
  * @brief  小车停止。 
  * @param  无            
  * @retval 无
 **/
void motor_Stop(void)
{  

    /***********DRV8833模块禁止***********/
	  GPIO_ResetBits(DRV8833_EN_PORT,DRV8833_EN_PIN);
	
	  /******************左电机PWM停止***********************/
	  motor_L_Motion_control( 0,0 );
    		 		
	  /******************右电机PWM停止***********************/
	  motor_R_Motion_control( 0,0 );
	  
	
}



/**
  * @brief  小车驱动程序。
	*					无论是通过按键、红外遥控，还是蓝牙、AI语音最终都是由
	*					motor_drive_Handler()来驱动小车。
  * @param  无            
  * @retval 无
 **/
void motor_drive_Handler(void)
{	
	
		  switch (short_mes)
				{
				case drive_mes_no :      //初始化后的状态						                      
														    break; 
					
				 case drive_mes_forward ://前进 
                                motor_Forward(motor_drv.forward_PWM_L , motor_drv.forward_PWM_R );				 
														    break;	
					
				 case drive_mes_backward://后退					                      
				                        motor_Backward(motor_drv.backward_PWM_L , motor_drv.backward_PWM_R);			                        				 																																
				 							          break;
									 
				 case drive_mes_turn_left://左转 																				 				  					           				 
				                        motor_Turn_Left_A(motor_drv.turn_pwm);                                					                        
				 											  break;
					  
				 case drive_mes_turn_right://右转					 															 				                                
				                        motor_Turn_Right_A(motor_drv.turn_pwm);  
				                        break;
				 				 
				 case drive_mes_auto_track://自动循迹
                                car_auto_track();			
				 												break;
				  
				 				 
         /***小车接收到停车指令；小车停止。***/
				 case drive_mes_stop :                                 			 				                        																												 																
					                      motor_Stop();        //小车停止。				                       				                    
                                Auto_track_variable_restore();//寻迹状态变量恢复                                  				 
														    break;
															
				 default : break;
					
			  }
		
		
}



