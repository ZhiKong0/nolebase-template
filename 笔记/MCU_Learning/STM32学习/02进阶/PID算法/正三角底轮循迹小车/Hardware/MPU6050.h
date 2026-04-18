#ifndef __MPU6050_H
#define __MPU6050_H

#include "stm32f10x.h"

#define MPU6050_SCL_PIN    GPIO_Pin_12
#define MPU6050_SDA_PIN    GPIO_Pin_13
#define MPU6050_GPIO_PORT  GPIOB
#define MPU6050_GPIO_CLK   RCC_APB2Periph_GPIOB

void MPU6050_Init(void);
uint8_t MPU6050_ReadReg(uint8_t Reg);
void MPU6050_WriteReg(uint8_t Reg, uint8_t Data);
void MPU6050_ReadData(float *Pitch, float *Roll);
float MPU6050_GetYaw(void);

#endif