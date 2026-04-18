#include "SoftI2C.h"

#define SOFTI2C_SCL_PORT GPIOB
#define SOFTI2C_SCL_PIN  GPIO_Pin_12

#define SOFTI2C_SDA_PORT GPIOB
#define SOFTI2C_SDA_PIN  GPIO_Pin_13

static void SoftI2C_Delay(void)
{
	for (volatile uint8_t i = 0; i < 20; i++)
		;
}

static void SoftI2C_SCL(uint8_t x)
{
	GPIO_WriteBit(SOFTI2C_SCL_PORT, SOFTI2C_SCL_PIN, (BitAction)x);
}

static void SoftI2C_SDA(uint8_t x)
{
	GPIO_WriteBit(SOFTI2C_SDA_PORT, SOFTI2C_SDA_PIN, (BitAction)x);
}

static uint8_t SoftI2C_SDA_Read(void)
{
	return GPIO_ReadInputDataBit(SOFTI2C_SDA_PORT, SOFTI2C_SDA_PIN);
}

static void SoftI2C_SDA_Input(void)
{
	GPIO_InitTypeDef GPIO_InitStructure;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU;
	GPIO_InitStructure.GPIO_Pin = SOFTI2C_SDA_PIN;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_Init(SOFTI2C_SDA_PORT, &GPIO_InitStructure);
}

static void SoftI2C_SDA_Output(void)
{
	GPIO_InitTypeDef GPIO_InitStructure;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_OD;
	GPIO_InitStructure.GPIO_Pin = SOFTI2C_SDA_PIN;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_Init(SOFTI2C_SDA_PORT, &GPIO_InitStructure);
}

void SoftI2C_Init(void)
{
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);

	GPIO_InitTypeDef GPIO_InitStructure;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_OD;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_InitStructure.GPIO_Pin = SOFTI2C_SCL_PIN;
	GPIO_Init(SOFTI2C_SCL_PORT, &GPIO_InitStructure);

	GPIO_InitStructure.GPIO_Pin = SOFTI2C_SDA_PIN;
	GPIO_Init(SOFTI2C_SDA_PORT, &GPIO_InitStructure);

	SoftI2C_SCL(1);
	SoftI2C_SDA(1);
}

void SoftI2C_Start(void)
{
	SoftI2C_SDA_Output();
	SoftI2C_SDA(1);
	SoftI2C_SCL(1);
	SoftI2C_Delay();
	SoftI2C_SDA(0);
	SoftI2C_Delay();
	SoftI2C_SCL(0);
	SoftI2C_Delay();
}

void SoftI2C_Stop(void)
{
	SoftI2C_SDA_Output();
	SoftI2C_SDA(0);
	SoftI2C_SCL(1);
	SoftI2C_Delay();
	SoftI2C_SDA(1);
	SoftI2C_Delay();
}

uint8_t SoftI2C_WriteByte(uint8_t Byte)
{
	SoftI2C_SDA_Output();
	for (uint8_t i = 0; i < 8; i++)
	{
		SoftI2C_SDA((Byte & 0x80) ? 1 : 0);
		SoftI2C_Delay();
		SoftI2C_SCL(1);
		SoftI2C_Delay();
		SoftI2C_SCL(0);
		SoftI2C_Delay();
		Byte <<= 1;
	}

	SoftI2C_SDA_Input();
	SoftI2C_Delay();
	SoftI2C_SCL(1);
	SoftI2C_Delay();
	uint8_t ack = SoftI2C_SDA_Read();
	SoftI2C_SCL(0);
	SoftI2C_Delay();
	SoftI2C_SDA_Output();

	return (ack == 0) ? 1 : 0;
}

uint8_t SoftI2C_ReadByte(uint8_t Ack)
{
	uint8_t Byte = 0;
	SoftI2C_SDA_Input();
	for (uint8_t i = 0; i < 8; i++)
	{
		SoftI2C_SCL(1);
		SoftI2C_Delay();
		Byte <<= 1;
		if (SoftI2C_SDA_Read())
			Byte |= 0x01;
		SoftI2C_SCL(0);
		SoftI2C_Delay();
	}

	SoftI2C_SDA_Output();
	SoftI2C_SDA(Ack ? 0 : 1);
	SoftI2C_Delay();
	SoftI2C_SCL(1);
	SoftI2C_Delay();
	SoftI2C_SCL(0);
	SoftI2C_Delay();
	SoftI2C_SDA(1);

	return Byte;
}
