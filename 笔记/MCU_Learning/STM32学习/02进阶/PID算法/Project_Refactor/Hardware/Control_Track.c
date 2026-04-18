#include "Control_Track.h"
#include "Encoder_Timer.h"
#include "ICM42688.h"
#include "Motor.h"
#include "VOFA.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TRACK_SENSOR_ACTIVE_LOW       1u
#define TRACK_CONTROL_PERIOD_MS       10u
#define TRACK_VOFA_SEND_PERIOD_MS     20u
#define TRACK_BASE_PWM_MIN            0
#define TRACK_PWM_MAX                 72
#define TRACK_TARGET_SPEED_DEFAULT    18.0f
#define TRACK_SPEED_TARGET_SCALE      0.10f
#define TRACK_SPEED_TARGET_RAMP_STEP  0.12f
#define TRACK_CMD_TO_PWM_GAIN         16.0f
#define TRACK_SPEED_KP                10.0f
#define TRACK_SPEED_KI                0.25f
#define TRACK_SPEED_KD                0.0f
#define TRACK_SPEED_FF_GAIN           4.0f
#define TRACK_SPEED_INTEGRAL_DECAY    0.75f
#define TRACK_SPEED_FEEDBACK_WINDOW_TICKS 4u
#define TRACK_SPEED_FEEDBACK_RPS_PER_RPM (1.0f / 60.0f)
#define TRACK_SPEED_START_PWM_MIN     26.0f
#define TRACK_SPEED_START_ACTUAL_MAX  0.50f
#define TRACK_SPEED_START_TARGET_MIN  0.60f
#define TRACK_SPEED_START_KICK_MS     800u
#define TRACK_SPEED_I_LIMIT           240.0f
#define TRACK_SPEED_OUT_LIMIT         100.0f
#define TRACK_SPEED_OUT_SLEW_STEP     6.0f
#define TRACK_LINE_KP                 0.40f
#define TRACK_LINE_KI                 0.004f
#define TRACK_LINE_KD                 0.00f
#define TRACK_LINE_I_LIMIT            120.0f
#define TRACK_LINE_DIFF_TARGET_LIMIT  0.40f
#define TRACK_LINE_DIFF_STEP          0.05f
#define TRACK_SPEED_TARGET_CLAMP_MIN  0.35f
#define TRACK_SPEED_TARGET_CLAMP_MAX  1.80f
#define TRACK_LINE_RECOVERY_MIN_ERROR 0.30f
#define TRACK_LINE_DEADBAND           0.15f
#define TRACK_LINE_LOST_STOP_COUNT    80u
#define TRACK_POSITION_FILTER_ALPHA   0.45f
#define TRACK_CMD_MAX_PER_CALL        16u
#define TRACK_STARTUP_SOFT_MS         600u

#define MAIN_MODE_STRAIGHT            0u
#define MAIN_MODE_TRACK               1u

extern volatile uint8_t g_mainSelectedMode;

#define TRACK_S1_PORT                 GPIOA
#define TRACK_S1_PIN                  GPIO_Pin_10
#define TRACK_S2_PORT                 GPIOA
#define TRACK_S2_PIN                  GPIO_Pin_11
#define TRACK_S3_PORT                 GPIOA
#define TRACK_S3_PIN                  GPIO_Pin_12
#define TRACK_S4_PORT                 GPIOB
#define TRACK_S4_PIN                  GPIO_Pin_3
#define TRACK_S5_PORT                 GPIOB
#define TRACK_S5_PIN                  GPIO_Pin_4
#define TRACK_S6_PORT                 GPIOB
#define TRACK_S6_PIN                  GPIO_Pin_9
#define TRACK_S7_PORT                 GPIOB
#define TRACK_S7_PIN                  GPIO_Pin_11
#define TRACK_S8_PORT                 GPIOC
#define TRACK_S8_PIN                  GPIO_Pin_13

static const int16_t g_trackSensorWeights[8] = { -350, -250, -150, -50, 50, 150, 250, 350 };

static float track_absf(float v)
{
    return (v >= 0.0f) ? v : -v;
}

static float track_shape_line_position(float rawPosition, uint8_t sensorCount)
{
    (void)sensorCount;
    return rawPosition;
}

static float track_clampf(float v, float minV, float maxV)
{
    if (v < minV) return minV;
    if (v > maxV) return maxV;
    return v;
}

static int16_t track_clamp_i16(int16_t v, int16_t minV, int16_t maxV)
{
    if (v < minV) return minV;
    if (v > maxV) return maxV;
    return v;
}

static float track_parse_float_after_eq(const char *s)
{
    const char *p = strchr(s, '=');
    if (!p) return 0.0f;
    return (float)atof(p + 1);
}

static int16_t track_parse_int_after_eq(const char *s)
{
    return (int16_t)track_parse_float_after_eq(s);
}

static void track_send_text(const char *text)
{
    if (!text) return;
    VOFA_SendString(text);
}

static void track_send_ok(const char *tag)
{
    char out[32];
    snprintf(out, sizeof(out), "OK %s\r\n", tag);
    track_send_text(out);
}

static void track_send_err(void)
{
    track_send_text("ERR\r\n");
}

static void track_sensor_gpio_init(void)
{
    GPIO_InitTypeDef g;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_AFIO | RCC_APB2Periph_GPIOA | RCC_APB2Periph_GPIOB | RCC_APB2Periph_GPIOC, ENABLE);
    GPIO_PinRemapConfig(GPIO_Remap_SWJ_JTAGDisable, ENABLE);

    g.GPIO_Mode = GPIO_Mode_IPU;
    g.GPIO_Speed = GPIO_Speed_50MHz;

    g.GPIO_Pin = TRACK_S1_PIN | TRACK_S2_PIN | TRACK_S3_PIN;
    GPIO_Init(GPIOA, &g);

    g.GPIO_Pin = TRACK_S4_PIN | TRACK_S5_PIN | TRACK_S6_PIN | TRACK_S7_PIN;
    GPIO_Init(GPIOB, &g);

    g.GPIO_Pin = TRACK_S8_PIN;
    GPIO_Init(GPIOC, &g);
}

static uint8_t track_sensor_is_active(GPIO_TypeDef *port, uint16_t pin)
{
    uint8_t level = (GPIO_ReadInputDataBit(port, pin) == Bit_SET) ? 1u : 0u;
#if TRACK_SENSOR_ACTIVE_LOW
    return level ? 0u : 1u;
#else
    return level;
#endif
}

static uint8_t track_read_sensor_bits(void)
{
    uint8_t bits = 0u;

    if (track_sensor_is_active(TRACK_S1_PORT, TRACK_S1_PIN)) bits |= (uint8_t)(1u << 0);
    if (track_sensor_is_active(TRACK_S2_PORT, TRACK_S2_PIN)) bits |= (uint8_t)(1u << 1);
    if (track_sensor_is_active(TRACK_S3_PORT, TRACK_S3_PIN)) bits |= (uint8_t)(1u << 2);
    if (track_sensor_is_active(TRACK_S4_PORT, TRACK_S4_PIN)) bits |= (uint8_t)(1u << 3);
    if (track_sensor_is_active(TRACK_S5_PORT, TRACK_S5_PIN)) bits |= (uint8_t)(1u << 4);
    if (track_sensor_is_active(TRACK_S6_PORT, TRACK_S6_PIN)) bits |= (uint8_t)(1u << 5);
    if (track_sensor_is_active(TRACK_S7_PORT, TRACK_S7_PIN)) bits |= (uint8_t)(1u << 6);
    if (track_sensor_is_active(TRACK_S8_PORT, TRACK_S8_PIN)) bits |= (uint8_t)(1u << 7);

    return bits;
}

static uint8_t track_bit_count(uint8_t bits)
{
    uint8_t n = 0u;
    while (bits) {
        if (bits & 0x01u) n++;
        bits >>= 1;
    }
    return n;
}

static uint8_t track_sample_line(ControlTrackSystem_t *sys)
{
    uint8_t bits;
    uint8_t count;
    uint8_t i;
    int32_t sum = 0;
    uint8_t hadLine;
    float rawPosition;
    float shapedPosition;

    bits = track_read_sensor_bits();
    count = track_bit_count(bits);

    sys->sensorBits = bits;
    sys->sensorCount = count;

    if (count == 0u) {
        sys->lineDetected = 0u;
        return 0u;
    }

    for (i = 0u; i < 8u; i++) {
        if ((bits & (uint8_t)(1u << i)) != 0u) {
            sum += g_trackSensorWeights[i];
        }
    }

    hadLine = sys->lineDetected;
    rawPosition = (float)sum / (float)count / 100.0f;
    shapedPosition = track_shape_line_position(rawPosition, count);
    sys->lineDetected = 1u;
    sys->linePositionRaw = rawPosition;
    if (!hadLine || (sys->lineMissingCount > 0u)) {
        sys->linePosition = shapedPosition;
    } else {
        sys->linePosition += (shapedPosition - sys->linePosition) * TRACK_POSITION_FILTER_ALPHA;
    }
    return 1u;
}

static void track_reset_speed_loop(ControlTrackSystem_t *sys)
{
    sys->speedTargetCurrent = 0.0f;
    sys->leftTargetSpeed = 0.0f;
    sys->rightTargetSpeed = 0.0f;
    sys->leftActualSpeed = 0.0f;
    sys->rightActualSpeed = 0.0f;
    sys->leftFeedbackAccum = 0;
    sys->rightFeedbackAccum = 0;
    sys->speedFeedbackTicks = 0u;
    sys->leftFeedbackSpeed = 0.0f;
    sys->rightFeedbackSpeed = 0.0f;
    sys->leftSpeedError = 0.0f;
    sys->rightSpeedError = 0.0f;
    sys->leftSpeedIntegral = 0.0f;
    sys->rightSpeedIntegral = 0.0f;
    sys->leftSpeedPrevError = 0.0f;
    sys->rightSpeedPrevError = 0.0f;
    sys->leftSpeedOut = 0.0f;
    sys->rightSpeedOut = 0.0f;
    sys->countDiffError = 0.0f;
    sys->countDiffIntegral = 0.0f;
    sys->countDiffPrevError = 0.0f;
    sys->countDiffOut = 0.0f;
}

static uint8_t track_update_feedback_speed(ControlTrackSystem_t *sys)
{
    uint32_t elapsedMs;
    float measuredLeft;
    float measuredRight;
    float alpha;

    if (!sys) {
        return 0u;
    }

    if (sys->feedbackSampleTick == 0u) {
        sys->feedbackSampleTick = sys->tickCount;
        return 0u;
    }

    elapsedMs = sys->tickCount - sys->feedbackSampleTick;
    if (elapsedMs < TRACK_CONTROL_PERIOD_MS) {
        return 0u;
    }

    Encoder_UpdateSpeed(&sys->encoder, (uint16_t)elapsedMs);
    sys->feedbackSampleTick = sys->tickCount;

    measuredLeft = sys->encoder.leftRPM * TRACK_SPEED_FEEDBACK_RPS_PER_RPM;
    measuredRight = sys->encoder.rightRPM * TRACK_SPEED_FEEDBACK_RPS_PER_RPM;
    alpha = (float)elapsedMs / ((float)TRACK_CONTROL_PERIOD_MS * (float)TRACK_SPEED_FEEDBACK_WINDOW_TICKS);
    alpha = track_clampf(alpha, 0.25f, 1.0f);

    if (sys->speedFeedbackTicks == 0u) {
        sys->leftFeedbackSpeed = measuredLeft;
        sys->rightFeedbackSpeed = measuredRight;
    } else {
        sys->leftFeedbackSpeed += (measuredLeft - sys->leftFeedbackSpeed) * alpha;
        sys->rightFeedbackSpeed += (measuredRight - sys->rightFeedbackSpeed) * alpha;
    }

    sys->leftFeedbackAccum = sys->encoder.leftSpeed;
    sys->rightFeedbackAccum = sys->encoder.rightSpeed;
    if (sys->speedFeedbackTicks < 255u) {
        sys->speedFeedbackTicks++;
    }
    return 1u;
}

static void track_reset_line_loop(ControlTrackSystem_t *sys)
{
    sys->lineError = 0.0f;
    sys->lineIntegral = 0.0f;
    sys->linePrevError = 0.0f;
    sys->linePidOut = 0.0f;
    sys->lineDiffTarget = 0.0f;
    sys->lastSeenError = 0.0f;
}

static float track_ramp_speed_target(ControlTrackSystem_t *sys)
{
    float target = track_clampf(sys->targetSpeed, 0.0f, 100.0f) * TRACK_SPEED_TARGET_SCALE;

    if (sys->speedTargetCurrent < target) {
        sys->speedTargetCurrent += TRACK_SPEED_TARGET_RAMP_STEP;
        if (sys->speedTargetCurrent > target) sys->speedTargetCurrent = target;
    } else if (sys->speedTargetCurrent > target) {
        sys->speedTargetCurrent -= TRACK_SPEED_TARGET_RAMP_STEP;
        if (sys->speedTargetCurrent < target) sys->speedTargetCurrent = target;
    }
    return sys->speedTargetCurrent;
}

static float track_startup_release(ControlTrackSystem_t *sys)
{
    uint32_t elapsed;

    if (!sys || !sys->isRunning) {
        return 1.0f;
    }

    elapsed = sys->tickCount - sys->runStartTick;
    if (elapsed >= TRACK_STARTUP_SOFT_MS) {
        return 1.0f;
    }

    return 0.35f + 0.65f * ((float)elapsed / (float)TRACK_STARTUP_SOFT_MS);
}

static float track_adjust_base_target(ControlTrackSystem_t *sys, float baseTarget)
{
    float release;
    release = track_startup_release(sys);
    baseTarget *= release;
    return track_clampf(baseTarget, 0.0f, TRACK_SPEED_TARGET_CLAMP_MAX);
}

static void track_mix_target_speeds(ControlTrackSystem_t *sys, float baseTarget)
{
    float diffTarget;
    float leftTarget;
    float rightTarget;
    float wheelMin;

    diffTarget = track_clampf(sys->linePidOut, -TRACK_LINE_DIFF_TARGET_LIMIT, TRACK_LINE_DIFF_TARGET_LIMIT);

    if (sys->lineDiffTarget < diffTarget) {
        sys->lineDiffTarget += TRACK_LINE_DIFF_STEP;
        if (sys->lineDiffTarget > diffTarget) {
            sys->lineDiffTarget = diffTarget;
        }
    } else if (sys->lineDiffTarget > diffTarget) {
        sys->lineDiffTarget -= TRACK_LINE_DIFF_STEP;
        if (sys->lineDiffTarget < diffTarget) {
            sys->lineDiffTarget = diffTarget;
        }
    }

    leftTarget = baseTarget + sys->lineDiffTarget;
    rightTarget = baseTarget - sys->lineDiffTarget;

    if (baseTarget > 0.0f) {
        wheelMin = (baseTarget >= TRACK_SPEED_TARGET_CLAMP_MIN) ? TRACK_SPEED_TARGET_CLAMP_MIN : 0.0f;
        leftTarget = track_clampf(leftTarget, wheelMin, TRACK_SPEED_TARGET_CLAMP_MAX);
        rightTarget = track_clampf(rightTarget, wheelMin, TRACK_SPEED_TARGET_CLAMP_MAX);
    } else {
        leftTarget = 0.0f;
        rightTarget = 0.0f;
    }

    sys->leftTargetSpeed = track_clampf(leftTarget, 0.0f, 20.0f);
    sys->rightTargetSpeed = track_clampf(rightTarget, 0.0f, 20.0f);
}

static void track_apply_output(ControlTrackSystem_t *sys, int16_t leftPwm, int16_t rightPwm)
{
    sys->leftPwm = leftPwm;
    sys->rightPwm = rightPwm;
    Motor_SetDiffSpeed(leftPwm, rightPwm);
}

static void track_stop_output(ControlTrackSystem_t *sys)
{
    sys->leftPwm = 0;
    sys->rightPwm = 0;
    sys->rawPwm = 0;
    sys->rawModeEnabled = 0u;
    sys->basePwm = 0;
    sys->linePidOut = 0.0f;
    sys->lineDiffTarget = 0.0f;
    sys->leftSpeedOut = 0.0f;
    sys->rightSpeedOut = 0.0f;
    Motor_Stop();
    Motor_Disable();
}

static void track_set_raw_output(ControlTrackSystem_t *sys, int16_t pwm)
{
    int16_t appliedPwm;

    if (!sys) {
        return;
    }

    appliedPwm = track_clamp_i16(pwm, (int16_t)(-((int16_t)sys->pwmMax)), (int16_t)sys->pwmMax);
    track_reset_speed_loop(sys);
    track_reset_line_loop(sys);
    sys->rawPwm = appliedPwm;
    sys->rawModeEnabled = (appliedPwm != 0) ? 1u : 0u;
    sys->basePwm = appliedPwm;
    sys->expActive = 0u;
    sys->expId = 0u;
    sys->expDurationMs = 0u;
    sys->lineMissingCount = 0u;
    sys->lineLostCount = 0u;
    sys->lineDetected = 0u;
    sys->sensorBits = 0u;
    sys->sensorCount = 0u;
    sys->linePosition = 0.0f;
    sys->linePositionRaw = 0.0f;
    sys->lineError = 0.0f;
    sys->lastSeenError = 0.0f;
    memset(&sys->encoder, 0, sizeof(sys->encoder));
    Encoder_Reset();
    sys->runStartTick = sys->tickCount;
    sys->lastControlTick = sys->tickCount;
    sys->hbLastTick = sys->tickCount;
    sys->feedbackSampleTick = sys->tickCount;

    if (appliedPwm == 0) {
        track_stop_output(sys);
        return;
    }

    Motor_Enable();
    track_apply_output(sys, appliedPwm, appliedPwm);
}

static void track_poll_imu(ControlTrackSystem_t *sys)
{
    float dt = 0.005f;
    uint32_t pollTick;
    uint32_t prevYawSampleTick;
    ICM42688_Data_t icmNext;

    memcpy(&icmNext, &sys->icm, sizeof(icmNext));
    prevYawSampleTick = sys->imuYawSampleTick;

    if (!ICM42688_ReadAll(&icmNext)) {
        return;
    }

    pollTick = sys->tickCount;
    if (icmNext.yawSampleUpdated) {
        if (prevYawSampleTick != 0u && pollTick >= prevYawSampleTick) {
            dt = (float)(pollTick - prevYawSampleTick) * 0.001f;
            if (dt <= 0.0f) {
                dt = 0.005f;
            }
        }
        ICM42688_UpdateYaw(&icmNext, dt);
        sys->imuYawSampleTick = pollTick;
    }

    memcpy(&sys->icm, &icmNext, sizeof(sys->icm));
    sys->imuDataValid = 1u;
}

static void track_update_raw_mode(ControlTrackSystem_t *sys)
{
    uint8_t feedbackReady;

    if (!sys || !sys->rawModeEnabled) {
        return;
    }

    track_poll_imu(sys);
    feedbackReady = track_update_feedback_speed(sys);
    if (feedbackReady) {
        sys->leftActualSpeed = sys->leftFeedbackSpeed;
        sys->rightActualSpeed = sys->rightFeedbackSpeed;
    }

    sys->speedTargetCurrent = 0.0f;
    sys->leftTargetSpeed = 0.0f;
    sys->rightTargetSpeed = 0.0f;
    sys->leftSpeedError = 0.0f;
    sys->rightSpeedError = 0.0f;
    sys->leftSpeedOut = (float)sys->leftPwm;
    sys->rightSpeedOut = (float)sys->rightPwm;
    sys->basePwm = sys->rawPwm;
    sys->sensorBits = track_read_sensor_bits();
    sys->sensorCount = track_bit_count(sys->sensorBits);
    sys->lineDetected = (sys->sensorCount != 0u) ? 1u : 0u;
    sys->linePosition = 0.0f;
    sys->linePositionRaw = 0.0f;
}

static void track_send_hb(ControlTrackSystem_t *sys)
{
    static char out[640];
    uint32_t tMs = 0u;

    if (sys->isRunning || sys->expActive || sys->rawModeEnabled) {
        tMs = sys->tickCount - sys->runStartTick;
    }

    snprintf(out, sizeof(out),
             "TRK tick=%lu exp_id=%u t_ms=%lu run=%u ts=%.3f kp=%.3f ki=%.3f kd=%.3f st=%.3f lt=%.3f rt=%.3f la=%.3f ra=%.3f lse=%.3f rse=%.3f lso=%.3f rso=%.3f lc=%ld rc=%ld cd=%ld cde=%.3f cdo=%.3f lp=%.3f lpr=%.3f y=%.3f yr=%.3f el=%.3f er=%.3f ed=%.3f dl=%d dr=%d cl=%u cr=%u L=%d R=%d s1=%u s2=%u s3=%u s4=%u s5=%u s6=%u s7=%u s8=%u rx=%lu txdrop=%lu\r\n",
             (unsigned long)sys->tickCount,
             (unsigned)sys->expId,
             (unsigned long)tMs,
             (unsigned)sys->isRunning,
             (double)sys->targetSpeed,
             (double)sys->lineKp,
             (double)sys->lineKi,
             (double)sys->lineKd,
             (double)sys->speedTargetCurrent,
             (double)sys->leftTargetSpeed,
             (double)sys->rightTargetSpeed,
             (double)sys->leftActualSpeed,
             (double)sys->rightActualSpeed,
             (double)sys->leftSpeedError,
             (double)sys->rightSpeedError,
             (double)sys->leftSpeedOut,
             (double)sys->rightSpeedOut,
             (long)sys->encoder.leftCount,
             (long)sys->encoder.rightCount,
             (long)(sys->encoder.leftCount - sys->encoder.rightCount),
             (double)sys->countDiffError,
             (double)sys->countDiffOut,
             (double)sys->linePosition,
             (double)sys->linePositionRaw,
             (double)sys->icm.yaw,
             (double)sys->icm.yawRate,
             (double)sys->leftFeedbackSpeed,
             (double)sys->rightFeedbackSpeed,
             (double)(sys->leftFeedbackSpeed - sys->rightFeedbackSpeed),
             (int)sys->encoder.rawLeftDelta,
             (int)sys->encoder.rawRightDelta,
             (unsigned)sys->encoder.leftDeltaClamped,
             (unsigned)sys->encoder.rightDeltaClamped,
             (int)sys->leftPwm,
             (int)sys->rightPwm,
             (unsigned)((sys->sensorBits >> 0) & 0x01u),
             (unsigned)((sys->sensorBits >> 1) & 0x01u),
             (unsigned)((sys->sensorBits >> 2) & 0x01u),
             (unsigned)((sys->sensorBits >> 3) & 0x01u),
             (unsigned)((sys->sensorBits >> 4) & 0x01u),
             (unsigned)((sys->sensorBits >> 5) & 0x01u),
             (unsigned)((sys->sensorBits >> 6) & 0x01u),
             (unsigned)((sys->sensorBits >> 7) & 0x01u),
             (unsigned long)VOFA_GetRxByteCount(),
             (unsigned long)VOFA_GetTxDropByteCount());
    track_send_text(out);
}

static void track_send_stat(ControlTrackSystem_t *sys)
{
    static char out[832];
    uint32_t tMs = 0u;

    if (sys->isRunning || sys->expActive || sys->rawModeEnabled) {
        tMs = sys->tickCount - sys->runStartTick;
    }

    snprintf(out, sizeof(out),
             "STAT tick=%lu exp_id=%u t_ms=%lu run=%u ts=%.6f skp=%.6f ski=%.6f skd=%.6f kp=%.6f ki=%.6f kd=%.6f st=%.6f lt=%.6f rt=%.6f la=%.6f ra=%.6f lse=%.6f rse=%.6f lso=%.6f rso=%.6f lc=%ld rc=%ld cd=%ld cde=%.6f cdi=%.6f cdo=%.6f lp=%.6f lpr=%.6f le=%.6f li=%.6f lout=%.6f diff=%.6f y=%.6f yr=%.6f el=%.3f er=%.3f ed=%.3f dl=%d dr=%d cl=%u cr=%u L=%d R=%d pwm_max=%u miss=%u lost=%u s1=%u s2=%u s3=%u s4=%u s5=%u s6=%u s7=%u s8=%u rx=%lu txdrop=%lu\r\n",
             (unsigned long)sys->tickCount,
             (unsigned)sys->expId,
             (unsigned long)tMs,
             (unsigned)sys->isRunning,
             (double)sys->targetSpeed,
             (double)sys->speedKp,
             (double)sys->speedKi,
             (double)sys->speedKd,
             (double)sys->lineKp,
             (double)sys->lineKi,
             (double)sys->lineKd,
             (double)sys->speedTargetCurrent,
             (double)sys->leftTargetSpeed,
             (double)sys->rightTargetSpeed,
             (double)sys->leftActualSpeed,
             (double)sys->rightActualSpeed,
             (double)sys->leftSpeedError,
             (double)sys->rightSpeedError,
             (double)sys->leftSpeedOut,
             (double)sys->rightSpeedOut,
             (long)sys->encoder.leftCount,
             (long)sys->encoder.rightCount,
             (long)(sys->encoder.leftCount - sys->encoder.rightCount),
             (double)sys->countDiffError,
             (double)sys->countDiffIntegral,
             (double)sys->countDiffOut,
             (double)sys->linePosition,
             (double)sys->linePositionRaw,
             (double)sys->lineError,
             (double)sys->lineIntegral,
             (double)sys->linePidOut,
             (double)sys->lineDiffTarget,
             (double)sys->icm.yaw,
             (double)sys->icm.yawRate,
             (double)sys->leftFeedbackSpeed,
             (double)sys->rightFeedbackSpeed,
             (double)(sys->leftFeedbackSpeed - sys->rightFeedbackSpeed),
             (int)sys->encoder.rawLeftDelta,
             (int)sys->encoder.rawRightDelta,
             (unsigned)sys->encoder.leftDeltaClamped,
             (unsigned)sys->encoder.rightDeltaClamped,
             (int)sys->leftPwm,
             (int)sys->rightPwm,
             (unsigned)sys->pwmMax,
             (unsigned)sys->lineMissingCount,
             (unsigned)sys->lineLostCount,
             (unsigned)((sys->sensorBits >> 0) & 0x01u),
             (unsigned)((sys->sensorBits >> 1) & 0x01u),
             (unsigned)((sys->sensorBits >> 2) & 0x01u),
             (unsigned)((sys->sensorBits >> 3) & 0x01u),
             (unsigned)((sys->sensorBits >> 4) & 0x01u),
             (unsigned)((sys->sensorBits >> 5) & 0x01u),
             (unsigned)((sys->sensorBits >> 6) & 0x01u),
             (unsigned)((sys->sensorBits >> 7) & 0x01u),
             (unsigned long)VOFA_GetRxByteCount(),
             (unsigned long)VOFA_GetTxDropByteCount());
    track_send_text(out);
}

static void track_stop_experiment(ControlTrackSystem_t *sys)
{
    if (!sys) return;
    sys->expActive = 0u;
    sys->expDurationMs = 0u;
}

static void track_handle_exp_timeout(ControlTrackSystem_t *sys)
{
    char out[40];

    if (!sys->expActive) return;
    if ((sys->tickCount - sys->expStartTick) < sys->expDurationMs) return;

    track_stop_experiment(sys);
    if (sys->isRunning) {
        ControlTrack_Stop(sys);
    }
    snprintf(out, sizeof(out), "EXP_END id=%u\r\n", (unsigned)sys->expId);
    track_send_text(out);
}

static void track_parse_cmd(ControlTrackSystem_t *sys, const char *cmd)
{
    unsigned id;
    unsigned ms;

    if (!sys || !cmd) return;

    if (strcmp(cmd, "#RUN") == 0 || strcmp(cmd, "#RUN!") == 0) {
        if (sys->rawModeEnabled) {
            track_set_raw_output(sys, 0);
        }
        if (!sys->isRunning) {
            if (!ControlTrack_Start(sys)) {
                track_send_err();
                return;
            }
        }
        track_send_ok("RUN");
        return;
    }
    if (strcmp(cmd, "#STOP") == 0 || strcmp(cmd, "#STOP!") == 0) {
        track_stop_experiment(sys);
        ControlTrack_Stop(sys);
        track_send_ok("STOP");
        return;
    }
    if (strcmp(cmd, "#STAT") == 0 || strcmp(cmd, "#STAT!") == 0) {
        track_send_stat(sys);
        return;
    }
    if (strcmp(cmd, "#CAL") == 0 || strcmp(cmd, "#CAL!") == 0) {
        if (ICM42688_ReadAll(&sys->icm)) {
            ICM42688_ResetAttitude(&sys->icm);
            sys->imuYawSampleTick = sys->tickCount;
            sys->imuDataValid = 1u;
        }
        track_send_ok("CAL");
        return;
    }
    if (strcmp(cmd, "#MODE=TRACK") == 0 || strcmp(cmd, "#MODE=TRACK!") == 0) {
        g_mainSelectedMode = MAIN_MODE_TRACK;
        track_send_ok("MODE");
        return;
    }
    if (strcmp(cmd, "#MODE=STRAIGHT") == 0 || strcmp(cmd, "#MODE=STRAIGHT!") == 0) {
        if (sys->isRunning) {
            track_send_err();
            return;
        }
        g_mainSelectedMode = MAIN_MODE_STRAIGHT;
        track_send_ok("MODE");
        return;
    }
    if (strncmp(cmd, "#EXP=STREAM,", 12) == 0) {
        unsigned en = 0u;
        if (sscanf(cmd, "#EXP=STREAM,%u", &en) == 1) {
            sys->expStreamEnabled = (en != 0u) ? 1u : 0u;
            track_send_ok("EXP_STREAM");
        } else {
            track_send_err();
        }
        return;
    }
    if (sscanf(cmd, "#EXP=RUN,%u,%u", &id, &ms) == 2) {
        if (sys->rawModeEnabled) {
            track_set_raw_output(sys, 0);
        }
        sys->expId = (uint16_t)id;
        sys->expStartTick = sys->tickCount;
        sys->expDurationMs = (uint32_t)ms;
        sys->expActive = 1u;
        if (!sys->isRunning) {
            if (!ControlTrack_Start(sys)) {
                track_stop_experiment(sys);
                track_send_err();
                return;
            }
        }
        track_send_ok("EXP_START");
        return;
    }
    if (sscanf(cmd, "#EXP=STOP,%u", &id) == 1) {
        if ((uint16_t)id == sys->expId || sys->expId == 0u) {
            track_stop_experiment(sys);
            ControlTrack_Stop(sys);
            track_send_ok("EXP_STOP");
            return;
        }
        track_send_err();
        return;
    }
    if (strncmp(cmd, "#TS=", 4) == 0) {
        sys->targetSpeed = track_parse_float_after_eq(cmd);
        track_send_ok("TS");
        return;
    }
    if (strncmp(cmd, "#SKP=", 5) == 0) {
        sys->speedKp = track_parse_float_after_eq(cmd);
        track_reset_speed_loop(sys);
        track_send_ok("SKP");
        return;
    }
    if (strncmp(cmd, "#SKI=", 5) == 0) {
        sys->speedKi = track_parse_float_after_eq(cmd);
        track_reset_speed_loop(sys);
        track_send_ok("SKI");
        return;
    }
    if (strncmp(cmd, "#SKD=", 5) == 0) {
        sys->speedKd = track_parse_float_after_eq(cmd);
        track_reset_speed_loop(sys);
        track_send_ok("SKD");
        return;
    }
    if (strncmp(cmd, "#AKP=", 5) == 0 || strncmp(cmd, "#KP=", 4) == 0) {
        sys->lineKp = track_parse_float_after_eq(cmd);
        track_reset_line_loop(sys);
        track_send_ok("AKP");
        return;
    }
    if (strncmp(cmd, "#AKI=", 5) == 0 || strncmp(cmd, "#KI=", 4) == 0) {
        sys->lineKi = track_parse_float_after_eq(cmd);
        track_reset_line_loop(sys);
        track_send_ok("AKI");
        return;
    }
    if (strncmp(cmd, "#AKD=", 5) == 0 || strncmp(cmd, "#KD=", 4) == 0) {
        sys->lineKd = track_parse_float_after_eq(cmd);
        track_reset_line_loop(sys);
        track_send_ok("AKD");
        return;
    }
    if (strncmp(cmd, "#RAW=", 5) == 0) {
        int16_t v = track_parse_int_after_eq(cmd);
        if (sys->isRunning) {
            track_stop_experiment(sys);
            ControlTrack_Stop(sys);
        }
        track_set_raw_output(sys, v);
        track_send_ok("RAW");
        return;
    }
    if (strncmp(cmd, "#PWM_MAX=", 9) == 0) {
        int16_t v = track_parse_int_after_eq(cmd);
        sys->pwmMax = (uint16_t)track_clamp_i16(v, TRACK_BASE_PWM_MIN, 100);
        track_send_ok("PWM_MAX");
        return;
    }

    track_send_err();
}

static void track_update_line_outer_loop(ControlTrackSystem_t *sys)
{
    float error;
    float derivative;
    float pidOut;

    if (!track_sample_line(sys)) {
        if (sys->lineMissingCount < 255u) {
            sys->lineMissingCount++;
        }

        sys->lineDetected = 0u;
        sys->lineLostCount = sys->lineMissingCount;
        if (sys->lineMissingCount >= TRACK_LINE_LOST_STOP_COUNT) {
            ControlTrack_Stop(sys);
            return;
        }

        error = sys->lastSeenError;
        if (track_absf(error) < TRACK_LINE_RECOVERY_MIN_ERROR) {
            error = (error >= 0.0f) ? TRACK_LINE_RECOVERY_MIN_ERROR : -TRACK_LINE_RECOVERY_MIN_ERROR;
        }
        sys->linePosition = error;
    } else {
        sys->lineMissingCount = 0u;
        sys->lineLostCount = 0u;
        error = sys->linePosition;
        sys->lastSeenError = error;
    }

    sys->lineError = error;

    if (track_absf(error) <= TRACK_LINE_DEADBAND) {
        sys->lineIntegral *= TRACK_SPEED_INTEGRAL_DECAY;
        sys->linePidOut = 0.0f;
        sys->lineDiffTarget = 0.0f;
        sys->linePrevError = error;
        return;
    }

    sys->lineIntegral += error;
    sys->lineIntegral = track_clampf(sys->lineIntegral, -TRACK_LINE_I_LIMIT, TRACK_LINE_I_LIMIT);
    derivative = error - sys->linePrevError;
    pidOut = sys->lineKp * error + sys->lineKi * sys->lineIntegral + sys->lineKd * derivative;

    sys->linePidOut = track_clampf(pidOut, -TRACK_LINE_DIFF_TARGET_LIMIT, TRACK_LINE_DIFF_TARGET_LIMIT);
    sys->lineDiffTarget = sys->linePidOut;
    sys->linePrevError = error;
}

static float track_apply_output_slew(float targetOut, float currentOut)
{
    if (targetOut > currentOut + TRACK_SPEED_OUT_SLEW_STEP) {
        return currentOut + TRACK_SPEED_OUT_SLEW_STEP;
    }

    if (targetOut < currentOut - TRACK_SPEED_OUT_SLEW_STEP) {
        return currentOut - TRACK_SPEED_OUT_SLEW_STEP;
    }

    return targetOut;
}

static void track_apply_start_pwm_floor_pair(ControlTrackSystem_t *sys,
    float leftTarget, float rightTarget,
    float leftActual, float rightActual,
    float *leftOut, float *rightOut)
{
    uint32_t elapsed;
    float avg;
    float halfDiff;

    if (!sys || !leftOut || !rightOut) {
        return;
    }

    elapsed = sys->tickCount - sys->runStartTick;
    if (elapsed >= TRACK_SPEED_START_KICK_MS) {
        return;
    }

    if (leftTarget < TRACK_SPEED_START_TARGET_MIN && rightTarget < TRACK_SPEED_START_TARGET_MIN) {
        return;
    }

    if (track_absf(leftActual) > TRACK_SPEED_START_ACTUAL_MAX ||
        track_absf(rightActual) > TRACK_SPEED_START_ACTUAL_MAX) {
        return;
    }

    avg = (*leftOut + *rightOut) * 0.5f;
    if (avg >= TRACK_SPEED_START_PWM_MIN) {
        return;
    }

    halfDiff = (*leftOut - *rightOut) * 0.5f;
    avg = TRACK_SPEED_START_PWM_MIN;
    *leftOut = avg + halfDiff;
    *rightOut = avg - halfDiff;
}

static void track_control_step(ControlTrackSystem_t *sys)
{
    float baseTarget;
    float leftTargetPulse;
    float rightTargetPulse;
    float leftOutTarget;
    float rightOutTarget;
    uint8_t feedbackReady;
    int16_t leftPwm;
    int16_t rightPwm;

    track_poll_imu(sys);
    feedbackReady = track_update_feedback_speed(sys);

    if (feedbackReady) {
        sys->leftActualSpeed = sys->leftFeedbackSpeed;
        sys->rightActualSpeed = sys->rightFeedbackSpeed;
    }

    track_update_line_outer_loop(sys);
    if (!sys->isRunning) {
        return;
    }

    baseTarget = track_ramp_speed_target(sys);
    baseTarget = track_adjust_base_target(sys, baseTarget);
    track_mix_target_speeds(sys, baseTarget);

    leftTargetPulse = sys->leftTargetSpeed;
    rightTargetPulse = sys->rightTargetSpeed;

    leftOutTarget = leftTargetPulse * TRACK_CMD_TO_PWM_GAIN;
    rightOutTarget = rightTargetPulse * TRACK_CMD_TO_PWM_GAIN;

    sys->leftSpeedOut = track_apply_output_slew(leftOutTarget, (float)sys->leftPwm);
    sys->rightSpeedOut = track_apply_output_slew(rightOutTarget, (float)sys->rightPwm);
    track_apply_start_pwm_floor_pair(
        sys,
        sys->leftTargetSpeed,
        sys->rightTargetSpeed,
        sys->leftActualSpeed,
        sys->rightActualSpeed,
        &sys->leftSpeedOut,
        &sys->rightSpeedOut);

    sys->countDiffOut = 0.0f;
    sys->countDiffError = 0.0f;
    sys->countDiffIntegral = 0.0f;
    sys->countDiffPrevError = 0.0f;

    sys->leftSpeedOut = track_clampf(sys->leftSpeedOut, 0.0f, TRACK_SPEED_OUT_LIMIT);
    sys->rightSpeedOut = track_clampf(sys->rightSpeedOut, 0.0f, TRACK_SPEED_OUT_LIMIT);
    sys->leftSpeedOut = track_apply_output_slew(sys->leftSpeedOut, (float)sys->leftPwm);
    sys->rightSpeedOut = track_apply_output_slew(sys->rightSpeedOut, (float)sys->rightPwm);

    sys->leftSpeedError = leftTargetPulse - sys->leftActualSpeed;
    sys->rightSpeedError = rightTargetPulse - sys->rightActualSpeed;
    sys->basePwm = (int16_t)((sys->leftSpeedOut + sys->rightSpeedOut) * 0.5f);

    leftPwm = track_clamp_i16((int16_t)sys->leftSpeedOut, TRACK_BASE_PWM_MIN, (int16_t)sys->pwmMax);
    rightPwm = track_clamp_i16((int16_t)sys->rightSpeedOut, TRACK_BASE_PWM_MIN, (int16_t)sys->pwmMax);

    Motor_Enable();
    track_apply_output(sys, leftPwm, rightPwm);
}

void ControlTrack_Init(ControlTrackSystem_t *sys)
{
    if (!sys) return;
    memset(sys, 0, sizeof(*sys));
    track_sensor_gpio_init();
    sys->targetSpeed = TRACK_TARGET_SPEED_DEFAULT;
    sys->speedKp = TRACK_SPEED_KP;
    sys->speedKi = TRACK_SPEED_KI;
    sys->speedKd = TRACK_SPEED_KD;
    sys->lineKp = TRACK_LINE_KP;
    sys->lineKi = TRACK_LINE_KI;
    sys->lineKd = TRACK_LINE_KD;
    sys->pwmMax = TRACK_PWM_MAX;
    track_stop_output(sys);
}

uint8_t ControlTrack_Start(ControlTrackSystem_t *sys)
{
    if (!sys) return 0u;
    if (!track_sample_line(sys)) return 0u;

    memset(&sys->encoder, 0, sizeof(sys->encoder));
    memset(&sys->icm, 0, sizeof(sys->icm));
    Encoder_Reset();
    if (ICM42688_ReadAll(&sys->icm)) {
        ICM42688_ResetAttitude(&sys->icm);
        sys->imuDataValid = 1u;
    }

    sys->isRunning = 1u;
    sys->runStartTick = sys->tickCount;
    sys->lastControlTick = sys->tickCount;
    sys->hbLastTick = sys->tickCount;
    sys->imuYawSampleTick = sys->tickCount;
    sys->expStreamLastTick = sys->tickCount;
    sys->lineMissingCount = 0u;
    sys->lineLostCount = 0u;
    track_reset_speed_loop(sys);
    track_reset_line_loop(sys);
    sys->feedbackSampleTick = sys->tickCount;
    sys->linePositionRaw = sys->linePosition;
    sys->linePrevError = sys->linePosition;
    sys->lineError = sys->linePosition;
    sys->lastSeenError = sys->linePosition;
    sys->leftPwm = 0;
    sys->rightPwm = 0;
    Motor_Enable();
    return 1u;
}

void ControlTrack_Stop(ControlTrackSystem_t *sys)
{
    if (!sys) return;
    sys->isRunning = 0u;
    sys->lineMissingCount = 0u;
    sys->lineLostCount = 0u;
    sys->hbLastTick = 0u;
    sys->expStreamLastTick = 0u;
    sys->feedbackSampleTick = 0u;
    track_reset_speed_loop(sys);
    track_reset_line_loop(sys);
    Encoder_Reset();
    track_stop_output(sys);
}

void ControlTrack_TimerTickISR(ControlTrackSystem_t *sys)
{
    if (!sys) return;
    sys->tickCount++;
}

void ControlTrack_Background(ControlTrackSystem_t *sys)
{
    char cmd[80];
    uint8_t got;
    uint8_t n = 0u;

    if (!sys) return;

    while (n < TRACK_CMD_MAX_PER_CALL) {
        got = VOFA_TakeCommand(cmd, sizeof(cmd));
        if (!got) break;
        track_parse_cmd(sys, cmd);
        n++;
    }

    track_handle_exp_timeout(sys);

    if (sys->rawModeEnabled) {
        while ((sys->tickCount - sys->lastControlTick) >= TRACK_CONTROL_PERIOD_MS) {
            sys->lastControlTick += TRACK_CONTROL_PERIOD_MS;
            track_update_raw_mode(sys);
            if (!sys->rawModeEnabled) {
                break;
            }
        }

        if ((sys->tickCount - sys->hbLastTick) >= TRACK_VOFA_SEND_PERIOD_MS) {
            sys->hbLastTick = sys->tickCount;
            track_send_hb(sys);
        }
        return;
    }

    if (!sys->isRunning) {
        if (sys->expStreamEnabled && ((sys->tickCount - sys->hbLastTick) >= TRACK_VOFA_SEND_PERIOD_MS)) {
            sys->hbLastTick = sys->tickCount;
            track_send_hb(sys);
        }
        return;
    }

    while ((sys->tickCount - sys->lastControlTick) >= TRACK_CONTROL_PERIOD_MS) {
        sys->lastControlTick += TRACK_CONTROL_PERIOD_MS;
        track_control_step(sys);
        if (!sys->isRunning) {
            break;
        }
    }

    if ((sys->tickCount - sys->hbLastTick) >= TRACK_VOFA_SEND_PERIOD_MS) {
        sys->hbLastTick = sys->tickCount;
        track_send_hb(sys);
    }
}
