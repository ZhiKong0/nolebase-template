#ifndef __MPU6050_H
#define __MPU6050_H
#include "stm32f10x.h"

// MPU6050 I2C地址
#define MPU6050_ADDR        0x68

// 寄存器地址
#define MPU6050_PWR_MGMT_1  0x6B
#define MPU6050_GYRO_CONFIG 0x1B
#define MPU6050_ACCEL_CONFIG 0x1C
#define MPU6050_GYRO_XOUT_H 0x43
#define MPU6050_ACCEL_XOUT_H 0x3B
#define MPU6050_WHO_AM_I    0x75

// 陀螺仪量程 ±2000°/s，灵敏度 16.4 LSB/(°/s)
#define MPU6050_GYRO_SENS   16.4f

// 数据结构
typedef struct {
    int16_t accelX, accelY, accelZ;
    int16_t gyroX, gyroY, gyroZ;
    float gyroXOffset, gyroYOffset, gyroZOffset;  // 零偏
    float yaw;           // 融合后的航向角
    float yawRate;       // 航向角速度 (°/s)
} MPU6050_Data_t;

// 初始化与基本操作
void MPU6050_Init(void);
uint8_t MPU6050_ReadAll(MPU6050_Data_t *data);

// 零偏校准（静止时调用）
void MPU6050_Calibrate(MPU6050_Data_t *data, uint16_t samples);

// 航向角更新（需定时调用，如10ms）
void MPU6050_UpdateYaw(MPU6050_Data_t *data, float dt);

// 获取归一化航向误差 (-180 ~ +180)
float MPU6050_GetYawError(float targetYaw, float currentYaw);

#endif
