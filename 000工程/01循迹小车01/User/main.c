#include "stm32f10x.h"
#include "Delay.h"
#include "OLED.h"
#include "Motor.h"

uint16_t i;

int main()
{
    OLED_Init();           // 初始化 OLED
	OLED_ShowString(1, 1, "Hello");
	Motor_Init();
	Motor_Enable();
    
    
    while(1)
    {
		MotorA_SetDir(MOTOR_DIR_FORWARD);
		MotorB_SetDir(MOTOR_DIR_FORWARD);
		for(i = 0; i < 100; i++)
		{
			MotorA_SetSpeed(i);
			MotorB_SetSpeed(i);
			Delay_ms(10);
		}
		for(i = 0; i < 100; i++)
		{
			MotorA_SetSpeed(99 - i);
			MotorB_SetSpeed(99 - i);
			Delay_ms(10);
		}
		Delay_ms(300);
		MotorA_SetSpeed(0);
		MotorB_SetSpeed(0);
		Delay_ms(300);
		MotorA_SetDir(MOTOR_DIR_BACKWARD);
		MotorB_SetDir(MOTOR_DIR_BACKWARD);
		for(i = 0; i < 100; i++)
		{
			MotorA_SetSpeed(i);
			MotorB_SetSpeed(i);
			Delay_ms(10);
		}
		for(i = 0; i < 100; i++)
		{
			MotorA_SetSpeed(99 - i);
			MotorB_SetSpeed(99 - i);
			Delay_ms(10);
		}
		Delay_ms(300);
	}
}