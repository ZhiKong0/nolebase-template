#ifndef  __BSP_UI_H
#define  __BSP_UI_H

#include "stm32f10x.h"
#include "bsp_beep.h"
#include "bsp_led.h"

#define response()   Beep0_short();\
                     LED0_flicker()

#define response_LED()  LED0_flicker()

#define Beep_Led_Long()  {Beep0_ON;\
                          LED0_ON;\
													SysTick_Delay_ms(300);\
													Beep0_OFF;\
													LED0_OFF;}

													
													
	









													
#endif



