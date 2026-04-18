#include "stm32f10x.h"
#include "Delay.h"

// 长按时间阈值（ms）
#define KEY_LONG_PRESS_TIME    1000

/**
  * 函    数：按键初始化
  * 参    数：无
  * 返 回 值：无
  */
void Key_Init(void)
{
	/*开启时钟*/
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);		//开启GPIOB的时钟
	
	/*GPIO初始化*/
	GPIO_InitTypeDef GPIO_InitStructure;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU;				//上拉输入模式
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_5;					//PB5引脚
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_Init(GPIOB, &GPIO_InitStructure);						//将PB5引脚初始化为上拉输入
}

/**
  * 函    数：获取按键键码（支持长按短按）
  * 参    数：无
  * 返 回 值：按下按键的键码值
  *           1 = 短按（启动/停止）
  *           2 = 长按（切换速度）
  *           0 = 无按键按下
  */
uint8_t Key_GetNum(void)
{
	uint8_t KeyNum = 0;
	uint32_t pressTime = 0;
	
	if (GPIO_ReadInputDataBit(GPIOB, GPIO_Pin_5) == 0)			//按键按下
	{
		Delay_ms(20);											//延时消抖
		
		if (GPIO_ReadInputDataBit(GPIOB, GPIO_Pin_5) == 0)		//确认按下
		{
			// 计算按下时间
			while (GPIO_ReadInputDataBit(GPIOB, GPIO_Pin_5) == 0 && pressTime < KEY_LONG_PRESS_TIME + 100)
			{
				Delay_ms(10);
				pressTime += 10;
			}
			
			// 判断长按还是短按
			if (pressTime >= KEY_LONG_PRESS_TIME)
			{
				KeyNum = 2;										//长按 = 切换速度
			}
			else
			{
				KeyNum = 1;										//短按 = 启动/停止
			}
			
			// 等待按键松开
			while (GPIO_ReadInputDataBit(GPIOB, GPIO_Pin_5) == 0);
			Delay_ms(20);										//延时消抖
		}
	}
	
	return KeyNum;
}