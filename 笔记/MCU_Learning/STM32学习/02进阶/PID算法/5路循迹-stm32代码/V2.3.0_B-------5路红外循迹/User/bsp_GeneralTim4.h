#ifndef __BSP_GENERALTIME4_H
#define __BSP_GENERALTIME4_H


#include "stm32f10x.h"
/********************************************************************************************
*通用定时器TIM参数定义，只限TIM2、3、4
*当使用不同的定时器的时候，对应的GPIO是不一样的，这点要注意
*这里使用TIM4-CH1,TIM4-CH2,TIM4-CH3,TIM4-CH4作为左右电机的pwm控制。
********************************************************************************************/

/******************************************************************
*注意:在 bsp_GeneralTim4.h 中定义的关于 TIM的宏 必须在TIM的后面带有
*定时器标号，这样做是为了避免在使用多个定时器时引发宏定义的多重定义
*******************************************************************/
#define            GENERAL_TIM4                   TIM4
#define            GENERAL_TIM4_APBxClock_FUN     RCC_APB1PeriphClockCmd
#define            GENERAL_TIM4_CLK               RCC_APB1Periph_TIM4


/******0分频*********/
#define            GENERAL_TIM4_Prescaler         (0)


/**************TIM4的计数周期为100us,输出频率为10K**********************/
#define            GENERAL_TIM4_Period            (7200-1)

#define            TIM4_CKD_DIV1                  TIM_CKD_DIV1
#define            TIM4_CounterMode_Up            TIM_CounterMode_Up


/**************************函数声明****************************/

void GENERAL_TIM4_Init(void);

#endif	/* __BSP_GENERALTIME4_H */


