#ifndef __BSP_LED_H
#define __BSP_LED_H

#include "stm32f10x.h"


/*****************LED0*******************/
#define LED0_GPIO_PIN              GPIO_Pin_13
#define LED0_GPIO_PORT             GPIOC
#define LED0_GPIO_CLK              RCC_APB2Periph_GPIOC

#define LED0_OFF		   GPIO_SetBits(LED0_GPIO_PORT,LED0_GPIO_PIN)
#define LED0_ON			   GPIO_ResetBits(LED0_GPIO_PORT,LED0_GPIO_PIN)


/***************************º¯ÊýÉùÃ÷***************************/
void LED0_GPIO_Config(void);
void LED0_flicker(void);
void LED0_long(void);



#endif /* __BSP_LED_H */


