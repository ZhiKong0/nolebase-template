#ifndef __SENSOR_FUSION_H
#define __SENSOR_FUSION_H

#include "stm32f10x.h"

typedef struct {
    float accelXf, accelYf, accelZf;
    float gyroXf, gyroYf, gyroZf;
    float yaw, prevYaw, yawRate;
    float pitch, roll;
    float q0, q1, q2, q3;
    uint8_t ahrsInited;
    uint8_t yawSampleUpdated;
    uint8_t yawRateValid;
} IMU_Data_t;

typedef struct {
    uint8_t bits;
    uint8_t count;
    uint8_t lineDetected;
    float position;
} LineSensor_Data_t;

void BNO085_Init(void);
uint8_t BNO085_ReadAll(IMU_Data_t *data);
void BNO085_ResetAttitude(IMU_Data_t *data);
void BNO085_UpdateYaw(IMU_Data_t *data, float dt);
float BNO085_GetYawError(float target, float current);
uint8_t BNO085_IsReady(void);
uint8_t BNO085_GetInitStage(void);
uint8_t BNO085_GetI2CAddr(void);
uint8_t BNO085_GetInitFailCode(void);
uint8_t BNO085_GetLastRxFailCode(void);
uint8_t BNO085_GetLastChannel(void);
uint8_t BNO085_GetLastReportId(void);
uint16_t BNO085_GetLastPayloadLen(void);

void LineSensor_Init(void);
void LineSensor_Read(LineSensor_Data_t *data);

#endif
