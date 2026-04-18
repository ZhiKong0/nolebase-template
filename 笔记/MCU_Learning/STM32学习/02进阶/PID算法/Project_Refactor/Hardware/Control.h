#ifndef __CONTROL_H
#define __CONTROL_H

#include "stm32f10x.h"
#include "ICM42688.h"
#include "Encoder_Timer.h"

typedef struct {
    uint16_t t_ms;
    uint8_t run;
    int16_t targetSpeed10;
    int16_t actualSpeed10;
    int16_t targetAngle10;
    int16_t actualAngle10;
    int16_t speedErr10;
    int16_t speedOut10;
    int16_t angleErr10;
    int16_t angleOut10;
    int16_t L;
    int16_t R;
    int16_t el;
    int16_t er;
    int16_t ed;
    int16_t pwm;
    int16_t yawRate100;
    int16_t ax;
    int16_t ay;
    int16_t az;
    int16_t gx;
    int16_t gy;
    int16_t gz;
} ExpSample_t;

#define EXP_SAMPLE_PERIOD_MS 10
#define EXP_MAX_SAMPLES      200

typedef struct {
    ICM42688_Data_t icm;
    Encoder_Data_t encoder;

    float targetSpeed;

    float speedKp;
    float speedKi;
    float speedKd;

    float angleKp;
    float angleKi;
    float angleKd;

    uint8_t expActive;
    uint8_t expStreamEnabled;
    uint16_t expId;
    uint32_t expStartTick;
    uint32_t expDurationMs;
    uint8_t expDumpReady;
    uint16_t expSampleCount;
    uint16_t expSamplePeriodMs;
    ExpSample_t expSamples[EXP_MAX_SAMPLES];

    float speedTarget;
    float actualSpeed;
    float speedErr;
    float speedI;
    float speedPrevError;
    float speedOut;

    float angleZero;
    float targetAngle;
    float actualAngle;
    float anglePrevActual;
    float angleErr;
    float angleI;
    float anglePrevError;
    float angleOut;

    float targetYaw;
    float yawErr;
    float yawOut;
    float filteredYawRate;
    float headingDiffResidual;

    int16_t leftPWM;
    int16_t rightPWM;
    int16_t outLeftPWM;
    int16_t outRightPWM;
    int16_t pwmCommand;
    int16_t pwmCore;
    int16_t headingDiffPwm;

    uint8_t isRunning;
    uint32_t runStartTick;
    volatile uint32_t loopTickCount;
    uint32_t imuLastUpdateTick;
    uint32_t imuYawSampleTick;
    uint32_t angleLoopLastTick;
    uint32_t imuPollLastTick;
    uint32_t hbLastTick;
    uint32_t binLastTick;
    uint32_t expSampleLastTick;
    uint32_t expStreamLastTick;
    uint32_t tickCount;
    volatile uint8_t imuDataValid;
    volatile uint32_t imuDataTick;
    volatile float sensedPitch;
    volatile float sensedYaw;
    volatile float sensedYawRate;
    uint8_t yawAlignPending;
} ControlSystem_t;

void Control_Init(ControlSystem_t *sys, uint8_t skipICM);
void Control_LoadStableDefaults(ControlSystem_t *sys);
uint8_t Control_Start(ControlSystem_t *sys);
void Control_Stop(ControlSystem_t *sys);
void Control_LockHeading(ControlSystem_t *sys);
void Control_SetTargetSpeed(ControlSystem_t *sys, float speed);
void Control_TimerTickISR(ControlSystem_t *sys);

void Control_Tick(ControlSystem_t *sys);
void Control_Background(ControlSystem_t *sys);

#endif
