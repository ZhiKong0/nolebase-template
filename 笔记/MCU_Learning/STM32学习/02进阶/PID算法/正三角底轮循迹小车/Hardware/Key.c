#include "stm32f10x.h"
#include "Delay.h"
#include "Key.h"




uint8_t flag = 0; // 1=短按（切换运行/停止）

void Key_Init(void)
{
	GPIO_InitTypeDef GPIO_InitStructure;
	
	// 开启时钟
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);
	
	// 配置按键引脚 PB5 上拉输入
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_5;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU;
	GPIO_Init(GPIOB, &GPIO_InitStructure);
}

// 极简按键扫描：只识别短按，无长按、无速度
void Key_Scan(void)
{
	static uint8_t key_state = 0; // 0松开 1按下
	
	// 检测按键 PB5 按下
	if(GPIO_ReadInputDataBit(GPIOB, GPIO_Pin_5) == Bit_RESET)
	{
		Delay_ms(20); // 消抖
		if(GPIO_ReadInputDataBit(GPIOB, GPIO_Pin_5) == Bit_RESET)
		{
			if(key_state == 0)
			{
				key_state = 1;
				flag = 1; // 短按触发
			}
		}
	}
	else
	{
		key_state = 0; // 按键松开
	}
}