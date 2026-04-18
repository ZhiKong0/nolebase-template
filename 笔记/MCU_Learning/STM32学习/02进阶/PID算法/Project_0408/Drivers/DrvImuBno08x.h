#ifndef __DRV_IMU_BNO08X_H
#define __DRV_IMU_BNO08X_H

#include "stm32f10x.h"

typedef struct {
    int16_t accelX;
    int16_t accelY;
    int16_t accelZ;
    int16_t gyroX;
    int16_t gyroY;
    int16_t gyroZ;
    float accelXf;
    float accelYf;
    float accelZf;
    float gyroXf;
    float gyroYf;
    float gyroZf;
    float gyroXOffset;
    float gyroYOffset;
    float gyroZOffset;
    float yawRate;
    float yaw;
    float prevYaw;
    float pitch;
    float roll;
    float q0;
    float q1;
    float q2;
    float q3;
    float exInt;
    float eyInt;
    float ezInt;
    uint8_t ahrsInited;
    uint8_t yawRateValid;
    uint8_t yawSampleUpdated;
} DrvImuBno08xData_t;

uint8_t DrvImuBno08x_Init(void);
uint8_t DrvImuBno08x_Read(DrvImuBno08xData_t *data);
void DrvImuBno08x_Calibrate(DrvImuBno08xData_t *data, uint16_t samples);
void DrvImuBno08x_ResetAttitude(DrvImuBno08xData_t *data);
void DrvImuBno08x_SetBiasTrackEnabled(uint8_t enabled);
void DrvImuBno08x_UpdateYaw(DrvImuBno08xData_t *data, float dt);
float DrvImuBno08x_GetYawError(float targetYaw, float currentYaw);
uint8_t DrvImuBno08x_IsReady(void);
uint8_t DrvImuBno08x_GetAddress(void);
uint8_t DrvImuBno08x_GetInitStage(void);

#endif
