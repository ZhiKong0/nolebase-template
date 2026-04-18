#ifndef __APP_TYPES_H
#define __APP_TYPES_H

#include "stm32f10x.h"
#include "DrvEncoder.h"
#include "DrvTrackSensor.h"
#include "DrvImuBno08x.h"

typedef enum {
    APP_MODE_STRAIGHT = 0,
    APP_MODE_TRACK = 1,
    APP_MODE_COUNT
} AppMode_t;

typedef enum {
    APP_STATE_IDLE = 0,
    APP_STATE_STARTING,
    APP_STATE_RUNNING,
    APP_STATE_STOPPING,
    APP_STATE_FAULT
} AppState_t;

typedef struct {
    DrvEncoderSample_t encoder;
    DrvTrackSensorSample_t track;
    DrvImuBno08xData_t imu;
    uint8_t imuValid;
} AppControlSnapshot_t;

typedef struct {
    int16_t leftPwm;
    int16_t rightPwm;
    uint8_t motorEnable;
} AppMotorCommand_t;

#endif
