#ifndef __MPU6050_H
#define __MPU6050_H

#include "stm32f10x.h"

typedef struct
{
	int16_t Ax;
	int16_t Ay;
	int16_t Az;
	int16_t Temp;
	int16_t Gx;
	int16_t Gy;
	int16_t Gz;
} MPU6050_Raw_t;

void MPU6050_Init(void);
uint8_t MPU6050_WhoAmI(void);
void MPU6050_ReadRaw(MPU6050_Raw_t *Raw);
float MPU6050_ReadGyroZ_dps(void);
float MPU6050_ReadGyroZ_dps_Calibrated(void);
void MPU6050_CalibrateGyroZ(uint16_t Samples, uint16_t DelayMs);
int16_t MPU6050_ReadGyroZ_Raw(void);  // 调试：读取原始值

#endif
