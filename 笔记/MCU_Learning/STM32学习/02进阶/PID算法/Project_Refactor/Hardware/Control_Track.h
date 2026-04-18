#ifndef __CONTROL_TRACK_H
#define __CONTROL_TRACK_H

#include "stm32f10x.h"
#include "Encoder_Timer.h"
#include "ICM42688.h"

typedef struct {
    volatile uint8_t isRunning;
    volatile uint32_t tickCount;
    uint32_t runStartTick;
    uint32_t lastControlTick;
    uint32_t hbLastTick;
    uint32_t imuYawSampleTick;
    uint32_t feedbackSampleTick;
    uint32_t expStartTick;
    uint32_t expDurationMs;
    uint32_t expStreamLastTick;
    uint8_t sensorBits;
    uint8_t sensorCount;
    uint8_t lineDetected;
    uint8_t lineMissingCount;
    uint8_t imuDataValid;
    uint8_t expActive;
    uint8_t expStreamEnabled;
    uint8_t rawModeEnabled;
    uint16_t expId;
    uint16_t pwmMax;
    uint16_t lineLostCount;
    float linePosition;
    float linePositionRaw;
    float lineError;
    float lineIntegral;
    float linePrevError;
    float linePidOut;
    float lineDiffTarget;
    float lastSeenError;
    float targetSpeed;
    float speedTargetCurrent;
    float leftTargetSpeed;
    float rightTargetSpeed;
    float leftActualSpeed;
    float rightActualSpeed;
    int32_t leftFeedbackAccum;
    int32_t rightFeedbackAccum;
    uint8_t speedFeedbackTicks;
    float leftFeedbackSpeed;
    float rightFeedbackSpeed;
    float leftSpeedError;
    float rightSpeedError;
    float leftSpeedIntegral;
    float rightSpeedIntegral;
    float leftSpeedPrevError;
    float rightSpeedPrevError;
    float leftSpeedOut;
    float rightSpeedOut;
    float countDiffError;
    float countDiffIntegral;
    float countDiffPrevError;
    float countDiffOut;
    float speedKp;
    float speedKi;
    float speedKd;
    float lineKp;
    float lineKi;
    float lineKd;
    int16_t basePwm;
    int16_t rawPwm;
    int16_t leftPwm;
    int16_t rightPwm;
    Encoder_Data_t encoder;
    ICM42688_Data_t icm;
} ControlTrackSystem_t;

void ControlTrack_Init(ControlTrackSystem_t *sys);
uint8_t ControlTrack_Start(ControlTrackSystem_t *sys);
void ControlTrack_Stop(ControlTrackSystem_t *sys);
void ControlTrack_TimerTickISR(ControlTrackSystem_t *sys);
void ControlTrack_Background(ControlTrackSystem_t *sys);

#endif
