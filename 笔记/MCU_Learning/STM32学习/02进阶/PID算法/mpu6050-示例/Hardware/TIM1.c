#include "stm32f10x.h"                  // Device header
#include "string.h"
#include "mpu6050.h"
#include "OLED.h"
#include "inv_mpu.h"

//float Pitch,Roll,Yaw;

void Timer_Init(void)
{
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_TIM1,ENABLE);//开启时钟频率
	
	TIM_InternalClockConfig(TIM1);
	
	TIM_TimeBaseInitTypeDef TIM_TimeBaseInitStruct;//时基单元
	TIM_TimeBaseInitStruct.TIM_ClockDivision=TIM_CKD_DIV1;//时钟分频，1分频
	TIM_TimeBaseInitStruct.TIM_CounterMode=TIM_CounterMode_Up;//计数器模式/向上计数
	TIM_TimeBaseInitStruct.TIM_Period=10000-1;//周期，ARR自动重装器的值，定时频率的值
	TIM_TimeBaseInitStruct.TIM_Prescaler=7200-1;//PSC预分频器的值
	TIM_TimeBaseInitStruct.TIM_RepetitionCounter=0;//重复计数器的值
    TIM_TimeBaseInit(TIM1,&TIM_TimeBaseInitStruct);

	TIM_ClearFlag(TIM1,TIM_FLAG_Update);//手动把更新中断标志位清除一下，定时器从零增加
	TIM_ITConfig(TIM1,TIM_IT_Update,ENABLE);//使能更新中断，定时器，更新中断，开启中断
	
	NVIC_PriorityGroupConfig(NVIC_PriorityGroup_2);//NVIC优先级分组，选择分组2
	//NVIC，提供中断控制器，用于总体管理异常，“内嵌向量中断控制器”
	NVIC_InitTypeDef NVIC_InitStructure;
	NVIC_InitStructure.NVIC_IRQChannel=TIM1_UP_IRQn;//定时器2的通道
	NVIC_InitStructure.NVIC_IRQChannelCmd=ENABLE;//
	NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority=1;//抢占优先级
	NVIC_InitStructure.NVIC_IRQChannelSubPriority=1;//响应优先级
	NVIC_Init(&NVIC_InitStructure);
	
	TIM_Cmd(TIM1,DISABLE);//启动定时器
}

//整个定时中断的初始化代码完成
//void TIM1_UP_IRQHandler(void)//定时器1的中断函数
//{
//	static int16_t time=0;
//	if(TIM_GetITStatus(TIM1,TIM_IT_Update)==SET)//获取中断标志位，更新中断的标志位
//	{
//		time++;
//        if(time==10)
//        {
//            
//            time=0;
//        }
//		
////		TIM_ClearITPendingBit(TIM1,TIM_IT_Update);//清除标志位
//	}
//}

