#include "AppPid.h"

static float app_pid_clamp(float value, float minValue, float maxValue)
{
    if (value < minValue) {
        return minValue;
    }
    if (value > maxValue) {
        return maxValue;
    }
    return value;
}

void AppPid_Init(AppPidController_t *pid,
                 const AppPidGains_t *gains,
                 float outputMin,
                 float outputMax,
                 float integralMin,
                 float integralMax)
{
    if (pid == 0) {
        return;
    }

    pid->gains.kp = 0.0f;
    pid->gains.ki = 0.0f;
    pid->gains.kd = 0.0f;
    pid->integral = 0.0f;
    pid->prevError = 0.0f;
    pid->lastOutput = 0.0f;
    pid->outputMin = outputMin;
    pid->outputMax = outputMax;
    pid->integralMin = integralMin;
    pid->integralMax = integralMax;

    if (gains != 0) {
        pid->gains = *gains;
    }
}

void AppPid_Reset(AppPidController_t *pid)
{
    if (pid == 0) {
        return;
    }

    pid->integral = 0.0f;
    pid->prevError = 0.0f;
    pid->lastOutput = 0.0f;
}

void AppPid_SetGains(AppPidController_t *pid, const AppPidGains_t *gains)
{
    if ((pid == 0) || (gains == 0)) {
        return;
    }

    pid->gains = *gains;
}

float AppPid_Step(AppPidController_t *pid, float error, float dtSeconds)
{
    float derivative = 0.0f;

    if (dtSeconds > 0.0f) {
        derivative = (error - pid->prevError) / dtSeconds;
    }

    return AppPid_StepWithDerivative(pid, error, derivative, dtSeconds);
}

float AppPid_StepWithDerivative(AppPidController_t *pid, float error, float derivative, float dtSeconds)
{
    float pTerm;
    float iTerm;
    float dTerm;
    float output;

    if (pid == 0) {
        return 0.0f;
    }

    if (dtSeconds > 0.0f) {
        pid->integral += error * dtSeconds;
    }

    iTerm = pid->gains.ki * pid->integral;
    iTerm = app_pid_clamp(iTerm, pid->integralMin, pid->integralMax);
    if (pid->gains.ki != 0.0f) {
        pid->integral = iTerm / pid->gains.ki;
    } else {
        pid->integral = 0.0f;
        iTerm = 0.0f;
    }

    pTerm = pid->gains.kp * error;
    dTerm = pid->gains.kd * derivative;
    output = pTerm + iTerm + dTerm;
    output = app_pid_clamp(output, pid->outputMin, pid->outputMax);

    pid->prevError = error;
    pid->lastOutput = output;
    return output;
}
