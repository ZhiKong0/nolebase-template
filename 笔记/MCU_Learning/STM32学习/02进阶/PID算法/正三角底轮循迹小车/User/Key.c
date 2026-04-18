#include "stm32f10x.h"
#include "Delay.h"
#include "Key.h"

uint8_t flag = 0; // 控制标志

void Key_Init(void)
{
	GPIO_InitTypeDef GPIO_InitStructure;
	
	// 开启时钟
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);
	
	// 配置按键引脚 PB5
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_5;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU;
	GPIO_Init(GPIOB, &GPIO_InitStructure);
}

void Key_Scan(void)
{
	static uint8_t key_state = 0;
	static uint32_t key_press_time = 0;
	
	// 检测按键 PB5
	if(GPIO_ReadInputDataBit(GPIOB, GPIO_Pin_5) == Bit_RESET)
	{
		Delay_ms(10); // 消抖
		if(GPIO_ReadInputDataBit(GPIOB, GPIO_Pin_5) == Bit_RESET)
		{
			if(key_state == 0) // 第一次检测到按键按下
			{
				key_state = 1;
				key_press_time = 0;
			}
			else if(key_state == 1) // 按键持续按下
			{
				key_press_time++;
				if(key_press_time > 200) // 长按检测（约2秒）
				{
					key_state = 2;
					flag = 2; // 长按标志
				}
			}
		}
	}
	else
	{
		if(key_state == 1) // 短按释放
		{
			flag = 1; // 短按标志
		}
		key_state = 0; // 重置状态
		key_press_time = 0;
	}
}