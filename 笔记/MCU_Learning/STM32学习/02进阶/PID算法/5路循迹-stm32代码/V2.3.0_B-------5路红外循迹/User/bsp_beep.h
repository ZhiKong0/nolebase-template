#ifndef __BSP_BEEP_H
#define __BSP_BEEP_H

#include "stm32f10x.h"


/* 定义控制Beep0的宏 */
#define Beep0_GPIO_PIN              GPIO_Pin_5
#define Beep0_GPIO_PORT             GPIOA
#define Beep0_GPIO_CLK              RCC_APB2Periph_GPIOA

#define Beep0_OFF		     GPIO_ResetBits(Beep0_GPIO_PORT,Beep0_GPIO_PIN)
#define Beep0_ON			   GPIO_SetBits(Beep0_GPIO_PORT,Beep0_GPIO_PIN)


/**************************函数声明****************************/
void Beep0_GPIO_Config(void);
void Beep0_short(void);
void Beep0_long(void);

#endif /* __BSP_BEEP_H */


