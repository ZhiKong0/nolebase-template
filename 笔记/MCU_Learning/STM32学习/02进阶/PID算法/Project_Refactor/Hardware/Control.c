#include "Control.h"
#include "Motor.h"
#include "VOFA.h"
#include "stm32f10x_it.h"
#include "Delay.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

extern volatile uint8_t g_mainSelectedMode;

#define CONTROL_LOOP_PERIOD_MS             10
#define CONTROL_LOOP_DT_S                  0.010f
#define ANGLE_LOOP_PERIOD_MS               5u
#define VOFA_SEND_PERIOD_MS                200
#define IMU_START_WAIT_MS                  300
#define IMU_UPDATE_PERIOD_S                0.005f
#define BIN_SEND_PERIOD_MS                 10
#define VOFA_MODE_FIREWATER                0u
#define VOFA_MODE_JUSTFLOAT3               3u
#define VOFA_MODE_JUSTFLOAT5               5u
#define CONTROL_MAX_PWM                    60
#define SPEED_OUTPUT_LIMIT_PWM             ((float)CONTROL_MAX_PWM)
#define SPEED_INTEGRAL_LIMIT               400.0f
#define ANGLE_OUTPUT_LIMIT_PWM             ((float)CONTROL_MAX_PWM)
#define ANGLE_INTEGRAL_LIMIT               200.0f
#define ANGLE_DERIVATIVE_LIMIT_DPS         25.0f
#define STABLE_DEFAULT_TARGET_SPEED        35.0f
#define STABLE_DEFAULT_SPEED_KP            0.600f
#define STABLE_DEFAULT_SPEED_KI            0.012f
#define STABLE_DEFAULT_SPEED_KD            0.000f
#define STABLE_DEFAULT_ANGLE_KP            1.500f
#define STABLE_DEFAULT_ANGLE_KI            0.00f
#define STABLE_DEFAULT_ANGLE_KD            0.120f
#define SPEED_TARGET_RAMP_RATE_PER_S       12.0f
#define SPEED_TARGET_RAMP_ENTRY_SPEED      8.0f
#define MOTOR_OUTPUT_DEADZONE_PWM          11
#define MOTOR_OUTPUT_DEADZONE_THRESHOLD    1.0f
#define MOTOR_OUTPUT_WEAK_SIDE_MIN_ACTIVE_PWM 8
#define MOTOR_OUTPUT_WEAK_SIDE_STRONG_ACTIVE_PWM 7
#define HEADING_STRONG_DIFF_THRESHOLD_PWM  4
#define HEADING_DIFF_LIMIT_PWM             7.0f
#define HEADING_DIFF_OUTPUT_DIVISOR        4
#define HEADING_D_FILTER_ALPHA             0.250f
#define HEADING_D_RAMP_MS                  800u

static uint8_t g_rawEnabled = 0;
static int16_t g_rawPwm = 0;

static uint8_t g_binMode = VOFA_MODE_FIREWATER;

static volatile uint32_t g_okTele = 0;
static volatile uint32_t g_failTele = 0;
static volatile uint32_t g_icmOk = 0;
static volatile uint32_t g_icmFail = 0;

volatile uint32_t g_icmReadOkCount = 0;
volatile uint32_t g_icmReadFailCount = 0;

static int16_t g_imuAx0 = 0;
static int16_t g_imuAy0 = 0;
static int16_t g_imuAz0 = 0;
static int16_t g_imuGx0 = 0;
static int16_t g_imuGy0 = 0;
static int16_t g_imuGz0 = 0;

static uint32_t control_enter_critical(void) {
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    return primask;
}

static void control_exit_critical(uint32_t primask) {
    if (primask == 0u) {
        __enable_irq();
    }
}

static void control_imu_zero_update(ControlSystem_t *sys) {
    g_imuAx0 = sys->icm.accelX;
    g_imuAy0 = sys->icm.accelY;
    g_imuAz0 = sys->icm.accelZ;
    g_imuGx0 = sys->icm.gyroX;
    g_imuGy0 = sys->icm.gyroY;
    g_imuGz0 = sys->icm.gyroZ;
}

static int16_t clamp_i16(int16_t v, int16_t lo, int16_t hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static int32_t clamp_i32(int32_t v, int32_t lo, int32_t hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static float abs_f(float v) {
    return (v < 0.0f) ? -v : v;
}

static float clamp_f(float v, float lo, float hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static int16_t float_to_i16(float v) {
    if (v >= 0.0f) v += 0.5f;
    else v -= 0.5f;
    return (int16_t)v;
}

static float parse_float_after_eq(const char *s) {
    const char *p = strchr(s, '=');
    if (!p) return 0.0f;
    return (float)atof(p + 1);
}

static int16_t parse_int_after_eq(const char *s) {
    return (int16_t)parse_float_after_eq(s);
}

static int16_t control_clamp_signed_pwm(int32_t pwm) {
    return (int16_t)clamp_i32(pwm, -CONTROL_MAX_PWM, CONTROL_MAX_PWM);
}

static float control_wrap_deg180(float v) {
    while (v > 180.0f) v -= 360.0f;
    while (v < -180.0f) v += 360.0f;
    return v;
}

static int16_t control_speed_command_to_pwm(float speedCmd) {
    speedCmd = clamp_f(speedCmd, -SPEED_OUTPUT_LIMIT_PWM, SPEED_OUTPUT_LIMIT_PWM);
    return control_clamp_signed_pwm((int32_t)float_to_i16(speedCmd));
}

static float control_ramp_speed_target(ControlSystem_t *sys, float targetSpeed) {
    float absTarget = abs_f(targetSpeed);
    float entryTarget = SPEED_TARGET_RAMP_ENTRY_SPEED;
    float rampMs;
    float runMs;
    float progress;
    float rampTarget;

    if (absTarget <= 0.0f) {
        return 0.0f;
    }
    if (SPEED_TARGET_RAMP_RATE_PER_S <= 0.0f) {
        return targetSpeed;
    }
    if (!sys->isRunning) {
        return targetSpeed;
    }
    if (entryTarget > absTarget) {
        entryTarget = absTarget;
    }
    if (absTarget <= entryTarget) {
        return targetSpeed;
    }
    if (sys->tickCount <= sys->runStartTick) {
        return (targetSpeed >= 0.0f) ? entryTarget : -entryTarget;
    }

    rampMs = ((absTarget - entryTarget) / SPEED_TARGET_RAMP_RATE_PER_S) * 1000.0f;
    if (rampMs <= 0.0f) {
        return targetSpeed;
    }

    runMs = (float)(sys->tickCount - sys->runStartTick);
    if (runMs >= rampMs) {
        return targetSpeed;
    }

    progress = clamp_f(runMs / rampMs, 0.0f, 1.0f);
    rampTarget = entryTarget + (absTarget - entryTarget) * progress;
    return (targetSpeed >= 0.0f) ? rampTarget : -rampTarget;
}

static int16_t control_quantize_heading_diff(ControlSystem_t *sys, float headingOut) {
    float divisor = (HEADING_DIFF_OUTPUT_DIVISOR > 0) ? (float)HEADING_DIFF_OUTPUT_DIVISOR : 1.0f;
    float scaled = clamp_f(headingOut / divisor, -HEADING_DIFF_LIMIT_PWM, HEADING_DIFF_LIMIT_PWM);
    float withResidual = scaled + sys->headingDiffResidual;
    int16_t quantized = control_clamp_signed_pwm((int32_t)float_to_i16(withResidual));
    sys->headingDiffResidual = clamp_f(withResidual - (float)quantized, -0.5f, 0.5f);
    return quantized;
}

static void control_reset_speed_loop(ControlSystem_t *sys) {
    sys->speedErr = 0.0f;
    sys->speedI = 0.0f;
    sys->speedPrevError = 0.0f;
    sys->speedOut = 0.0f;
    sys->speedTarget = 0.0f;
    sys->actualSpeed = 0.0f;
}

static void control_reset_angle_loop(ControlSystem_t *sys) {
    sys->targetAngle = 0.0f;
    sys->actualAngle = 0.0f;
    sys->anglePrevActual = 0.0f;
    sys->angleErr = 0.0f;
    sys->angleI = 0.0f;
    sys->anglePrevError = 0.0f;
    sys->angleOut = 0.0f;
}

static void control_reset_heading_loop(ControlSystem_t *sys) {
    sys->yawErr = 0.0f;
    sys->yawOut = 0.0f;
    sys->filteredYawRate = 0.0f;
    sys->headingDiffResidual = 0.0f;
}

static void control_reset_cascade_state(ControlSystem_t *sys) {
    control_reset_speed_loop(sys);
    control_reset_angle_loop(sys);
    control_reset_heading_loop(sys);
    sys->pwmCommand = 0;
}

static void control_reset_output(ControlSystem_t *sys) {
    sys->leftPWM = 0;
    sys->rightPWM = 0;
    sys->outLeftPWM = 0;
    sys->outRightPWM = 0;
    sys->pwmCommand = 0;
    sys->pwmCore = 0;
    sys->headingDiffPwm = 0;
}

static int16_t control_apply_deadzone_side(float pwmCore) {
    float compensated = pwmCore;
    if (abs_f(pwmCore) >= MOTOR_OUTPUT_DEADZONE_THRESHOLD) {
        if (pwmCore > 0.0f) compensated += (float)MOTOR_OUTPUT_DEADZONE_PWM;
        else if (pwmCore < 0.0f) compensated -= (float)MOTOR_OUTPUT_DEADZONE_PWM;
    }
    return control_clamp_signed_pwm((int32_t)float_to_i16(compensated));
}

static int16_t control_mixed_output_from_core(int16_t mixedCore) {
    int16_t absMixed = (int16_t)abs((int)mixedCore);
    if (absMixed <= 0) return 0;
    if (absMixed < (int16_t)MOTOR_OUTPUT_DEADZONE_THRESHOLD) return 0;
    return control_apply_deadzone_side((float)mixedCore);
}

static int16_t control_drive_output_from_core(int16_t pwmCore) {
    return control_clamp_signed_pwm((int32_t)pwmCore);
}

static int16_t control_heading_diff_limit_from_mix(int16_t pwmCore, int16_t baseDriveOut, int16_t requestedHeadingDiffPwm) {
    int16_t absCore = (int16_t)abs((int)pwmCore);
    int16_t absDriveOut = (int16_t)abs((int)baseDriveOut);
    int16_t limit = (int16_t)HEADING_DIFF_LIMIT_PWM;
    int16_t weakSideLimit = 0;
    int16_t coreBound = 0;
    int16_t weakSideMinActive = MOTOR_OUTPUT_WEAK_SIDE_MIN_ACTIVE_PWM;
    if (absCore <= 0) return 0;
    if (absDriveOut <= 0) return 0;
    if ((int16_t)abs((int)requestedHeadingDiffPwm) >= HEADING_STRONG_DIFF_THRESHOLD_PWM) {
        weakSideMinActive = MOTOR_OUTPUT_WEAK_SIDE_STRONG_ACTIVE_PWM;
    }
    weakSideLimit = absDriveOut - weakSideMinActive;
    if (weakSideLimit < 0) weakSideLimit = 0;
    if (weakSideLimit < limit) limit = weakSideLimit;
    if (absCore <= 1) coreBound = 1;
    else if (absCore <= 3) coreBound = 2;
    else if (absCore <= 5) coreBound = 3;
    else coreBound = absCore - 2;
    if (coreBound < 0) coreBound = 0;
    if (coreBound < limit) limit = coreBound;
    if (limit < 0) limit = 0;
    return limit;
}

static float control_heading_d_scale(ControlSystem_t *sys) {
    uint32_t runMs;
    if (!sys->isRunning) return 0.0f;
    if (sys->tickCount < sys->runStartTick) return 0.0f;
    runMs = sys->tickCount - sys->runStartTick;
    if (runMs >= HEADING_D_RAMP_MS) return 1.0f;
    return (float)runMs / (float)HEADING_D_RAMP_MS;
}

static int16_t control_apply_motor_output(ControlSystem_t *sys, int16_t pwmCore, int16_t headingDiffPwm) {
    int16_t appliedHeadingDiffPwm = headingDiffPwm;
    int16_t leftCore = 0;
    int16_t rightCore = 0;
    int16_t leftOut = 0;
    int16_t rightOut = 0;
    int16_t baseDriveOut = 0;
    int16_t headingLimit = 0;

    baseDriveOut = control_drive_output_from_core(pwmCore);
    headingLimit = control_heading_diff_limit_from_mix(pwmCore, baseDriveOut, appliedHeadingDiffPwm);
    if (appliedHeadingDiffPwm > headingLimit) appliedHeadingDiffPwm = headingLimit;
    else if (appliedHeadingDiffPwm < -headingLimit) appliedHeadingDiffPwm = -headingLimit;
    leftCore = control_clamp_signed_pwm((int32_t)pwmCore + (int32_t)appliedHeadingDiffPwm);
    rightCore = control_clamp_signed_pwm((int32_t)pwmCore - (int32_t)appliedHeadingDiffPwm);
    leftOut = control_drive_output_from_core(leftCore);
    rightOut = control_drive_output_from_core(rightCore);

    sys->pwmCore = pwmCore;
    sys->headingDiffPwm = appliedHeadingDiffPwm;
    sys->leftPWM = leftCore;
    sys->rightPWM = rightCore;
    sys->outLeftPWM = leftOut;
    sys->outRightPWM = rightOut;
    sys->pwmCommand = control_clamp_signed_pwm(((int32_t)leftOut + (int32_t)rightOut) / 2);
    Motor_SetDiffSpeed(leftOut, rightOut);
    return appliedHeadingDiffPwm;
}

static void control_apply_raw_output(ControlSystem_t *sys) {
    int16_t pwm = control_clamp_signed_pwm(g_rawPwm);
    sys->targetAngle = 0.0f;
    sys->actualAngle = 0.0f;
    sys->angleErr = 0.0f;
    sys->angleOut = 0.0f;
    sys->yawErr = 0.0f;
    sys->yawOut = 0.0f;
    sys->headingDiffResidual = 0.0f;
    (void)control_apply_motor_output(sys, pwm, 0);
}

static uint8_t control_vofa_text_enabled(void) {
    return (g_binMode == VOFA_MODE_FIREWATER) ? 1u : 0u;
}

static void control_send_text(const char *text) {
    if (!text) return;
    VOFA_SendString(text);
}

static void send_ok(const char *tag) {
    char b[24];
    snprintf(b, sizeof(b), "OK %s\r\n", tag);
    control_send_text(b);
}

static void send_err(void) {
    control_send_text("ERR\r\n");
}

static void send_trace(const char *tag) {
    char b[48];
    snprintf(b, sizeof(b), "TRACE %s\r\n", tag);
    control_send_text(b);
}

static void send_boot_flags(void) {
    char b[128];
    uint16_t fault = FaultTrace_GetAndClearCode();
    snprintf(b, sizeof(b),
             "BOOT pin=%u por=%u sftr=%u iwdg=%u wwdg=%u lpwr=%u fault=%u\r\n",
             (unsigned)(RCC_GetFlagStatus(RCC_FLAG_PINRST) != RESET),
             (unsigned)(RCC_GetFlagStatus(RCC_FLAG_PORRST) != RESET),
             (unsigned)(RCC_GetFlagStatus(RCC_FLAG_SFTRST) != RESET),
             (unsigned)(RCC_GetFlagStatus(RCC_FLAG_IWDGRST) != RESET),
             (unsigned)(RCC_GetFlagStatus(RCC_FLAG_WWDGRST) != RESET),
             (unsigned)(RCC_GetFlagStatus(RCC_FLAG_LPWRRST) != RESET),
             (unsigned)fault);
    control_send_text(b);
    RCC_ClearFlag();
}

static uint8_t starts_with(const char *s, const char *prefix) {
    if (!s || !prefix) return 0;
    while (*prefix) {
        if (*s++ != *prefix++) return 0;
    }
    return 1;
}

static const char *control_get_vofa_mode_name(uint8_t mode) {
    if (mode == VOFA_MODE_JUSTFLOAT5) return "justfloat5";
    if (mode == VOFA_MODE_JUSTFLOAT3) return "justfloat3";
    return "firewater";
}

static void exp_reset(ControlSystem_t *sys) {
    sys->expActive = 0;
    sys->expStreamEnabled = 0;
    sys->expId = 0;
    sys->expStartTick = 0;
    sys->expDurationMs = 0;
    sys->expDumpReady = 0;
    sys->expSampleCount = 0;
    sys->expSamplePeriodMs = EXP_SAMPLE_PERIOD_MS;
}

static void control_send_cfg(ControlSystem_t *sys) {
    static char b[640];
    if (!control_vofa_text_enabled()) return;
    if (sys->expActive) {
        snprintf(b, sizeof(b),
                 "CFG exp_id=%u ms=%lu ts=%.6f skp=%.6f ski=%.6f skd=%.6f akp=%.6f aki=%.6f akd=%.6f\r\n",
                 (unsigned)sys->expId,
                 (unsigned long)sys->expDurationMs,
                 (double)sys->targetSpeed,
                 (double)sys->speedKp,
                 (double)sys->speedKi,
                 (double)sys->speedKd,
                 (double)sys->angleKp,
                 (double)sys->angleKi,
                 (double)sys->angleKd);
    } else {
        snprintf(b, sizeof(b),
                 "CFG ts=%.6f skp=%.6f ski=%.6f skd=%.6f akp=%.6f aki=%.6f akd=%.6f\r\n",
                 (double)sys->targetSpeed,
                 (double)sys->speedKp,
                 (double)sys->speedKi,
                 (double)sys->speedKd,
                 (double)sys->angleKp,
                 (double)sys->angleKi,
                 (double)sys->angleKd);
    }
    control_send_text(b);
}

static void control_send_hb_compact(ControlSystem_t *sys, uint8_t include_exp_id, uint32_t t_ms) {
    static char out[768];
    static char samples[96];
    int16_t ed = (int16_t)(sys->encoder.leftSpeed - sys->encoder.rightSpeed);
    if (!control_vofa_text_enabled()) return;
    if (include_exp_id) {
        snprintf(out, sizeof(out),
                 "HB tick=%lu exp_id=%u t_ms=%lu run=%u ts=%.3f as=%.3f ta=%.3f aa=%.3f se=%.3f so=%.3f ae=%.3f ao=%.3f pwm=%d pc=%d hd=%d y=%.3f yr=%.3f L=%d R=%d OL=%d OR=%d el=%d er=%d ed=%d dl=%d dr=%d cl=%u cr=%u ok=%lu fail=%lu vofa=%s rx=%lu txdrop=%lu\r\n",
                 (unsigned long)sys->tickCount,
                 (unsigned)sys->expId,
                 (unsigned long)t_ms,
                 (unsigned)sys->isRunning,
                 (double)sys->speedTarget,
                 (double)sys->actualSpeed,
                 (double)sys->targetAngle,
                 (double)sys->actualAngle,
                 (double)sys->speedErr,
                 (double)sys->speedOut,
                 (double)sys->angleErr,
                 (double)sys->angleOut,
                 (int)sys->pwmCommand,
                 (int)sys->pwmCore,
                 (int)sys->headingDiffPwm,
                 (double)sys->icm.yaw,
                 (double)sys->icm.yawRate,
                 (int)sys->leftPWM,
                 (int)sys->rightPWM,
                 (int)sys->outLeftPWM,
                 (int)sys->outRightPWM,
                 (int)sys->encoder.leftSpeed,
                 (int)sys->encoder.rightSpeed,
                 (int)ed,
                 (int)sys->encoder.rawLeftDelta,
                 (int)sys->encoder.rawRightDelta,
                 (unsigned)sys->encoder.leftDeltaClamped,
                 (unsigned)sys->encoder.rightDeltaClamped,
                 (unsigned long)g_okTele,
                 (unsigned long)g_failTele,
                 control_get_vofa_mode_name(g_binMode),
                 (unsigned long)VOFA_GetRxByteCount(),
                 (unsigned long)VOFA_GetTxDropByteCount());
    } else {
        snprintf(out, sizeof(out),
                 "HB tick=%lu run=%u ts=%.3f as=%.3f ta=%.3f aa=%.3f se=%.3f so=%.3f ae=%.3f ao=%.3f pwm=%d pc=%d hd=%d y=%.3f yr=%.3f L=%d R=%d OL=%d OR=%d el=%d er=%d ed=%d dl=%d dr=%d cl=%u cr=%u ok=%lu fail=%lu vofa=%s rx=%lu txdrop=%lu\r\n",
                 (unsigned long)sys->tickCount,
                 (unsigned)sys->isRunning,
                 (double)sys->speedTarget,
                 (double)sys->actualSpeed,
                 (double)sys->targetAngle,
                 (double)sys->actualAngle,
                 (double)sys->speedErr,
                 (double)sys->speedOut,
                 (double)sys->angleErr,
                 (double)sys->angleOut,
                 (int)sys->pwmCommand,
                 (int)sys->pwmCore,
                 (int)sys->headingDiffPwm,
                 (double)sys->icm.yaw,
                 (double)sys->icm.yawRate,
                 (int)sys->leftPWM,
                 (int)sys->rightPWM,
                 (int)sys->outLeftPWM,
                 (int)sys->outRightPWM,
                 (int)sys->encoder.leftSpeed,
                 (int)sys->encoder.rightSpeed,
                 (int)ed,
                 (int)sys->encoder.rawLeftDelta,
                 (int)sys->encoder.rawRightDelta,
                 (unsigned)sys->encoder.leftDeltaClamped,
                 (unsigned)sys->encoder.rightDeltaClamped,
                 (unsigned long)g_okTele,
                 (unsigned long)g_failTele,
                 control_get_vofa_mode_name(g_binMode),
                 (unsigned long)VOFA_GetRxByteCount(),
                 (unsigned long)VOFA_GetTxDropByteCount());
    }
    control_send_text(out);

    if (!include_exp_id && sys->isRunning) {
        snprintf(samples, sizeof(samples),
                 "samples:%.6f,%.6f,%d\r\n",
                 (double)sys->actualAngle,
                 (double)sys->actualSpeed,
                 (int)sys->pwmCommand);
        control_send_text(samples);
    }

    g_okTele++;
}

static void control_capture_exp_sample(ControlSystem_t *sys, uint32_t t_ms) {
    if (sys->expSampleCount < EXP_MAX_SAMPLES) {
        if (sys->isRunning) {
            uint16_t idx = sys->expSampleCount++;
            ExpSample_t *s = &sys->expSamples[idx];
            int16_t ed = (int16_t)(sys->encoder.leftSpeed - sys->encoder.rightSpeed);
            s->t_ms = (uint16_t)t_ms;
            s->run = sys->isRunning;
            s->targetSpeed10 = (int16_t)(sys->speedTarget * 10.0f);
            s->actualSpeed10 = (int16_t)(sys->actualSpeed * 10.0f);
            s->targetAngle10 = (int16_t)(sys->targetAngle * 10.0f);
            s->actualAngle10 = (int16_t)(sys->actualAngle * 10.0f);
            s->speedErr10 = (int16_t)(sys->speedErr * 10.0f);
            s->speedOut10 = (int16_t)(sys->speedOut * 10.0f);
            s->angleErr10 = (int16_t)(sys->angleErr * 10.0f);
            s->angleOut10 = (int16_t)(sys->angleOut * 10.0f);
            s->L = sys->leftPWM;
            s->R = sys->rightPWM;
            s->el = sys->encoder.leftSpeed;
            s->er = sys->encoder.rightSpeed;
            s->ed = ed;
            s->pwm = sys->pwmCommand;
            s->yawRate100 = (int16_t)(sys->sensedYawRate * 100.0f);
            s->ax = sys->icm.accelX;
            s->ay = sys->icm.accelY;
            s->az = sys->icm.accelZ;
            s->gx = sys->icm.gyroX;
            s->gy = sys->icm.gyroY;
            s->gz = sys->icm.gyroZ;
        }
    }
}

static void exp_stream_line(ControlSystem_t *sys, uint32_t t_ms) {
    if (!control_vofa_text_enabled()) return;
    control_send_hb_compact(sys, 1u, t_ms);
}

static void exp_start(ControlSystem_t *sys, uint16_t id, uint32_t ms) {
    sys->expActive = 1;
    sys->expId = id;
    sys->expStartTick = sys->tickCount;
    sys->expDurationMs = ms;
    sys->expDumpReady = 0;
    sys->expSampleCount = 0;
    sys->expSampleLastTick = 0u;
    sys->expStreamLastTick = 0u;
    {
        uint32_t p;
        if (ms == 0) {
            p = (uint32_t)EXP_SAMPLE_PERIOD_MS;
        } else {
            p = (ms + (uint32_t)EXP_MAX_SAMPLES - 1u) / (uint32_t)EXP_MAX_SAMPLES;
            if (p < (uint32_t)EXP_SAMPLE_PERIOD_MS) p = (uint32_t)EXP_SAMPLE_PERIOD_MS;
        }
        sys->expSamplePeriodMs = (uint16_t)p;
    }
    {
        char b[48];
        snprintf(b, sizeof(b), "EXP_START id=%u ms=%lu\r\n", (unsigned)id, (unsigned long)ms);
        control_send_text(b);
    }
}

static void exp_stop(ControlSystem_t *sys, uint16_t id) {
    if (sys->expActive && (sys->expId == id)) {
        Control_Stop(sys);
        sys->expActive = 0;
        sys->expDumpReady = 1;
        {
            char b[36];
            snprintf(b, sizeof(b), "EXP_END id=%u\r\n", (unsigned)id);
            control_send_text(b);
        }
        send_ok("EXP_STOP");
        return;
    }
    send_err();
}

static void exp_dump(ControlSystem_t *sys, uint16_t id) {
    if (!control_vofa_text_enabled()) return;
    if (sys->expId != id) {
        send_err();
        return;
    }
    if (sys->expActive || !sys->expDumpReady) {
        send_err();
        return;
    }
    {
        char b[256];
        uint16_t i;
        snprintf(b, sizeof(b), "EXP_DUMP_BEGIN id=%u\r\n", (unsigned)id);
        control_send_text(b);
        control_send_text("FIELDS:exp_id,t_ms,run,targetSpeed10,actualSpeed10,targetAngle10,actualAngle10,speedErr10,speedOut10,angleErr10,angleOut10,L,R,el,er,ed,pwm,yawRate100,ax,ay,az,gx,gy,gz\r\n");
        for (i = 0; i < sys->expSampleCount; i++) {
            ExpSample_t *s = &sys->expSamples[i];
            snprintf(b, sizeof(b),
                     "D %u,%u,%u,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d\r\n",
                     (unsigned)sys->expId,
                     (unsigned)s->t_ms,
                     (unsigned)s->run,
                     (int)s->targetSpeed10,
                     (int)s->actualSpeed10,
                     (int)s->targetAngle10,
                     (int)s->actualAngle10,
                     (int)s->speedErr10,
                     (int)s->speedOut10,
                     (int)s->angleErr10,
                     (int)s->angleOut10,
                     (int)s->L,
                     (int)s->R,
                     (int)s->el,
                     (int)s->er,
                     (int)s->ed,
                     (int)s->pwm,
                     (int)s->yawRate100,
                     (int)s->ax,
                     (int)s->ay,
                     (int)s->az,
                     (int)s->gx,
                     (int)s->gy,
                     (int)s->gz);
            control_send_text(b);
        }
        snprintf(b, sizeof(b), "EXP_DUMP_END id=%u\r\n", (unsigned)id);
        control_send_text(b);
    }
    sys->expDumpReady = 0;
    send_ok("EXP_DUMP");
}

static void control_send_stat(ControlSystem_t *sys) {
    static char out[1280];
    int16_t ed = (int16_t)(sys->encoder.leftSpeed - sys->encoder.rightSpeed);
    uint32_t t_ms = 0u;
    if (!control_vofa_text_enabled()) return;
    if (sys->expActive) {
        t_ms = (sys->tickCount - sys->expStartTick);
        snprintf(out, sizeof(out),
                 "STAT tick=%lu exp_id=%u t_ms=%lu run=%u ts=%.6f as=%.6f ta=%.6f aa=%.6f se=%.6f so=%.6f ae=%.6f ao=%.6f pwm=%d pc=%d hd=%d y=%.6f p=%.6f r=%.6f yr=%.6f ax=%d ay=%d az=%d gx=%d gy=%d gz=%d who=0x%02X addr=0x%02X icm_ok=%lu icm_fail=%lu L=%d R=%d OL=%d OR=%d el=%d er=%d ed=%d dl=%d dr=%d cl=%u cr=%u skp=%.6f ski=%.6f skd=%.6f akp=%.6f aki=%.6f akd=%.6f ok=%lu fail=%lu rx=%lu txdrop=%lu\r\n",
                 (unsigned long)sys->tickCount,
                 (unsigned)sys->expId,
                 (unsigned long)t_ms,
                 (unsigned)sys->isRunning,
                 (double)sys->speedTarget,
                 (double)sys->actualSpeed,
                 (double)sys->targetAngle,
                 (double)sys->actualAngle,
                 (double)sys->speedErr,
                 (double)sys->speedOut,
                 (double)sys->angleErr,
                 (double)sys->angleOut,
                 (int)sys->pwmCommand,
                 (int)sys->pwmCore,
                 (int)sys->headingDiffPwm,
                 (double)sys->icm.yaw,
                 (double)sys->icm.pitch,
                 (double)sys->icm.roll,
                 (double)sys->icm.yawRate,
                 (int)(sys->icm.accelX - g_imuAx0),
                 (int)(sys->icm.accelY - g_imuAy0),
                 (int)(sys->icm.accelZ - g_imuAz0),
                 (int)(sys->icm.gyroX - g_imuGx0),
                 (int)(sys->icm.gyroY - g_imuGy0),
                 (int)(sys->icm.gyroZ - g_imuGz0),
                 (unsigned)ICM42688_GetWhoAmI(),
                 (unsigned)ICM42688_GetI2CAddr(),
                 (unsigned long)g_icmOk,
                 (unsigned long)g_icmFail,
                 (int)sys->leftPWM,
                 (int)sys->rightPWM,
                 (int)sys->outLeftPWM,
                 (int)sys->outRightPWM,
                 (int)sys->encoder.leftSpeed,
                 (int)sys->encoder.rightSpeed,
                 (int)ed,
                 (int)sys->encoder.rawLeftDelta,
                 (int)sys->encoder.rawRightDelta,
                 (unsigned)sys->encoder.leftDeltaClamped,
                 (unsigned)sys->encoder.rightDeltaClamped,
                 (double)sys->speedKp,
                 (double)sys->speedKi,
                 (double)sys->speedKd,
                 (double)sys->angleKp,
                 (double)sys->angleKi,
                 (double)sys->angleKd,
                 (unsigned long)g_okTele,
                 (unsigned long)g_failTele,
                 (unsigned long)VOFA_GetRxByteCount(),
                 (unsigned long)VOFA_GetTxDropByteCount());
    } else {
        snprintf(out, sizeof(out),
                 "STAT tick=%lu run=%u ts=%.6f as=%.6f ta=%.6f aa=%.6f se=%.6f so=%.6f ae=%.6f ao=%.6f pwm=%d pc=%d hd=%d y=%.6f p=%.6f r=%.6f yr=%.6f ax=%d ay=%d az=%d gx=%d gy=%d gz=%d who=0x%02X addr=0x%02X icm_ok=%lu icm_fail=%lu L=%d R=%d OL=%d OR=%d el=%d er=%d ed=%d dl=%d dr=%d cl=%u cr=%u skp=%.6f ski=%.6f skd=%.6f akp=%.6f aki=%.6f akd=%.6f ok=%lu fail=%lu rx=%lu txdrop=%lu\r\n",
                 (unsigned long)sys->tickCount,
                 (unsigned)sys->isRunning,
                 (double)sys->speedTarget,
                 (double)sys->actualSpeed,
                 (double)sys->targetAngle,
                 (double)sys->actualAngle,
                 (double)sys->speedErr,
                 (double)sys->speedOut,
                 (double)sys->angleErr,
                 (double)sys->angleOut,
                 (int)sys->pwmCommand,
                 (int)sys->pwmCore,
                 (int)sys->headingDiffPwm,
                 (double)sys->icm.yaw,
                 (double)sys->icm.pitch,
                 (double)sys->icm.roll,
                 (double)sys->icm.yawRate,
                 (int)(sys->icm.accelX - g_imuAx0),
                 (int)(sys->icm.accelY - g_imuAy0),
                 (int)(sys->icm.accelZ - g_imuAz0),
                 (int)(sys->icm.gyroX - g_imuGx0),
                 (int)(sys->icm.gyroY - g_imuGy0),
                 (int)(sys->icm.gyroZ - g_imuGz0),
                 (unsigned)ICM42688_GetWhoAmI(),
                 (unsigned)ICM42688_GetI2CAddr(),
                 (unsigned long)g_icmOk,
                 (unsigned long)g_icmFail,
                 (int)sys->leftPWM,
                 (int)sys->rightPWM,
                 (int)sys->outLeftPWM,
                 (int)sys->outRightPWM,
                 (int)sys->encoder.leftSpeed,
                 (int)sys->encoder.rightSpeed,
                 (int)ed,
                 (int)sys->encoder.rawLeftDelta,
                 (int)sys->encoder.rawRightDelta,
                 (unsigned)sys->encoder.leftDeltaClamped,
                 (unsigned)sys->encoder.rightDeltaClamped,
                 (double)sys->speedKp,
                 (double)sys->speedKi,
                 (double)sys->speedKd,
                 (double)sys->angleKp,
                 (double)sys->angleKi,
                 (double)sys->angleKd,
                 (unsigned long)g_okTele,
                 (unsigned long)g_failTele,
                 (unsigned long)VOFA_GetRxByteCount(),
                 (unsigned long)VOFA_GetTxDropByteCount());
    }
    control_send_text(out);
}

static void control_parse_cmd(ControlSystem_t *sys, const char *cmd) {
    if (!cmd) return;

    if (strcmp(cmd, "#RUN") == 0 || strcmp(cmd, "#RUN!") == 0) {
        if (sys->isRunning) {
            send_ok("RUN");
            return;
        }
        send_trace("RUN_CMD");
        if (Control_Start(sys)) {
            send_trace("RUN_OK");
            send_ok("RUN");
            control_send_cfg(sys);
        } else {
            send_trace("RUN_FAIL");
            send_err();
        }
        return;
    }
    if (strcmp(cmd, "#STOP") == 0 || strcmp(cmd, "#STOP!") == 0) {
        Control_Stop(sys);
        send_ok("STOP");
        return;
    }
    if (strcmp(cmd, "#STAT") == 0 || strcmp(cmd, "#STAT!") == 0) {
        control_send_stat(sys);
        return;
    }

    if (strcmp(cmd, "#CAL") == 0 || strcmp(cmd, "#CAL!") == 0) {
        if (sys->isRunning) {
            send_ok("CAL");
            return;
        }
        Control_LockHeading(sys);
        send_ok("CAL");
        return;
    }

    if (strcmp(cmd, "#IMU_INIT") == 0 || strcmp(cmd, "#IMU_INIT!") == 0) {
        memset(&sys->icm, 0, sizeof(sys->icm));
        ICM42688_Init();
        if (ICM42688_GetWhoAmI() == 0x85u) {
            (void)ICM42688_ReadAll(&sys->icm);
            control_imu_zero_update(sys);
            ICM42688_ResetAttitude(&sys->icm);
            sys->angleZero = sys->icm.pitch;
            control_reset_cascade_state(sys);
            send_ok("IMU_INIT");
        } else {
            send_err();
        }
        return;
    }

    if (strcmp(cmd, "#MODE=TRACK") == 0 || strcmp(cmd, "#MODE=TRACK!") == 0) {
        if (sys->isRunning) {
            send_err();
            return;
        }
        g_mainSelectedMode = 1u;
        send_ok("MODE");
        return;
    }
    if (strcmp(cmd, "#MODE=STRAIGHT") == 0 || strcmp(cmd, "#MODE=STRAIGHT!") == 0) {
        g_mainSelectedMode = 0u;
        send_ok("MODE");
        return;
    }

    if (strncmp(cmd, "#BIN=", 5) == 0) {
        int16_t v = parse_int_after_eq(cmd);
        uint8_t newMode = VOFA_MODE_JUSTFLOAT3;
        if (v <= 0) newMode = VOFA_MODE_FIREWATER;
        else if (v == 5) newMode = VOFA_MODE_JUSTFLOAT5;
        if (newMode == VOFA_MODE_FIREWATER) {
            g_binMode = newMode;
            send_ok("BIN");
        } else {
            if (control_vofa_text_enabled()) send_ok("BIN");
            g_binMode = newMode;
        }
        return;
    }

    if (strncmp(cmd, "#VOFA=", 6) == 0) {
        int16_t v = parse_int_after_eq(cmd);
        uint8_t newMode = VOFA_MODE_JUSTFLOAT3;
        if (v <= 0) newMode = VOFA_MODE_FIREWATER;
        else if (v == 5) newMode = VOFA_MODE_JUSTFLOAT5;
        if (newMode == VOFA_MODE_FIREWATER) {
            g_binMode = newMode;
            send_ok("VOFA");
        } else {
            if (control_vofa_text_enabled()) send_ok("VOFA");
            g_binMode = newMode;
        }
        return;
    }

    if (strncmp(cmd, "#ENC_L_SIGN=", 12) == 0) {
        int16_t v = parse_int_after_eq(cmd);
        Encoder_SetLeftSign((v >= 0) ? 1 : -1);
        send_ok("ENC_L_SIGN");
        return;
    }
    if (strncmp(cmd, "#ENC_R_SIGN=", 12) == 0) {
        int16_t v = parse_int_after_eq(cmd);
        Encoder_SetRightSign((v >= 0) ? 1 : -1);
        send_ok("ENC_R_SIGN");
        return;
    }

    if (starts_with(cmd, "#EXP=START")) {
        unsigned id = 0;
        unsigned ms = 0;
        if (sscanf(cmd, "#EXP=START,%u,%u", &id, &ms) == 2) {
            if (sys->expActive) {
                if (sys->expId == (uint16_t)id) {
                    send_ok("EXP_START");
                } else {
                    send_err();
                }
                return;
            }
            exp_start(sys, (uint16_t)id, (uint32_t)ms);
            send_ok("EXP_START");
        } else {
            send_err();
        }
        return;
    }
    if (starts_with(cmd, "#EXP=STREAM")) {
        unsigned en = 0;
        if (sscanf(cmd, "#EXP=STREAM,%u", &en) == 1) {
            sys->expStreamEnabled = (en != 0u) ? 1u : 0u;
            send_ok("EXP_STREAM");
        } else {
            send_err();
        }
        return;
    }
    if (starts_with(cmd, "#EXP=RUN")) {
        unsigned id = 0;
        unsigned ms = 0;
        if (sscanf(cmd, "#EXP=RUN,%u,%u", &id, &ms) == 2) {
            if (sys->isRunning || sys->expActive) {
                if (sys->isRunning && sys->expActive && sys->expId == (uint16_t)id) {
                    send_ok("EXP_START");
                    send_ok("RUN");
                } else {
                    send_err();
                }
                return;
            }
            exp_start(sys, (uint16_t)id, (uint32_t)ms);
            send_ok("EXP_START");

            Control_LockHeading(sys);
            send_ok("CAL");

            if (Control_Start(sys)) {
                send_ok("RUN");
                control_send_cfg(sys);
            } else {
                send_err();
            }
        } else {
            send_err();
        }
        return;
    }
    if (starts_with(cmd, "#EXP=STOP")) {
        unsigned id = 0;
        if (sscanf(cmd, "#EXP=STOP,%u", &id) == 1) {
            exp_stop(sys, (uint16_t)id);
        } else {
            send_err();
        }
        return;
    }
    if (starts_with(cmd, "#EXP=DUMP")) {
        unsigned id = 0;
        if (sscanf(cmd, "#EXP=DUMP,%u", &id) == 1) {
            exp_dump(sys, (uint16_t)id);
        } else {
            send_err();
        }
        return;
    }
    if (strncmp(cmd, "#SDIAG=", 7) == 0) {
        send_err();
        return;
    }
    if (strcmp(cmd, "#SDUMP") == 0 || strcmp(cmd, "#SDUMP!") == 0) {
        send_err();
        return;
    }

    if (strncmp(cmd, "#TS=", 4) == 0) {
        sys->targetSpeed = parse_float_after_eq(cmd);
        send_ok("TS");
        return;
    }
    if (strncmp(cmd, "#SKP=", 5) == 0) {
        sys->speedKp = parse_float_after_eq(cmd);
        control_reset_cascade_state(sys);
        send_ok("SKP");
        return;
    }
    if (strncmp(cmd, "#SKI=", 5) == 0) {
        sys->speedKi = parse_float_after_eq(cmd);
        control_reset_cascade_state(sys);
        send_ok("SKI");
        return;
    }
    if (strncmp(cmd, "#SKD=", 5) == 0) {
        sys->speedKd = parse_float_after_eq(cmd);
        control_reset_cascade_state(sys);
        send_ok("SKD");
        return;
    }
    if (strncmp(cmd, "#AKP=", 5) == 0 || strncmp(cmd, "#KP=", 4) == 0) {
        sys->angleKp = parse_float_after_eq(cmd);
        control_reset_cascade_state(sys);
        send_ok("AKP");
        return;
    }
    if (strncmp(cmd, "#AKI=", 5) == 0 || strncmp(cmd, "#KI=", 4) == 0) {
        sys->angleKi = parse_float_after_eq(cmd);
        control_reset_cascade_state(sys);
        send_ok("AKI");
        return;
    }
    if (strncmp(cmd, "#AKD=", 5) == 0 || strncmp(cmd, "#KD=", 4) == 0) {
        sys->angleKd = parse_float_after_eq(cmd);
        control_reset_cascade_state(sys);
        send_ok("AKD");
        return;
    }
    if (strncmp(cmd, "#RAW=", 5) == 0) {
        send_err();
        return;
    }

    send_err();
}

static void control_speed_loop(ControlSystem_t *sys) {
    float derivative = 0.0f;
    Encoder_UpdateSpeed(&sys->encoder, CONTROL_LOOP_PERIOD_MS);

    if (!sys->isRunning) {
        control_reset_output(sys);
        Motor_Stop();
        Motor_Disable();
        return;
    }

    sys->speedTarget = control_ramp_speed_target(sys, sys->targetSpeed);
    sys->actualSpeed = ((float)sys->encoder.leftSpeed + (float)sys->encoder.rightSpeed) * 0.5f;
    sys->speedErr = sys->speedTarget - sys->actualSpeed;
    sys->speedI += sys->speedErr * CONTROL_LOOP_DT_S;
    sys->speedI = clamp_f(sys->speedI, -SPEED_INTEGRAL_LIMIT, SPEED_INTEGRAL_LIMIT);
    derivative = (sys->speedErr - sys->speedPrevError) / CONTROL_LOOP_DT_S;
    sys->speedOut = sys->speedKp * sys->speedErr + sys->speedKi * sys->speedI + sys->speedKd * derivative;
    sys->speedOut = clamp_f(sys->speedOut, -SPEED_OUTPUT_LIMIT_PWM, SPEED_OUTPUT_LIMIT_PWM);
    sys->speedPrevError = sys->speedErr;
}

static void control_poll_imu(ControlSystem_t *sys) {
    float dt = IMU_UPDATE_PERIOD_S;
    uint32_t pollTick;
    uint32_t prevYawSampleTick;
    ICM42688_Data_t icmNext;

    memcpy(&icmNext, &sys->icm, sizeof(icmNext));
    prevYawSampleTick = sys->imuYawSampleTick;
    if (ICM42688_ReadAll(&icmNext)) {
        g_icmOk++;
        g_icmReadOkCount = g_icmOk;
        pollTick = sys->tickCount;
        if (icmNext.yawSampleUpdated) {
            if (prevYawSampleTick != 0u && pollTick >= prevYawSampleTick) {
                dt = (float)(pollTick - prevYawSampleTick) * 0.001f;
                if (dt <= 0.0f) dt = IMU_UPDATE_PERIOD_S;
            }
            ICM42688_UpdateYaw(&icmNext, dt);
        }

        {
            uint32_t primask = control_enter_critical();
            memcpy(&sys->icm, &icmNext, sizeof(sys->icm));
            sys->sensedPitch = icmNext.pitch;
            sys->sensedYaw = icmNext.yaw;
            sys->sensedYawRate = icmNext.yawRate;
            sys->imuDataTick = pollTick;
            if (icmNext.yawSampleUpdated) {
                if (sys->yawAlignPending) {
                    sys->targetYaw = icmNext.yaw;
                    sys->yawAlignPending = 0u;
                }
                sys->imuYawSampleTick = pollTick;
            }
            sys->imuDataValid = 1u;
            control_exit_critical(primask);
        }
        sys->imuLastUpdateTick = pollTick;
    } else {
        g_icmFail++;
        g_icmReadFailCount = g_icmFail;
    }
    sys->imuPollLastTick = sys->tickCount;
}

static void control_heading_loop(ControlSystem_t *sys) {
    float dt = IMU_UPDATE_PERIOD_S;
    float headingErr = 0.0f;
    float yawRate = 0.0f;
    float yawRateRaw = 0.0f;
    float headingOut = 0.0f;
    float dScale = 0.0f;
    int16_t pwmCore = 0;
    int16_t headingDiffPwm = 0;

    if (sys->angleLoopLastTick != 0u && sys->tickCount >= sys->angleLoopLastTick) {
        dt = (float)(sys->tickCount - sys->angleLoopLastTick) * 0.001f;
        if (dt <= 0.0f) dt = IMU_UPDATE_PERIOD_S;
    }
    sys->angleLoopLastTick = sys->tickCount;

    if (!sys->isRunning) {
        control_reset_cascade_state(sys);
        control_reset_output(sys);
        return;
    }

    sys->targetAngle = 0.0f;
    pwmCore = control_speed_command_to_pwm(sys->speedOut);

    if (!sys->imuDataValid) {
        sys->actualAngle = 0.0f;
        sys->angleI = 0.0f;
        sys->angleOut = 0.0f;
        sys->yawOut = 0.0f;
        sys->filteredYawRate = 0.0f;
        sys->headingDiffResidual = 0.0f;
        (void)control_apply_motor_output(sys, pwmCore, 0);
        return;
    }

    sys->actualAngle = control_wrap_deg180(sys->sensedYaw - sys->targetYaw);

    headingErr = control_wrap_deg180(sys->targetYaw - sys->sensedYaw);
    sys->angleErr = headingErr;
    sys->anglePrevError = sys->angleErr;
    sys->anglePrevActual = sys->actualAngle;
    sys->yawErr = headingErr;

    yawRateRaw = sys->sensedYawRate;
    sys->filteredYawRate = ((1.0f - HEADING_D_FILTER_ALPHA) * sys->filteredYawRate) +
                           (HEADING_D_FILTER_ALPHA * yawRateRaw);
    yawRate = clamp_f(sys->filteredYawRate, -ANGLE_DERIVATIVE_LIMIT_DPS, ANGLE_DERIVATIVE_LIMIT_DPS);
    sys->filteredYawRate = yawRate;
    dScale = control_heading_d_scale(sys);
    sys->angleI += headingErr * dt;
    sys->angleI = clamp_f(sys->angleI, -ANGLE_INTEGRAL_LIMIT, ANGLE_INTEGRAL_LIMIT);
    headingOut = sys->angleKp * headingErr +
                 sys->angleKi * sys->angleI +
                 -(sys->angleKd * yawRate * dScale);
    sys->angleOut = clamp_f(headingOut, -ANGLE_OUTPUT_LIMIT_PWM, ANGLE_OUTPUT_LIMIT_PWM);
    headingDiffPwm = control_quantize_heading_diff(sys, sys->angleOut);
    headingDiffPwm = control_apply_motor_output(sys, pwmCore, headingDiffPwm);
    sys->yawOut = (float)headingDiffPwm;
}

static void control_send_hb(ControlSystem_t *sys) {
    if (!control_vofa_text_enabled()) return;
    control_send_hb_compact(sys, 0u, 0u);
}

static void control_send_bin3(ControlSystem_t *sys) {
    VOFA_SendJustFloat3(
            sys->actualAngle,
            sys->actualSpeed,
            (float)sys->pwmCommand);
}

static void control_send_bin5(ControlSystem_t *sys) {
    VOFA_SendJustFloat5(
            sys->targetAngle,
            sys->actualAngle,
            sys->actualSpeed,
            sys->speedOut,
            sys->angleOut);
}

void Control_LoadStableDefaults(ControlSystem_t *sys) {
    sys->speedKp = STABLE_DEFAULT_SPEED_KP;
    sys->speedKi = STABLE_DEFAULT_SPEED_KI;
    sys->speedKd = STABLE_DEFAULT_SPEED_KD;
    sys->angleKp = STABLE_DEFAULT_ANGLE_KP;
    sys->angleKi = STABLE_DEFAULT_ANGLE_KI;
    sys->angleKd = STABLE_DEFAULT_ANGLE_KD;
    sys->targetSpeed = STABLE_DEFAULT_TARGET_SPEED;
    sys->angleZero = 0.0f;
    sys->targetYaw = 0.0f;
    sys->angleLoopLastTick = 0u;
    sys->imuYawSampleTick = 0u;
    sys->imuPollLastTick = 0u;
    sys->hbLastTick = 0u;
    sys->binLastTick = 0u;
    sys->expSampleLastTick = 0u;
    sys->expStreamLastTick = 0u;
    sys->imuDataValid = 0u;
    sys->imuDataTick = 0u;
    sys->sensedPitch = 0.0f;
    sys->sensedYaw = 0.0f;
    sys->sensedYawRate = 0.0f;
    sys->headingDiffResidual = 0.0f;
    sys->yawAlignPending = 0u;
    control_reset_cascade_state(sys);
    control_reset_output(sys);
}

void Control_Init(ControlSystem_t *sys, uint8_t skipICM) {
    (void)skipICM;
    memset(sys, 0, sizeof(*sys));

    Motor_Init();
    Motor_Stop();
    Motor_Disable();
    Encoder_Timer_Init();
    VOFA_Init();
    send_boot_flags();
    ICM42688_SetBiasTrackEnabled(1u);

    Control_LoadStableDefaults(sys);

    if (!skipICM) {
        ICM42688_Init();
        if (ICM42688_GetWhoAmI() == 0x85u) {
            ICM42688_Calibrate(&sys->icm, 100);
            (void)ICM42688_ReadAll(&sys->icm);
            control_imu_zero_update(sys);
            ICM42688_ResetAttitude(&sys->icm);
            sys->angleZero = sys->icm.pitch;
            sys->sensedPitch = sys->icm.pitch;
            sys->sensedYaw = sys->icm.yaw;
            sys->sensedYawRate = sys->icm.yawRate;
            sys->anglePrevActual = sys->icm.pitch - sys->angleZero;
            sys->imuYawSampleTick = sys->tickCount;
            sys->imuDataValid = 1u;
        } else {
            memset(&sys->icm, 0, sizeof(sys->icm));
        }
    }

    g_okTele = 0;
    g_failTele = 0;
    g_icmOk = 0;
    g_icmFail = 0;
    g_icmReadOkCount = 0;
    g_icmReadFailCount = 0;

    exp_reset(sys);
}

static uint8_t control_wait_imu_ready(ControlSystem_t *sys, uint16_t timeoutMs) {
    uint16_t t;

    for (t = 0u; t < timeoutMs; t++) {
        if (ICM42688_ReadAll(&sys->icm) && sys->icm.ahrsInited) {
            g_icmOk++;
            g_icmReadOkCount = g_icmOk;
            ICM42688_UpdateYaw(&sys->icm, IMU_UPDATE_PERIOD_S);
            return 1u;
        }
        Delay_ms(1);
    }

    return 0u;
}

static uint8_t control_sync_start_heading(ControlSystem_t *sys, uint16_t timeoutMs) {
    uint16_t t;
    ICM42688_Data_t icmNext;

    memcpy(&icmNext, &sys->icm, sizeof(icmNext));
    for (t = 0u; t < timeoutMs; t++) {
        if (ICM42688_ReadAll(&icmNext) && icmNext.ahrsInited) {
            g_icmOk++;
            g_icmReadOkCount = g_icmOk;
            if (icmNext.yawSampleUpdated) {
                uint32_t primask;
                ICM42688_UpdateYaw(&icmNext, IMU_UPDATE_PERIOD_S);
                primask = control_enter_critical();
                memcpy(&sys->icm, &icmNext, sizeof(sys->icm));
                sys->sensedPitch = icmNext.pitch;
                sys->sensedYaw = icmNext.yaw;
                sys->sensedYawRate = icmNext.yawRate;
                sys->imuDataTick = sys->tickCount;
                sys->imuYawSampleTick = sys->tickCount;
                sys->imuDataValid = 1u;
                sys->targetYaw = icmNext.yaw;
                sys->yawAlignPending = 0u;
                control_exit_critical(primask);
                return 1u;
            }
        } else {
            g_icmFail++;
            g_icmReadFailCount = g_icmFail;
        }
        Delay_ms(1);
    }

    return 0u;
}

uint8_t Control_Start(ControlSystem_t *sys) {
    send_trace("RUN_STEP=ENTER");
    Control_Stop(sys);
    send_trace("RUN_STEP=READ_IMU");
    if (!control_wait_imu_ready(sys, IMU_START_WAIT_MS)) {
        sys->isRunning = 0;
        send_trace("RUN_STEP=IMU_FAIL");
        return 0u;
    }
    send_trace("RUN_STEP=IMU_OK");
    control_imu_zero_update(sys);
    ICM42688_ResetAttitude(&sys->icm);
    sys->angleZero = sys->icm.pitch;
    sys->targetYaw = sys->icm.yaw;
    sys->yawAlignPending = 0u;
    sys->sensedPitch = sys->icm.pitch;
    sys->sensedYaw = sys->icm.yaw;
    sys->sensedYawRate = sys->icm.yawRate;
    sys->imuDataTick = sys->tickCount;
    sys->imuYawSampleTick = sys->tickCount;
    sys->imuDataValid = 1u;
    (void)control_sync_start_heading(sys, 20u);

    control_reset_cascade_state(sys);
    control_reset_output(sys);
    sys->imuLastUpdateTick = sys->tickCount;
    sys->imuYawSampleTick = sys->tickCount;
    sys->imuPollLastTick = sys->tickCount;
    sys->angleLoopLastTick = sys->tickCount;
    sys->hbLastTick = sys->tickCount;
    sys->binLastTick = sys->tickCount;
    sys->expSampleLastTick = 0u;
    sys->expStreamLastTick = 0u;
    sys->sensedPitch = sys->icm.pitch;
    sys->sensedYaw = sys->icm.yaw;
    sys->sensedYawRate = sys->icm.yawRate;
    sys->anglePrevActual = sys->sensedPitch - sys->angleZero;
    sys->imuDataTick = sys->tickCount;
    sys->imuDataValid = 1u;

    send_trace("RUN_STEP=ENABLE");
    {
        uint32_t primask = control_enter_critical();
        sys->isRunning = 1;
        sys->runStartTick = sys->tickCount;
        control_reset_output(sys);
        Encoder_Reset();
        ICM42688_SetBiasTrackEnabled(0u);
        Motor_Enable();
        control_exit_critical(primask);
    }
    send_trace("RUN_STEP=DONE");
    return 1u;
}

void Control_Stop(ControlSystem_t *sys) {
    uint32_t primask = control_enter_critical();
    sys->isRunning = 0;
    control_reset_output(sys);
    control_reset_cascade_state(sys);
    sys->yawAlignPending = 0u;
    sys->imuLastUpdateTick = 0u;
    sys->imuPollLastTick = 0u;
    sys->angleLoopLastTick = 0u;
    ICM42688_SetBiasTrackEnabled(1u);
    Motor_Stop();
    Motor_Disable();
    Encoder_Reset();
    control_exit_critical(primask);
}

void Control_LockHeading(ControlSystem_t *sys) {
    uint32_t primask = control_enter_critical();
    sys->angleZero = sys->sensedPitch;
    sys->targetYaw = sys->sensedYaw;
    sys->yawAlignPending = 0u;
    control_reset_cascade_state(sys);
    control_exit_critical(primask);
}

void Control_SetTargetSpeed(ControlSystem_t *sys, float speed) {
    sys->targetSpeed = speed;
}

void Control_TimerTickISR(ControlSystem_t *sys) {
    if (!sys) return;

    sys->tickCount++;
    sys->loopTickCount++;
    if ((sys->loopTickCount % CONTROL_LOOP_PERIOD_MS) == 0u) {
        control_speed_loop(sys);
    }
    if ((sys->tickCount - sys->angleLoopLastTick) >= (uint32_t)ANGLE_LOOP_PERIOD_MS) {
        control_heading_loop(sys);
    }
    if (sys->expActive && sys->expSamplePeriodMs > 0) {
        uint32_t t_ms = (sys->tickCount - sys->expStartTick);
        if ((t_ms - sys->expSampleLastTick) >= (uint32_t)sys->expSamplePeriodMs) {
            sys->expSampleLastTick = t_ms;
            control_capture_exp_sample(sys, t_ms);
        }
    }
}

void Control_Background(ControlSystem_t *sys) {
    char cmd[80];
    uint8_t got;

    {
        uint8_t n = 0;
        while (n < 16) {
            got = VOFA_TakeCommand(cmd, sizeof(cmd));
            if (!got) break;
            control_parse_cmd(sys, cmd);
            n++;
        }
    }

    if ((sys->tickCount - sys->imuPollLastTick) >= (uint32_t)ANGLE_LOOP_PERIOD_MS) {
        control_poll_imu(sys);
    }

    if (g_binMode) {
        if ((sys->tickCount - sys->binLastTick) >= (uint32_t)BIN_SEND_PERIOD_MS) {
            sys->binLastTick = sys->tickCount;
            if (g_binMode == VOFA_MODE_JUSTFLOAT5) control_send_bin5(sys);
            else control_send_bin3(sys);
        }
    } else {
        if ((!sys->expActive || !sys->expStreamEnabled) && !sys->expDumpReady) {
            if ((sys->tickCount - sys->hbLastTick) >= (uint32_t)VOFA_SEND_PERIOD_MS) {
                sys->hbLastTick = sys->tickCount;
                control_send_hb(sys);
            }
        }
    }

    if (sys->expActive && sys->expStreamEnabled) {
        uint32_t t_ms = (sys->tickCount - sys->expStartTick);
        if ((t_ms - sys->expStreamLastTick) >= (uint32_t)EXP_SAMPLE_PERIOD_MS) {
            sys->expStreamLastTick = t_ms;
            if (g_binMode == VOFA_MODE_FIREWATER) {
                exp_stream_line(sys, t_ms);
            }
        }
    }

    if (sys->expActive && sys->expDurationMs > 0) {
        uint32_t dt = (sys->tickCount - sys->expStartTick);
        if (dt >= sys->expDurationMs) {
            Control_Stop(sys);
            sys->expActive = 0;
            sys->expDumpReady = 1;
            {
                char b[40];
                snprintf(b, sizeof(b), "EXP_TIMEOUT id=%u\r\n", (unsigned)sys->expId);
                control_send_text(b);
            }
            {
                char b[36];
                snprintf(b, sizeof(b), "EXP_END id=%u\r\n", (unsigned)sys->expId);
                control_send_text(b);
            }
        }
    }
}

void Control_Tick(ControlSystem_t *sys) {
    Control_Background(sys);
}
