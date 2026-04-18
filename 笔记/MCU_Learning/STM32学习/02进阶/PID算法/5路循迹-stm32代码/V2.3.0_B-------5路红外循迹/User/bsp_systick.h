#ifndef __BSP_SYSTICK_H
#define __BSP_SYSTICK_H

#include "stm32f10x.h"
#include "core_cm3.h"



/***************************º¯ÊýÉùÃ÷***************************/	
void SysTick_Init(void);

void SysTick_Delay_us(__IO uint32_t delay_us);
void SysTick_Delay_ms(__IO uint32_t delay_ms);
void SysTick_Delay_s(__IO uint32_t delay_s);


 
#endif /* __BSP_SYSTICK_H */


