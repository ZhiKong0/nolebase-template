#ifndef __BSP_PID_CONTROL_H
#define __BSP_PID_CONTROL_H

#include "stm32f10x.h"



#define  Motor_PWM_MIN           800//1000

/***************************循迹PID结构体声明***************************/

typedef struct{
	
	int16_t    Motor_L_PWM;       // 左PWM  
	int16_t    Motor_R_PWM;       // 右PWM
	
  int16_t    Dev_Speed;         // 差速 
  uint16_t   Base_Speed;        // 基础速度:3000
	 
  uint16_t   Motor_L_PWM_MAX;   // 左PWM限制 6600
	uint16_t   Motor_R_PWM_MAX;   // 右PWM限制 6600
	
}Track_PID_TypeDef;


/***************************变量声明***************************/
extern Track_PID_TypeDef    track_PID;
 
 


/***************************函数声明***************************/

int16_t Dev_speed_PID(int8_t Encode,int8_t Target);
void Compute_PWM(void);
void Track_position_control(void);
void Track_Handler(void);	

#endif

