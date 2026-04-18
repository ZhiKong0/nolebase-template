#include "stm32f10x.h"



// 延时函数初始化
void Delay_Init(void)
{
	// 配置SysTick定时器
	SysTick->CTRL = 0; // 关闭SysTick
	SysTick->LOAD = 0xFFFFFFFF; // 最大重载值
	SysTick->VAL = 0; // 清空当前值
	SysTick->CTRL = 0x00000005; // 启用SysTick，使用HCLK
}

// 微秒延时
void Delay_us(uint32_t us)
{
	uint32_t start = SysTick->VAL;
	uint32_t ticks = us * (SystemCoreClock / 1000000);
	while((SysTick->VAL - start) < ticks);
}

// 毫秒延时
void Delay_ms(uint32_t ms)
{
	while(ms--)
	{
		Delay_us(1000);
	}
}