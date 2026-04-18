#ifndef __BSP_KEY_H
#define __BSP_KEY_H

#include "stm32f10x.h"

#define  KEY_mes_ID        0x0E

#define  KEY_Up            1
#define  KEY_Down          0   

#define KEY1_GPIO_PIN              GPIO_Pin_12
#define KEY1_GPIO_PORT             GPIOA
#define KEY1_GPIO_CLK              RCC_APB2Periph_GPIOA

#define KEY2_GPIO_PIN              GPIO_Pin_15
#define KEY2_GPIO_PORT             GPIOA
#define KEY2_GPIO_CLK              RCC_APB2Periph_GPIOA

#define KEY3_GPIO_PIN              GPIO_Pin_14
#define KEY3_GPIO_PORT             GPIOC
#define KEY3_GPIO_CLK              RCC_APB2Periph_GPIOC

#define KEY4_GPIO_PIN              GPIO_Pin_15
#define KEY4_GPIO_PORT             GPIOC
#define KEY4_GPIO_CLK              RCC_APB2Periph_GPIOC

 
/***************************º¯ÊýÉùÃ÷***************************/
void Key_GPIO_Config(void);
void Key_Handler(void);

#endif /* __BSP_KEY_H */



/************************END OF FILE****************************/


