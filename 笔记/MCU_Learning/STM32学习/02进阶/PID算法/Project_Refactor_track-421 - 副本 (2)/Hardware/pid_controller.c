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

static int16_t clamp_toward_i16(int16_t current, int16_t target, int16_t step)
{
    if (step <= 0)
        return target;
    if (target > (int16_t)(current + step))
        return (int16_t)(current + step);
    if (target < (int16_t)(current - step))
        return (int16_t)(current - step);
    return target;
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
    state->headingTrim = HEADING_TRIM;
    state->currentYaw = 0.0f;
    state->leftPWM = 0;
    state->rightPWM = 0;
    state->pwmCore = 0;
    state->headingDiffPWM = 0;
    state->dTermPostDZ = 0;
    state->lastHp = 0.0f;
    state->lastHi = 0.0f;
    state->lastHd = 0.0f;
    state->headingResidual = 0.0f;
    state->headingAccumDt = 0.0f;
    state->feedforwardGain = SPEED_FEEDFORWARD_GAIN;
    DualLoop_LoadStraightDefaults(state);
}

void DualLoop_LoadStraightDefaults(DualLoopState_t *state)
{
    if (!state) return;
    state->targetSpeed = PID_STRAIGHT_SPEED_TARGET;
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
    state->targetSpeed = PID_TRACK_SPEED_TARGET;
    PID_Init(&state->speedPID,
             PID_TRACK_SPEED_KP, PID_TRACK_SPEED_KI, PID_TRACK_SPEED_KD,
             SPEED_OUTPUT_LIMIT);
    state->feedforwardGain = SPEED_FEEDFORWARD_GAIN;
}

void DualLoop_ResetAll(DualLoopState_t *state)
{
    if (!state) return;
    PID_Reset(&state->speedPID);
    PID_Reset(&state->headingPID);
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
    state->headingResidual = 0.0f;
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

/* ========== Speed PID (shared by straight & tracking) ========== */

int16_t DualLoop_ComputeSpeed(DualLoopState_t *state, float avgSpeed, float dt)
{
    float speedErr;
    int16_t core;
    int16_t targetCore;

    if (!state || dt <= 0.0f) return 0;

    state->currentSpeed = avgSpeed;
    DualLoop_ApplySpeedRamp(state, dt);

    speedErr = state->speedRampTarget - avgSpeed;
    {
        float ff = state->speedRampTarget * state->feedforwardGain;
        float pid_out = PID_ComputeDOB(&state->speedPID, speedErr, avgSpeed, dt);
        float total = ff + pid_out;
        if (total > SPEED_OUTPUT_LIMIT)  total = SPEED_OUTPUT_LIMIT;
        if (total < 1.0f) {
            total = 1.0f;
            state->speedPID.integral = 1.0f - ff
                                     - (state->speedPID.kp * speedErr)
                                     - state->speedPID.filteredDerivative;
        }
        targetCore = (int16_t)(total + 0.5f);
    }

    if (state->pwmCore == 0)
        core = targetCore;
    else
        core = clamp_toward_i16(state->pwmCore, targetCore, SPEED_CORE_SLEW_STEP);

    state->pwmCore = core;
    return core;
}

/* ========== Straight Mode: Speed + Heading ========== */

void DualLoop_ComputeStraight(DualLoopState_t *state, float avgSpeed, float yaw, float yawRate, float gyroZ, float dt)
{
    float headingErr;
    int16_t core, diff;

    if (!state || dt <= 0.0f) return;

    state->currentYaw = yaw;
    core = DualLoop_ComputeSpeed(state, avgSpeed, dt);

    headingErr = wrap_deg(state->targetYaw + state->headingTrim - yaw);
    {
        float hp = state->headingPID.kp * headingErr;
        float hd = -state->headingPID.kd * gyroZ;
        float ki_eff = state->headingPID.ki;
        {
            float absErr = (headingErr >= 0.0f) ? headingErr : -headingErr;
            if (absErr > HEADING_INTEGRAL_ZONE)
                ki_eff *= HEADING_INTEGRAL_ATTEN;
        }
        float absCore = (float)(core >= 0 ? core : -core);
        float base = (absCore > 0.0f && absCore < (float)MOTOR_DEADZONE)
                     ? (float)MOTOR_DEADZONE : absCore;
        float diff_max = base * 0.40f;
        if (diff_max < 5.0f) diff_max = 5.0f;
        if (diff_max > state->headingPID.outputLimit)
            diff_max = state->headingPID.outputLimit;
        float hi = state->headingPID.integral + ki_eff * headingErr * dt;
        hi = clampf(hi, -diff_max, diff_max);
        state->headingPID.integral = hi;
        float hout = clampf(hp + hi + hd, -diff_max, diff_max);
        state->headingPID.output = hout;
        diff = (hout >= 0.0f) ? (int16_t)(hout + 0.5f) : (int16_t)(hout - 0.5f);
        {
            float hd_rpt = clampf(hd, -200.0f, 200.0f);
            state->dTermPostDZ = (hd_rpt >= 0.0f) ? (int16_t)(hd_rpt + 0.5f) : (int16_t)(hd_rpt - 0.5f);
        }
        state->lastHp = hp;
        state->lastHi = hi;
        state->lastHd = hd;
    }
    state->headingDiffPWM = diff;
    /* left/right PWM fields updated after motor driver applies deadzone */
}
