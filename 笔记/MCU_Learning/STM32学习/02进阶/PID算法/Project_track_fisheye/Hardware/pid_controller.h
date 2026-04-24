#ifndef __PID_CONTROLLER_H
#define __PID_CONTROLLER_H

#include "stm32f10x.h"
#include "config.h"

typedef struct {
    float kp;
    float ki;
    float kd;
    float integral;
    float prevError;
    float prevMeasurement;
    float filteredDerivative;
    float output;
    float outputLimit;
    float integralLimit;
} PID_Instance_t;

typedef struct {
    PID_Instance_t speedPID;
    PID_Instance_t headingPID;

    float targetSpeed;
    float currentSpeed;
    float speedRampTarget;

    float targetYaw;
    float headingTrim;       /* deg: constant yaw offset to compensate hardware bias */
    float currentYaw;

    float feedforwardGain;

    int16_t leftPWM;
    int16_t rightPWM;
    int16_t pwmCore;
    int16_t headingDiffPWM;
    int16_t dTermPostDZ;

    float lastHp;
    float lastHi;
    float lastHd;
    float headingResidual;   /* sigma-delta residual for sub-integer diff */
    float headingAccumDt;    /* accumulated dt for heading loop subsampling */
} DualLoopState_t;

typedef struct {
    float straightSpeedTarget;
    float straightSpeedKp;
    float straightSpeedKi;
    float straightSpeedKd;

    float straightHeadingKp;
    float straightHeadingKi;
    float straightHeadingKd;
    float headingTrim;
    float headingIntegralZone;
    float headingIntegralAtten;

    float trackSpeedTarget;
    float trackSpeedKp;
    float trackSpeedKi;
    float trackSpeedKd;

    float speedEntry;
    float speedRampRate;
    float speedOutputLimit;
    float speedFeedforwardGain;
    float pidDerivLpfAlpha;
    int16_t speedCoreSlewStep;
} DualLoop_RuntimeConfig_t;

extern DualLoop_RuntimeConfig_t g_dualLoopCfg;

void PID_Init(PID_Instance_t *pid, float kp, float ki, float kd, float outLimit);
void PID_Reset(PID_Instance_t *pid);
float PID_Compute(PID_Instance_t *pid, float error, float dt);
void PID_SetParams(PID_Instance_t *pid, float kp, float ki, float kd);
float PID_ComputeDOB(PID_Instance_t *pid, float error, float measurement, float dt);

void DualLoop_Init(DualLoopState_t *state);
void DualLoop_LoadStraightDefaults(DualLoopState_t *state);
void DualLoop_LoadTrackDefaults(DualLoopState_t *state);
void DualLoop_ResetAll(DualLoopState_t *state);
void DualLoop_ResetRuntimeConfig(void);
uint8_t DualLoop_ParamSet(DualLoopState_t *state, ControlMode_t activeMode, const char *key, float value, float *appliedValue);
uint8_t DualLoop_ParamGet(const DualLoopState_t *state, ControlMode_t activeMode, const char *key, float *value);
void DualLoop_ParamList(char *out, uint16_t outSize);

/** Compute speed PID only (shared by straight & tracking modes).
 *  Returns pwmCore and stores it in state->pwmCore. */
int16_t DualLoop_ComputeSpeed(DualLoopState_t *state, float avgSpeed, float dt);

void DualLoop_ComputeStraight(DualLoopState_t *state, float avgSpeed, float yaw, float yawRate, float gyroZ, float dt);

void DualLoop_ApplySpeedRamp(DualLoopState_t *state, float dt);

#endif
