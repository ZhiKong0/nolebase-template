#include "pid_controller.h"
#include "config.h"
#include <math.h>

static float clampf(float v, float lo, float hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static float wrap_deg(float deg)
{
    while (deg > 180.0f) deg -= 360.0f;
    while (deg < -180.0f) deg += 360.0f;
    return deg;
}

/* ========== Generic PID ========== */

void PID_Init(PID_Instance_t *pid, float kp, float ki, float kd, float outLimit)
{
    if (!pid) return;
    pid->kp = kp;
    pid->ki = ki;
    pid->kd = kd;
    pid->integral = 0.0f;
    pid->prevError = 0.0f;
    pid->prevMeasurement = 0.0f;
    pid->filteredDerivative = 0.0f;
    pid->output = 0.0f;
    pid->outputLimit = outLimit;
    pid->integralLimit = outLimit * 0.5f;
}

void PID_Reset(PID_Instance_t *pid)
{
    if (!pid) return;
    pid->integral = 0.0f;
    pid->prevError = 0.0f;
    pid->prevMeasurement = 0.0f;
    pid->filteredDerivative = 0.0f;
    pid->output = 0.0f;
}

float PID_Compute(PID_Instance_t *pid, float error, float dt)
{
    float p, d, out;

    if (!pid || dt <= 0.0f) return 0.0f;

    p = pid->kp * error;
    pid->integral += pid->ki * error * dt;
    pid->integral = clampf(pid->integral, -pid->integralLimit, pid->integralLimit);
    d = pid->kd * (error - pid->prevError) / dt;
    pid->prevError = error;

    out = p + pid->integral + d;
    out = clampf(out, -pid->outputLimit, pid->outputLimit);
    pid->output = out;
    return out;
}

void PID_SetParams(PID_Instance_t *pid, float kp, float ki, float kd)
{
    if (!pid) return;
    pid->kp = kp;
    pid->ki = ki;
    pid->kd = kd;
}

float PID_ComputeDOB(PID_Instance_t *pid, float error, float measurement, float dt)
{
    float p, rawD, out;

    if (!pid || dt <= 0.0f) return 0.0f;

    p = pid->kp * error;
    pid->integral += pid->ki * error * dt;
    pid->integral = clampf(pid->integral, -pid->integralLimit, pid->integralLimit);

    rawD = -pid->kd * (measurement - pid->prevMeasurement) / dt;
    pid->prevMeasurement = measurement;
    pid->filteredDerivative += PID_DERIV_LPF_ALPHA * (rawD - pid->filteredDerivative);

    out = p + pid->integral + pid->filteredDerivative;
    out = clampf(out, -pid->outputLimit, pid->outputLimit);
    pid->output = out;
    return out;
}

/* ========== DualLoop State ========== */

void DualLoop_Init(DualLoopState_t *state)
{
    if (!state) return;
    state->targetSpeed = SPEED_TARGET_DEFAULT;
    state->currentSpeed = 0.0f;
    state->speedRampTarget = SPEED_ENTRY;
    state->targetYaw = 0.0f;
    state->currentYaw = 0.0f;
    state->linePosition = 0.0f;
    state->leftPWM = 0;
    state->rightPWM = 0;
    state->pwmCore = 0;
    state->headingDiffPWM = 0;
    state->dTermPostDZ = 0;
    state->lastHp = 0.0f;
    state->lastHi = 0.0f;
    state->lastHd = 0.0f;
    state->feedforwardGain = SPEED_FEEDFORWARD_GAIN;
    DualLoop_LoadStraightDefaults(state);
}

void DualLoop_LoadStraightDefaults(DualLoopState_t *state)
{
    if (!state) return;
    PID_Init(&state->speedPID,
             PID_STRAIGHT_SPEED_KP, PID_STRAIGHT_SPEED_KI, PID_STRAIGHT_SPEED_KD,
             SPEED_OUTPUT_LIMIT);
    PID_Init(&state->headingPID,
             PID_STRAIGHT_HEADING_KP, PID_STRAIGHT_HEADING_KI, PID_STRAIGHT_HEADING_KD,
             (float)MOTOR_DIFF_MAX);
    state->feedforwardGain = SPEED_FEEDFORWARD_GAIN;
}

void DualLoop_LoadTrackDefaults(DualLoopState_t *state)
{
    if (!state) return;
    PID_Init(&state->speedPID,
             PID_TRACK_SPEED_KP, PID_TRACK_SPEED_KI, PID_TRACK_SPEED_KD,
             SPEED_OUTPUT_LIMIT);
    PID_Init(&state->linePID,
             PID_TRACK_LINE_KP, PID_TRACK_LINE_KI, PID_TRACK_LINE_KD,
             (float)MOTOR_DIFF_MAX);
}

void DualLoop_ResetAll(DualLoopState_t *state)
{
    if (!state) return;
    PID_Reset(&state->speedPID);
    PID_Reset(&state->headingPID);
    PID_Reset(&state->linePID);
    state->currentSpeed = 0.0f;
    state->speedRampTarget = SPEED_ENTRY;
    state->leftPWM = 0;
    state->rightPWM = 0;
    state->pwmCore = 0;
    state->headingDiffPWM = 0;
    state->dTermPostDZ = 0;
    state->lastHp = 0.0f;
    state->lastHi = 0.0f;
    state->lastHd = 0.0f;
}

void DualLoop_ApplySpeedRamp(DualLoopState_t *state, float dt)
{
    float step;

    if (!state || dt <= 0.0f) return;

    step = SPEED_RAMP_RATE * dt;
    if (state->speedRampTarget < state->targetSpeed) {
        state->speedRampTarget += step;
        if (state->speedRampTarget > state->targetSpeed)
            state->speedRampTarget = state->targetSpeed;
    } else if (state->speedRampTarget > state->targetSpeed) {
        state->speedRampTarget -= step;
        if (state->speedRampTarget < state->targetSpeed)
            state->speedRampTarget = state->targetSpeed;
    }
}

/* ========== Straight Mode: Speed + Heading ========== */

void DualLoop_ComputeStraight(DualLoopState_t *state, float avgSpeed, float yaw, float yawRate, float gyroZ, float dt)
{
    float speedErr, headingErr;
    int16_t core, diff, left, right;
    int16_t pwmMax = MOTOR_PWM_MAX;

    if (!state || dt <= 0.0f) return;

    state->currentSpeed = avgSpeed;
    state->currentYaw = yaw;

    DualLoop_ApplySpeedRamp(state, dt);

    speedErr = state->speedRampTarget - avgSpeed;
    {
        float ff = state->speedRampTarget * state->feedforwardGain;
        float pid_out = PID_ComputeDOB(&state->speedPID, speedErr, avgSpeed, dt);
        float total = ff + pid_out;
        if (total > SPEED_OUTPUT_LIMIT)  total = SPEED_OUTPUT_LIMIT;
        if (total < -SPEED_OUTPUT_LIMIT) total = -SPEED_OUTPUT_LIMIT;
        core = (total >= 0.0f) ? (int16_t)(total + 0.5f) : (int16_t)(total - 0.5f);
    }
    state->pwmCore = core;

    headingErr = wrap_deg(state->targetYaw - yaw);
    {
        float hp = state->headingPID.kp * headingErr;
        /* D-term: oppose angular velocity to damp oscillation.
           No deadzone — D must provide continuous linear damping at
           ALL rates for proper stability.  Noise is handled by the
           rate-clamp ±30°/s and LPF α=0.7 in main.c. */
        float hd = -state->headingPID.kd * gyroZ;
        float hi = state->headingPID.integral + state->headingPID.ki * headingErr * dt;
        hi = clampf(hi, -state->headingPID.integralLimit, state->headingPID.integralLimit);
        state->headingPID.integral = hi;
        /* Dynamic diff limit: keep both motors spinning to avoid
           nonlinear gain discontinuity when one motor stalls (diff > core).
           This is what makes AKD truly scalable. */
        float diff_max = (float)(core >= 0 ? core : -core) - 1.0f;
        if (diff_max < 1.0f) diff_max = 1.0f;
        if (diff_max > state->headingPID.outputLimit)
            diff_max = state->headingPID.outputLimit;
        float hout = clampf(hp + hi + hd, -diff_max, diff_max);
        state->headingPID.output = hout;
        diff = (hout >= 0.0f) ? (int16_t)(hout + 0.5f) : (int16_t)(hout - 0.5f);
        /* dp = D contribution for telemetry */
        float hd_rpt = clampf(hd, -20.0f, 20.0f);
        state->dTermPostDZ = (hd_rpt >= 0.0f) ? (int16_t)(hd_rpt + 0.5f) : (int16_t)(hd_rpt - 0.5f);
        state->lastHp = hp;
        state->lastHi = hi;
        state->lastHd = hd;
    }
    state->headingDiffPWM = diff;

    left  = (int16_t)(core + diff);
    right = (int16_t)(core - diff);

    if (core > 0) {
        if (left < 0) left = 0;
        if (right < 0) right = 0;
    } else if (core < 0) {
        if (left > 0) left = 0;
        if (right > 0) right = 0;
    }
    /* core==0: allow diff to drive wheels for heading correction */

    if (left > pwmMax) left = pwmMax;
    if (left < -pwmMax) left = -pwmMax;
    if (right > pwmMax) right = pwmMax;
    if (right < -pwmMax) right = -pwmMax;

    state->leftPWM = left;
    state->rightPWM = right;
}

/* ========== Track Mode: Speed + Line Following ========== */

void DualLoop_ComputeTrack(DualLoopState_t *state, float leftSpeed, float rightSpeed,
                           float linePos, float dt)
{
    float avgSpeed, speedErr;
    float lineErr;
    int16_t core, diff, left, right;
    int16_t pwmMax = MOTOR_PWM_MAX;

    if (!state || dt <= 0.0f) return;

    avgSpeed = (leftSpeed + rightSpeed) * 0.5f;
    state->currentSpeed = avgSpeed;
    state->linePosition = linePos;

    DualLoop_ApplySpeedRamp(state, dt);

    speedErr = state->speedRampTarget - avgSpeed;
    {
        float ff = state->speedRampTarget * state->feedforwardGain;
        float pid_out = PID_ComputeDOB(&state->speedPID, speedErr, avgSpeed, dt);
        float total = ff + pid_out;
        if (total > SPEED_OUTPUT_LIMIT)  total = SPEED_OUTPUT_LIMIT;
        if (total < -SPEED_OUTPUT_LIMIT) total = -SPEED_OUTPUT_LIMIT;
        core = (total >= 0.0f) ? (int16_t)(total + 0.5f) : (int16_t)(total - 0.5f);
    }
    state->pwmCore = core;

    state->dTermPostDZ = 0;

    lineErr = -linePos;
    diff = (int16_t)PID_Compute(&state->linePID, lineErr, dt);
    state->headingDiffPWM = diff;

    left  = (int16_t)(core + diff);
    right = (int16_t)(core - diff);

    if (left > pwmMax) left = pwmMax;
    if (left < -pwmMax) left = -pwmMax;
    if (right > pwmMax) right = pwmMax;
    if (right < -pwmMax) right = -pwmMax;

    state->leftPWM = left;
    state->rightPWM = right;
}
