/**
  *****************************************************************************
  * @file    : bsp_pid_control.c
  * @author  : 至善电子科技
  * @version : V2.3.0-B
  * @date    : 2025-04-22
  * @brief   : 5路红外寻迹PID控制.
  *****************************************************************************
  * @attention
	*  主板    : SmartCar V2.3.0_B
  *****************************************************************************
	* COPYRIGHT：本程序只供学习使用，未经作者许可，不得用于其它任何用途。
	*****************************************************************************
**/

#include "bsp_pid_control.h"	
#include "bsp_GeneralTim4.h"
#include "bsp_track.h"
#include "bsp_motor_drv.h"
  

Track_PID_TypeDef    track_PID = {     0,
	                                     0,
	                                     0,	
	                                  3000,
	                                  6600,
	                                  6600  };


/**           
  * @brief  循迹小车的差速PID控制---位置式PID
	* @param  int8_t   Empower:  权重 =  track.bearing_dev
						int8_t   Target:  目标 =  0
	* @retval int16_t  Pwm: 偏差速度
  */
int16_t Dev_speed_PID(int8_t Empower,int8_t Target)
{ 
	
	 uint16_t  KP = 160,  KD = 4500;
	 float     KI = 0; 
	
	 static int32_t I_dev; //Integral_deviation：偏差积分；
	 static int8_t  L_dev; //Last_deviation：上次偏差
	
	 int8_t    Dev; //Deviation： 偏差
	 int16_t   Pwm; //差速
	
	 Dev  = Empower - Target;                         //计算偏差
	 I_dev += Dev ;	                                 //求出偏差的积分
	 Pwm = KP*Dev  + KI*I_dev + KD*(Dev - L_dev);    //位置式PID控制
	 L_dev = Dev ;                                   //保存上一次偏差 
	
	 return Pwm;                                     //返回PWM值
	
}


/**
  * @brief  计算循迹小车左右PWM,注意参数限制
  * @param  无
  * @retval 无
  */
void Compute_PWM(void)
{	  

	  track_PID.Motor_L_PWM   = (track_PID.Base_Speed + track_PID.Dev_Speed) ;	 
  	track_PID.Motor_R_PWM   = (track_PID.Base_Speed - track_PID.Dev_Speed) ;
    	
	   /*控制小车电机的PWM有效值范围*/
	  if(track_PID.Motor_L_PWM > track_PID.Motor_L_PWM_MAX)
			 track_PID.Motor_L_PWM = track_PID.Motor_L_PWM_MAX;
		
    if(track_PID.Motor_L_PWM < Motor_PWM_MIN)
			 track_PID.Motor_L_PWM = 0;
		
	  if(track_PID.Motor_R_PWM > track_PID.Motor_R_PWM_MAX)
			 track_PID.Motor_R_PWM = track_PID.Motor_R_PWM_MAX;
		
    if(track_PID.Motor_R_PWM < Motor_PWM_MIN)
			 track_PID.Motor_R_PWM = 0;
		
}


/**
  * @brief  循迹小车姿态控制
  * @param  无
  * @retval 无
  */
void Track_position_control(void)//
{
		  		
   motor_Forward(track_PID.Motor_L_PWM, track_PID.Motor_R_PWM);
	
}	


/**
  * @brief  寻迹处理，寻迹模式开启后  每隔20ms调用一次
  * @param  无
  * @retval 无
  */
void Track_Handler(void)  
{
	
   track_PID.Dev_Speed = Dev_speed_PID(track.bearing_dev,0);//计算循迹差速	 
	 Compute_PWM();//计算循迹PWM
	 Track_position_control();//循迹小车姿态控制	
	
}



