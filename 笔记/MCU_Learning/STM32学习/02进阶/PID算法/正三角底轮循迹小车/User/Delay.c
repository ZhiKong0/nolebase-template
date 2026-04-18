#include "Delay.h"

static uint32_t g_fac_us = 0;

void Delay_Init(void)
{
	SysTick_CLKSourceConfig(SysTick_CLKSource_HCLK_Div8);
	g_fac_us = SystemCoreClock / 8000000;
}

void Delay_us(uint32_t us)
{
	uint32_t temp;
	SysTick->LOAD = us * g_fac_us;
	SysTick->VAL = 0x00;
	SysTick->CTRL |= SysTick_CTRL_ENABLE_Msk;
	
	do
	{
		temp = SysTick->CTRL;
	} while((temp & SysTick_CTRL_ENABLE_Msk) && !(temp & SysTick_CTRL_COUNTFLAG_Msk));
	
	SysTick->CTRL &= ~SysTick_CTRL_ENABLE_Msk;
	SysTick->VAL = 0x00;
}

void Delay_ms(uint32_t ms)
{
	uint32_t i;
	for(i = 0; i < ms; i++)
	{
		Delay_us(1000);
	}
}