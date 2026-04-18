#ifndef __BSP_MOTOR_DRV_H
#define __BSP_MOTOR_DRV_H

#include "stm32f10x.h"
#include "bsp_usart.h"

/******电机时钟******/
#define MOTOR_CLK             RCC_APB2Periph_GPIOB

/******电机驱动模块 使能脚:STBY******/
#define DRV8833_EN_PIN                GPIO_Pin_4
#define DRV8833_EN_PORT               GPIOB


/********TIM4 输出比较通道1---小车右电机PWM-BIN2*********/
#define     BIN2_GPIO_CLK           RCC_APB2Periph_GPIOB
#define     BIN2_GPIO_PORT          GPIOB
#define     BIN2_GPIO_PIN           GPIO_Pin_6

/********TIM4 输出比较通道2---小车右电机PWM-BIN1*********/
#define     BIN1_GPIO_CLK           RCC_APB2Periph_GPIOB
#define     BIN1_GPIO_PORT          GPIOB
#define     BIN1_GPIO_PIN           GPIO_Pin_7


/********TIM4 输出比较通道3---小车左电机PWM-AIN1*********/
#define     AIN1_GPIO_CLK           RCC_APB2Periph_GPIOB
#define     AIN1_GPIO_PORT          GPIOB
#define     AIN1_GPIO_PIN           GPIO_Pin_8

/********TIM4 输出比较通道4---小车左电机PWM-AIN2*********/
#define     AIN2_GPIO_CLK           RCC_APB2Periph_GPIOB
#define     AIN2_GPIO_PORT          GPIOB
#define     AIN2_GPIO_PIN           GPIO_Pin_9


/*******************************************************************

*******************************************************************/


/******驱动消息宏定义，范围:0x00 - 0xFF******/ 
#define drive_mes_no               0x00

#define drive_mes_forward          0x01
#define drive_mes_backward         0x02
#define drive_mes_turn_left        0x03 
#define drive_mes_turn_right       0x04

#define drive_mes_speed_up         0x05
#define drive_mes_speed_down       0x06 


#define drive_mes_auto_avoid       0x0A
#define drive_mes_auto_track       0x0B
#define drive_mes_auto_follow      0x0C
#define drive_mes_track_power_ON   0x0D
#define drive_mes_track_power_OFF  0x0E  
#define drive_mes_stop             0xFF

/****** 消息来源宏定义 ******/ 

#define from_AI_voice              0x0A
#define from_Bluetooth             0x0B 
#define from_IRDA                  0x0D 
#define from_Key                   0x0E    
											
#define  short_mes  (motor_drv.control_mes & 0xff)


/***************************电机驱动结构体声明***************************/
typedef struct{ 
  
	uint16_t   control_mes;       //0xcb01  c:命令 b:蓝牙 01:前进
	uint16_t   forward_PWM_L;     //
	uint16_t   forward_PWM_R;
	uint16_t	 backward_PWM_L;
	uint16_t   backward_PWM_R;
	uint16_t   turn_pwm;
	
}Motor_Drive_TypeDef;



/***************************变量声明***************************/
extern   Motor_Drive_TypeDef      motor_drv;

																

/***************************函数声明***************************/
void Motor_Init(void);

void motor_Forward(uint16_t L_pwm_val, uint16_t R_pwm_val);
void motor_Backward(uint16_t L_pwm_val, uint16_t R_pwm_val);

void motor_Turn_Left_A(uint16_t turn_pwm);
void motor_Turn_Right_A(uint16_t turn_pwm);

void motor_Turn_Left_B(uint16_t turn_pwm_l , uint16_t turn_pwm_r);
void motor_Turn_Right_B(uint16_t turn_pwm_l , uint16_t turn_pwm_r);

void std_motor_Turn_Left(void);
void std_motor_Turn_Right(void);

void motor_Stop(void);
void motor_Brake(void);


void motor_drive_Handler(void);


#endif /*__bsp_motor_drv_h*/




