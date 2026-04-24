#include "pid_controller.h"
#include "config.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

DualLoop_RuntimeConfig_t g_dualLoopCfg;

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

static float dualloop_clamp_paramf(float v, float lo, float hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static void dualloop_load_default_runtime_config(void)
{
    g_dualLoopCfg.straightSpeedTarget = PID_STRAIGHT_SPEED_TARGET;
    g_dualLoopCfg.straightSpeedKp = PID_STRAIGHT_SPEED_KP;
    g_dualLoopCfg.straightSpeedKi = PID_STRAIGHT_SPEED_KI;
    g_dualLoopCfg.straightSpeedKd = PID_STRAIGHT_SPEED_KD;

    g_dualLoopCfg.straightHeadingKp = PID_STRAIGHT_HEADING_KP;
    g_dualLoopCfg.straightHeadingKi = PID_STRAIGHT_HEADING_KI;
    g_dualLoopCfg.straightHeadingKd = PID_STRAIGHT_HEADING_KD;
    g_dualLoopCfg.headingTrim = HEADING_TRIM;
    g_dualLoopCfg.headingIntegralZone = HEADING_INTEGRAL_ZONE;
    g_dualLoopCfg.headingIntegralAtten = HEADING_INTEGRAL_ATTEN;

    g_dualLoopCfg.trackSpeedTarget = PID_TRACK_SPEED_TARGET;
    g_dualLoopCfg.trackSpeedKp = PID_TRACK_SPEED_KP;
    g_dualLoopCfg.trackSpeedKi = PID_TRACK_SPEED_KI;
    g_dualLoopCfg.trackSpeedKd = PID_TRACK_SPEED_KD;

    g_dualLoopCfg.speedEntry = SPEED_ENTRY;
    g_dualLoopCfg.speedRampRate = SPEED_RAMP_RATE;
    g_dualLoopCfg.speedOutputLimit = SPEED_OUTPUT_LIMIT;
    g_dualLoopCfg.speedFeedforwardGain = SPEED_FEEDFORWARD_GAIN;
    g_dualLoopCfg.pidDerivLpfAlpha = PID_DERIV_LPF_ALPHA;
    g_dualLoopCfg.speedCoreSlewStep = SPEED_CORE_SLEW_STEP;
}

void DualLoop_ResetRuntimeConfig(void)
{
    dualloop_load_default_runtime_config();
}

static void dualloop_apply_active_params(DualLoopState_t *state, ControlMode_t activeMode)
{
    if (state == 0)
        return;

    state->headingTrim = g_dualLoopCfg.headingTrim;
    state->feedforwardGain = g_dualLoopCfg.speedFeedforwardGain;

    if (activeMode == MODE_TRACK)
    {
        state->targetSpeed = g_dualLoopCfg.trackSpeedTarget;
        PID_SetParams(&state->speedPID,
                      g_dualLoopCfg.trackSpeedKp,
                      g_dualLoopCfg.trackSpeedKi,
                      g_dualLoopCfg.trackSpeedKd);
        state->speedPID.outputLimit = g_dualLoopCfg.speedOutputLimit;
        state->speedPID.integralLimit = g_dualLoopCfg.speedOutputLimit * 0.5f;
    }
    else if (activeMode == MODE_STRAIGHT)
    {
        state->targetSpeed = g_dualLoopCfg.straightSpeedTarget;
        PID_SetParams(&state->speedPID,
                      g_dualLoopCfg.straightSpeedKp,
                      g_dualLoopCfg.straightSpeedKi,
                      g_dualLoopCfg.straightSpeedKd);
        state->speedPID.outputLimit = g_dualLoopCfg.speedOutputLimit;
        state->speedPID.integralLimit = g_dualLoopCfg.speedOutputLimit * 0.5f;
        PID_SetParams(&state->headingPID,
                      g_dualLoopCfg.straightHeadingKp,
                      g_dualLoopCfg.straightHeadingKi,
                      g_dualLoopCfg.straightHeadingKd);
        state->headingPID.outputLimit = (float)MOTOR_DIFF_MAX;
        state->headingPID.integralLimit = (float)MOTOR_DIFF_MAX * 0.5f;
    }
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
    pid->filteredDerivative += g_dualLoopCfg.pidDerivLpfAlpha * (rawD - pid->filteredDerivative);

    out = p + pid->integral + pid->filteredDerivative;
    out = clampf(out, -pid->outputLimit, pid->outputLimit);
    pid->output = out;
    return out;
}

/* ========== DualLoop State ========== */

void DualLoop_Init(DualLoopState_t *state)
{
    if (!state) return;
    DualLoop_ResetRuntimeConfig();
    state->targetSpeed = SPEED_TARGET_DEFAULT;
    state->currentSpeed = 0.0f;
    state->speedRampTarget = g_dualLoopCfg.speedEntry;
    state->targetYaw = 0.0f;
    state->headingTrim = g_dualLoopCfg.headingTrim;
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
    state->feedforwardGain = g_dualLoopCfg.speedFeedforwardGain;
    DualLoop_LoadStraightDefaults(state);
}

void DualLoop_LoadStraightDefaults(DualLoopState_t *state)
{
    if (!state) return;
    state->targetSpeed = g_dualLoopCfg.straightSpeedTarget;
    PID_Init(&state->speedPID,
             g_dualLoopCfg.straightSpeedKp, g_dualLoopCfg.straightSpeedKi, g_dualLoopCfg.straightSpeedKd,
             g_dualLoopCfg.speedOutputLimit);
    PID_Init(&state->headingPID,
             g_dualLoopCfg.straightHeadingKp, g_dualLoopCfg.straightHeadingKi, g_dualLoopCfg.straightHeadingKd,
             (float)MOTOR_DIFF_MAX);
    state->headingTrim = g_dualLoopCfg.headingTrim;
    state->feedforwardGain = g_dualLoopCfg.speedFeedforwardGain;
}

void DualLoop_LoadTrackDefaults(DualLoopState_t *state)
{
    if (!state) return;
    state->targetSpeed = g_dualLoopCfg.trackSpeedTarget;
    PID_Init(&state->speedPID,
             g_dualLoopCfg.trackSpeedKp, g_dualLoopCfg.trackSpeedKi, g_dualLoopCfg.trackSpeedKd,
             g_dualLoopCfg.speedOutputLimit);
    state->feedforwardGain = g_dualLoopCfg.speedFeedforwardGain;
}

void DualLoop_ResetAll(DualLoopState_t *state)
{
    if (!state) return;
    PID_Reset(&state->speedPID);
    PID_Reset(&state->headingPID);
    state->currentSpeed = 0.0f;
    state->speedRampTarget = g_dualLoopCfg.speedEntry;
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

    step = g_dualLoopCfg.speedRampRate * dt;
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
        if (total > g_dualLoopCfg.speedOutputLimit)  total = g_dualLoopCfg.speedOutputLimit;
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
        core = clamp_toward_i16(state->pwmCore, targetCore, g_dualLoopCfg.speedCoreSlewStep);

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
            if (absErr > g_dualLoopCfg.headingIntegralZone)
                ki_eff *= g_dualLoopCfg.headingIntegralAtten;
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

uint8_t DualLoop_ParamSet(DualLoopState_t *state, ControlMode_t activeMode, const char *key, float value, float *appliedValue)
{
    float applied;

    if (key == 0)
        return 0u;

    applied = value;

    if (strcmp(key, "straight.speed_target") == 0)
        g_dualLoopCfg.straightSpeedTarget = applied = dualloop_clamp_paramf(value, 0.0f, 120.0f);
    else if (strcmp(key, "straight.speed_kp") == 0)
        g_dualLoopCfg.straightSpeedKp = applied = dualloop_clamp_paramf(value, 0.0f, 20.0f);
    else if (strcmp(key, "straight.speed_ki") == 0)
        g_dualLoopCfg.straightSpeedKi = applied = dualloop_clamp_paramf(value, 0.0f, 10.0f);
    else if (strcmp(key, "straight.speed_kd") == 0)
        g_dualLoopCfg.straightSpeedKd = applied = dualloop_clamp_paramf(value, 0.0f, 20.0f);
    else if (strcmp(key, "straight.heading_kp") == 0)
        g_dualLoopCfg.straightHeadingKp = applied = dualloop_clamp_paramf(value, 0.0f, 40.0f);
    else if (strcmp(key, "straight.heading_ki") == 0)
        g_dualLoopCfg.straightHeadingKi = applied = dualloop_clamp_paramf(value, 0.0f, 10.0f);
    else if (strcmp(key, "straight.heading_kd") == 0)
        g_dualLoopCfg.straightHeadingKd = applied = dualloop_clamp_paramf(value, 0.0f, 40.0f);
    else if (strcmp(key, "heading.trim") == 0)
        g_dualLoopCfg.headingTrim = applied = dualloop_clamp_paramf(value, -20.0f, 20.0f);
    else if (strcmp(key, "heading.integral_zone") == 0)
        g_dualLoopCfg.headingIntegralZone = applied = dualloop_clamp_paramf(value, 0.1f, 20.0f);
    else if (strcmp(key, "heading.integral_atten") == 0)
        g_dualLoopCfg.headingIntegralAtten = applied = dualloop_clamp_paramf(value, 0.0f, 1.0f);
    else if (strcmp(key, "track.speed_target") == 0)
        g_dualLoopCfg.trackSpeedTarget = applied = dualloop_clamp_paramf(value, 0.0f, 120.0f);
    else if (strcmp(key, "track.speed_kp") == 0)
        g_dualLoopCfg.trackSpeedKp = applied = dualloop_clamp_paramf(value, 0.0f, 20.0f);
    else if (strcmp(key, "track.speed_ki") == 0)
        g_dualLoopCfg.trackSpeedKi = applied = dualloop_clamp_paramf(value, 0.0f, 10.0f);
    else if (strcmp(key, "track.speed_kd") == 0)
        g_dualLoopCfg.trackSpeedKd = applied = dualloop_clamp_paramf(value, 0.0f, 20.0f);
    else if (strcmp(key, "system.speed_entry") == 0)
        g_dualLoopCfg.speedEntry = applied = dualloop_clamp_paramf(value, 0.0f, 120.0f);
    else if (strcmp(key, "system.speed_ramp_rate") == 0)
        g_dualLoopCfg.speedRampRate = applied = dualloop_clamp_paramf(value, 0.1f, 200.0f);
    else if (strcmp(key, "system.speed_output_limit") == 0)
        g_dualLoopCfg.speedOutputLimit = applied = dualloop_clamp_paramf(value, 50.0f, 1000.0f);
    else if (strcmp(key, "system.speed_feedforward_gain") == 0)
        g_dualLoopCfg.speedFeedforwardGain = applied = dualloop_clamp_paramf(value, 0.0f, 30.0f);
    else if (strcmp(key, "system.pid_deriv_lpf") == 0)
        g_dualLoopCfg.pidDerivLpfAlpha = applied = dualloop_clamp_paramf(value, 0.01f, 0.99f);
    else if (strcmp(key, "system.speed_core_slew_step") == 0)
    {
        applied = dualloop_clamp_paramf(value, 0.0f, 200.0f);
        g_dualLoopCfg.speedCoreSlewStep = (int16_t)(applied + 0.5f);
        applied = (float)g_dualLoopCfg.speedCoreSlewStep;
    }
    else
        return 0u;

    dualloop_apply_active_params(state, activeMode);
    if (state != 0 && state->speedRampTarget < g_dualLoopCfg.speedEntry)
        state->speedRampTarget = g_dualLoopCfg.speedEntry;

    if (appliedValue != 0)
        *appliedValue = applied;
    return 1u;
}

uint8_t DualLoop_ParamGet(const DualLoopState_t *state, ControlMode_t activeMode, const char *key, float *value)
{
    (void)state;
    (void)activeMode;

    if (key == 0 || value == 0)
        return 0u;

    if (strcmp(key, "straight.speed_target") == 0)
        *value = g_dualLoopCfg.straightSpeedTarget;
    else if (strcmp(key, "straight.speed_kp") == 0)
        *value = g_dualLoopCfg.straightSpeedKp;
    else if (strcmp(key, "straight.speed_ki") == 0)
        *value = g_dualLoopCfg.straightSpeedKi;
    else if (strcmp(key, "straight.speed_kd") == 0)
        *value = g_dualLoopCfg.straightSpeedKd;
    else if (strcmp(key, "straight.heading_kp") == 0)
        *value = g_dualLoopCfg.straightHeadingKp;
    else if (strcmp(key, "straight.heading_ki") == 0)
        *value = g_dualLoopCfg.straightHeadingKi;
    else if (strcmp(key, "straight.heading_kd") == 0)
        *value = g_dualLoopCfg.straightHeadingKd;
    else if (strcmp(key, "heading.trim") == 0)
        *value = g_dualLoopCfg.headingTrim;
    else if (strcmp(key, "heading.integral_zone") == 0)
        *value = g_dualLoopCfg.headingIntegralZone;
    else if (strcmp(key, "heading.integral_atten") == 0)
        *value = g_dualLoopCfg.headingIntegralAtten;
    else if (strcmp(key, "track.speed_target") == 0)
        *value = g_dualLoopCfg.trackSpeedTarget;
    else if (strcmp(key, "track.speed_kp") == 0)
        *value = g_dualLoopCfg.trackSpeedKp;
    else if (strcmp(key, "track.speed_ki") == 0)
        *value = g_dualLoopCfg.trackSpeedKi;
    else if (strcmp(key, "track.speed_kd") == 0)
        *value = g_dualLoopCfg.trackSpeedKd;
    else if (strcmp(key, "system.speed_entry") == 0)
        *value = g_dualLoopCfg.speedEntry;
    else if (strcmp(key, "system.speed_ramp_rate") == 0)
        *value = g_dualLoopCfg.speedRampRate;
    else if (strcmp(key, "system.speed_output_limit") == 0)
        *value = g_dualLoopCfg.speedOutputLimit;
    else if (strcmp(key, "system.speed_feedforward_gain") == 0)
        *value = g_dualLoopCfg.speedFeedforwardGain;
    else if (strcmp(key, "system.pid_deriv_lpf") == 0)
        *value = g_dualLoopCfg.pidDerivLpfAlpha;
    else if (strcmp(key, "system.speed_core_slew_step") == 0)
        *value = (float)g_dualLoopCfg.speedCoreSlewStep;
    else
        return 0u;

    return 1u;
}

void DualLoop_ParamList(char *out, uint16_t outSize)
{
    if (out == 0 || outSize == 0u)
        return;

    snprintf(out, outSize,
             "straight.speed_target,straight.speed_kp,straight.speed_ki,straight.speed_kd,"
             "straight.heading_kp,straight.heading_ki,straight.heading_kd,"
             "heading.trim,heading.integral_zone,heading.integral_atten,"
             "track.speed_target,track.speed_kp,track.speed_ki,track.speed_kd,"
             "system.speed_entry,system.speed_ramp_rate,system.speed_output_limit,"
             "system.speed_feedforward_gain,system.pid_deriv_lpf,system.speed_core_slew_step");
}
