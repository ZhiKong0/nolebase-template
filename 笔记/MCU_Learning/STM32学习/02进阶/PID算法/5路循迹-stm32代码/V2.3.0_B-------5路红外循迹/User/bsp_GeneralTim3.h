#ifndef __BSP_GENERALTIME3_H
#define __BSP_GENERALTIME3_H


#include "stm32f10x.h"


/********************************************************************************************
*通用定时器TIM参数定义，只限TIM2、3、4
*注意：当使用不同的定时器的时候，对应的GPIO是不一样的。
********************************************************************************************/

/******************************************************************
*注意:在 bsp_GeneralTim3.h 中定义的关于TIM的宏,必须在TIM的后面带有
*定时器标号n，这样做是为了避免在使用多个定时器时引发宏定义的多重定义
*的冲突。
*******************************************************************/
#define            GENERAL_TIM3                   TIM3
#define            GENERAL_TIM3_APBxClock_FUN     RCC_APB1PeriphClockCmd
#define            GENERAL_TIM3_CLK               RCC_APB1Periph_TIM3

/**************TIM3的计数周期为20ms **********************/
#define            GENERAL_TIM3_Period            (20000-1)

/******经分频之后的TIM3驱动时钟频率为1M，时间为1us。*********/
#define            GENERAL_TIM3_Prescaler         (72-1)

#define            TIM3_CKD_DIV1                  TIM_CKD_DIV1
#define            TIM3_CounterMode_Up            TIM_CounterMode_Up


#define            GENERAL_TIM3_IRQ               TIM3_IRQn


/***************************时钟节拍结构体声明***************************/
typedef struct{ 
  
	uint8_t   tick;    //时钟节拍标志位
	uint8_t   hour;    //小时
	uint8_t   min;     //分钟
  uint8_t   sec;     //秒
	uint8_t   ms_20;   // 20ms
	
	
}clock_TypeDef;




/***************************变量声明***************************/
extern   clock_TypeDef     clock;




/***************************函数声明***************************/
void GENERAL_TIM3_Init(void);



#endif	/* __BSP_GENERALTIME3_H */


