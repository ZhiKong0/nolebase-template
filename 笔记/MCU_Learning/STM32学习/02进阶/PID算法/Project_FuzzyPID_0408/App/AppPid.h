#ifndef __APP_PID_H
#define __APP_PID_H

#include "AppTypes.h"

typedef struct {
    AppPidGains_t gains;
    float integral;
    float prevError;
    float lastOutput;
    float outputMin;
    float outputMax;
    float integralMin;
    float integralMax;
} AppPidController_t;

void AppPid_Init(AppPidController_t *pid,
                 const AppPidGains_t *gains,
                 float outputMin,
                 float outputMax,
                 float integralMin,
                 float integralMax);
void AppPid_Reset(AppPidController_t *pid);
void AppPid_SetGains(AppPidController_t *pid, const AppPidGains_t *gains);
float AppPid_Step(AppPidController_t *pid, float error, float dtSeconds);
float AppPid_StepWithDerivative(AppPidController_t *pid, float error, float derivative, float dtSeconds);

#endif
