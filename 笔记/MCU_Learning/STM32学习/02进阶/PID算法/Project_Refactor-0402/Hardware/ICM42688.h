#ifndef __ICM42688_H
#define __ICM42688_H

#include "stm32f10x.h"

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

    float accelXf;
    float accelYf;
    float accelZf;

    float gyroXf;
    float gyroYf;
    float gyroZf;

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
} ICM42688_Data_t;

void ICM42688_Init(void);
uint8_t ICM42688_ReadAll(ICM42688_Data_t *data);
void ICM42688_Calibrate(ICM42688_Data_t *data, uint16_t samples);
void ICM42688_ResetAttitude(ICM42688_Data_t *data);
void ICM42688_SetBiasTrackEnabled(uint8_t enabled);
void ICM42688_UpdateYaw(ICM42688_Data_t *data, float dt);
float ICM42688_GetYawError(float targetYaw, float currentYaw);

uint8_t ICM42688_GetWhoAmI(void);
uint8_t ICM42688_GetI2CAddr(void);
uint8_t ICM42688_GetLastProbeAddr(void);
uint8_t ICM42688_GetInitStage(void);
uint8_t ICM42688_GetProbeWhoAmI(void);
uint8_t ICM42688_GetScanFirstAddr(void);
uint8_t ICM42688_GetScanLastAddr(void);
uint8_t ICM42688_GetScanHitCount(void);
uint8_t ICM42688_GetLastChannel(void);
uint8_t ICM42688_GetLastReportId(void);
uint16_t ICM42688_GetLastPayloadLen(void);
uint8_t ICM42688_GetLastTxChannel(void);
uint16_t ICM42688_GetLastTxPacketLen(void);
uint16_t ICM42688_GetLastWriteFailIndex(void);
uint8_t ICM42688_DiagProbeAddr(uint8_t addr);
void ICM42688_DiagPinsInit(void);
uint8_t ICM42688_GetSclLevel(void);
uint8_t ICM42688_GetSdaLevel(void);
uint8_t ICM42688_GetResetLevel(void);
uint8_t ICM42688_GetIntLevel(void);

#endif
