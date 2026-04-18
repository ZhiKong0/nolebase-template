#ifndef __MPU6050_H
#define __MPU6050_H

#include "stm32f10x.h"

#define MPU6050_ADDR            0x68
#define MPU6050_PWR_MGMT_1      0x6B
#define MPU6050_GYRO_CONFIG     0x1B
#define MPU6050_ACCEL_CONFIG    0x1C
#define MPU6050_ACCEL_XOUT_H    0x3B

#define MPU6050_GYRO_SENS       16.4f

typedef struct {
    int16_t accelX;
    int16_t accelY;
    int16_t accelZ;

    int16_t gyroX;
    int16_t gyroY;
    int16_t gyroZ;

    float gyroXOffset;
    float gyroYOffset;
    float gyroZOffset;

    float yawRate;
    float yaw;
} MPU6050_Data_t;

void MPU6050_Init(void);
uint8_t MPU6050_ReadAll(MPU6050_Data_t *data);
void MPU6050_Calibrate(MPU6050_Data_t *data, uint16_t samples);
void MPU6050_UpdateYaw(MPU6050_Data_t *data, float dt);
float MPU6050_GetYawError(float targetYaw, float currentYaw);

#endif
