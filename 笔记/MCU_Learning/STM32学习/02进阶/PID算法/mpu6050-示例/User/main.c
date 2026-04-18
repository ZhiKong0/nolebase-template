#include "stm32f10x.h"                  // Device header
#include "Delay.h"
#include "OLED.h"
#include "MPU6050.h"
#include "inv_mpu.h"
#include "Motor.h"
#include "Encoder.h"
#include "PWM.h"

int16_t aacx,aacy,aacz;		
int16_t Speed1,Speed2;
int16_t gyrox,gyroy,gyroz;	
float Pitch,Roll,Yaw; 		

int output,output1,output2;



int main(void)
{
	OLED_Init();
	MPU_6050_Init();
  	mpu_dmp_init();
	Motor_Init();
	Encoder_TIM2_Init();
	Encoder_TIM3_Init();

	while (1)
	{ 
	  
		
    OLED_ShowSignedNum(2, 1, Pitch, 5);
		OLED_ShowSignedNum(3, 1, Roll, 5);
		OLED_ShowSignedNum(4, 1, Yaw, 5);

		OLED_ShowSignedNum(2, 8, Speed1, 5);
		OLED_ShowSignedNum(3, 8, Speed2, 5);
		
	}

}

void TIM4_IRQHandler(void)//10ms获取一次编码器的值
{

	if (TIM_GetITStatus(TIM4, TIM_IT_Update) == SET)
	{
					  MPU6050_GetData(&gyrox,&gyroy,&gyroz,&aacx,&aacy,&aacz);//陀螺仪都读取--角速度	
		        mpu_dmp_get_data( &Pitch, &Roll, &Yaw);  
            Speed1=Encoder_TIM3_get();
            Speed2=Encoder_TIM2_get();
					
					output = angle(Yaw,0,0);
          output1 = PID_A( 300-output);
          output2 = PID_B( 300+output);
          right( -output );
          left( output );
//		      right( output1 );
//          left( output2 );

        }
		TIM_ClearITPendingBit(TIM4, TIM_IT_Update);

}


