#include "Motor.h"
#include "PWM.h"

#define TB6612_STBY_PORT	GPIOB
#define TB6612_STBY_PIN		GPIO_Pin_0

#define TB6612_AIN1_PORT	GPIOA
#define TB6612_AIN1_PIN		GPIO_Pin_4
#define TB6612_AIN2_PORT	GPIOA
#define TB6612_AIN2_PIN		GPIO_Pin_5

#define TB6612_BIN1_PORT	GPIOB
#define TB6612_BIN1_PIN		GPIO_Pin_1
#define TB6612_BIN2_PORT	GPIOB
#define TB6612_BIN2_PIN		GPIO_Pin_10

static void Motor_GPIO_Init(void)
{
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_GPIOB, ENABLE);

	GPIO_InitTypeDef GPIO_InitStructure;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;

	GPIO_InitStructure.GPIO_Pin = TB6612_AIN1_PIN | TB6612_AIN2_PIN;
	GPIO_Init(TB6612_AIN1_PORT, &GPIO_InitStructure);

	GPIO_InitStructure.GPIO_Pin = TB6612_STBY_PIN | TB6612_BIN1_PIN | TB6612_BIN2_PIN;
	GPIO_Init(TB6612_STBY_PORT, &GPIO_InitStructure);
}

void Motor_Init(void)
{
	Motor_GPIO_Init();
	PWM_Init();

	Motor_Disable();
	MotorA_SetDir(MOTOR_DIR_FORWARD);
	MotorB_SetDir(MOTOR_DIR_FORWARD);
	MotorA_SetSpeed(0);
	MotorB_SetSpeed(0);
}

void Motor_Enable(void)
{
	GPIO_SetBits(TB6612_STBY_PORT, TB6612_STBY_PIN);
}

void Motor_Disable(void)
{
	GPIO_ResetBits(TB6612_STBY_PORT, TB6612_STBY_PIN);
}

void MotorA_SetDir(MotorDir_t Dir)
{
	if (Dir == MOTOR_DIR_FORWARD)
	{
		GPIO_SetBits(TB6612_AIN1_PORT, TB6612_AIN1_PIN);
		GPIO_ResetBits(TB6612_AIN2_PORT, TB6612_AIN2_PIN);
	}
	else
	{
		GPIO_ResetBits(TB6612_AIN1_PORT, TB6612_AIN1_PIN);
		GPIO_SetBits(TB6612_AIN2_PORT, TB6612_AIN2_PIN);
	}
}

void MotorB_SetDir(MotorDir_t Dir)
{
	if (Dir == MOTOR_DIR_FORWARD)
	{
		GPIO_SetBits(TB6612_BIN1_PORT, TB6612_BIN1_PIN);
		GPIO_ResetBits(TB6612_BIN2_PORT, TB6612_BIN2_PIN);
	}
	else
	{
		GPIO_ResetBits(TB6612_BIN1_PORT, TB6612_BIN1_PIN);
		GPIO_SetBits(TB6612_BIN2_PORT, TB6612_BIN2_PIN);
	}
}

void MotorA_SetSpeed(uint16_t Speed)
{
	PWM_SetCompare1(Speed);
}

void MotorB_SetSpeed(uint16_t Speed)
{
	PWM_SetCompare2(Speed);
}
