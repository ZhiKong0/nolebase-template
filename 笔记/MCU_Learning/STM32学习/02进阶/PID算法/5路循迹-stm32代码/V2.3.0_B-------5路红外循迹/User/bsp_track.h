#ifndef __BSP_TRACK_H
#define __BSP_TRACK_H

#include "stm32f10x.h"
#include "bsp_motor_drv.h"

#define AUTO_TRACK
/******循迹时钟******/
#define  Track_GPIO_CLK              RCC_APB2Periph_GPIOB|RCC_APB2Periph_GPIOA

/******循迹端口******/

#define  L2_Track_PORT             GPIOA
#define  L1_Track_PORT             GPIOB
#define  L0_Track_PORT             GPIOB

#define  M_Track_PORT              GPIOB

#define  R0_Track_PORT             GPIOB
#define  R1_Track_PORT             GPIOA
#define  R2_Track_PORT             GPIOA

/******循迹引脚:左-B14, B13,中- B12,右-B5，A4******/

#define  L2_Track_Pin     GPIO_Pin_11 //A11,未使用
#define  L1_Track_Pin     GPIO_Pin_14 //B14
#define  L0_Track_Pin     GPIO_Pin_13 //B13

#define  M_Track_Pin      GPIO_Pin_12 //B12

#define  R0_Track_Pin     GPIO_Pin_5  //B5
#define  R1_Track_Pin     GPIO_Pin_4  //A4
#define  R2_Track_Pin     GPIO_Pin_0  //A0,未使用


/******循迹状态******/

#define  track_starting        0x01
#define  track_running         0x02

#define    start              1
#define    stop               0


#define   dir_L               1
#define   dir_R               2

#define   blank_Yes           1
#define   blank_No            0  


/***************************循迹结构体声明***************************/
typedef struct{
	
  uint8_t    state;           //用于程序状态切换
	 
	int8_t     bearing_dev;     //小车的姿态偏移量 
	 
 	uint8_t    sensor_data;     //传感器数据, 检测到黑线时为1，没有检测到黑线时为0
	uint8_t    last_data;       //上一次的传感器数据
	
	uint8_t    filter_times;      //过滤器
	uint8_t    crossing;        //赛道中小车经过的交叉口数量，如果只有起始（终点）线 = 2   
	uint8_t    cross_count;     //小车遇到交叉路口的次数
	uint8_t    cross_state;     //用于程序状态切换 
	
	uint8_t    auto_flag;       //自动循迹标志
	
}Track_TypeDef; 


/***************************变量声明***************************/
extern  Track_TypeDef      track;




/***************************函数声明***************************/
void  Track_GPIO_Config(void);
void  Track_Init(void);
void  Signal_Handler(void);
void  corner_handler( uint8_t dir_, uint16_t L_PWM, uint16_t R_PWM );
void  car_auto_track(void);
void  Auto_track_variable_restore(void);





#endif

/************************END OF FILE****************************/

