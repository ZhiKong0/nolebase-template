#include "PID.h"

static float clampf(float v, float lo, float hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

void PID_Init(PID_t *pid, float kp, float ki, float kd, float outLimit) {
    pid->Kp = kp;
    pid->Ki = ki;
    pid->Kd = kd;
    pid->outputLimit = outLimit;
    PID_Reset(pid);
}

void PID_Reset(PID_t *pid) {
    pid->output = 0.0f;
    pid->e1 = 0.0f;
    pid->e2 = 0.0f;
}

float PID_CalcIncremental(PID_t *pid, float err) {
    float de = err - pid->e1;
    float dde = err - 2.0f * pid->e1 + pid->e2;

    pid->output += pid->Kp * de + pid->Ki * err + pid->Kd * dde;

    if (pid->outputLimit < 0) pid->outputLimit = -pid->outputLimit;
    pid->output = clampf(pid->output, -pid->outputLimit, pid->outputLimit);

    pid->e2 = pid->e1;
    pid->e1 = err;
    return pid->output;
}
