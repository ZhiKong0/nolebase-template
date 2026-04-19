/*
 * 统一调参参数层实现:
 * 1. 默认值集中来自 config.h，避免“代码逻辑”和“串口默认值”分叉。
 * 2. 参数表只暴露当前真实还存在的两条控制链:
 *    - 直线居中循迹前端
 *    - S 弯贴边循迹前端
 * 3. 已删掉 capture / recenter / 找线相关运行时接口。
 */
#include "tune_params.h"
#include <stddef.h>
#include <string.h>
#include <stdio.h>

#define TUNE_OFF(field) ((uint16_t)offsetof(TuneRuntime_t, field))

static const TuneRuntime_t s_tuneDefaults = {
    { PID_STRAIGHT_SPEED_TARGET, PID_STRAIGHT_SPEED_KP, PID_STRAIGHT_SPEED_KI, PID_STRAIGHT_SPEED_KD },
    { PID_STRAIGHT_HEADING_KP, PID_STRAIGHT_HEADING_KI, PID_STRAIGHT_HEADING_KD,
      PID_STRAIGHT_HEADING_DIFF_RATIO, PID_STRAIGHT_HEADING_DIFF_MIN },
    { PID_TRACK_SPEED_TARGET, PID_TRACK_SPEED_KP, PID_TRACK_SPEED_KI, PID_TRACK_SPEED_KD },
    {
        PID_TRACK_LINE_KP,
        PID_TRACK_LINE_KD,
        TRACK_POS_LPF,
        TRACK_DERIV_LPF,
        TRACK_LINE_CENTER_BIAS,
        TRACK_TARGET_YAW_LIMIT,
        TRACK_HEADING_DIFF_RATIO,
        TRACK_HEADING_DIFF_MIN,
        TRACK_LINE_LOSS_HOLD_MS,
        TRACK_LINE_LOSS_TIMEOUT_MS,
        TRACK_LINE_LOSS_YAW_DECAY,
        TRACK_LINE_LOSS_SPEED_SCALE,
        TRACK_CURVE_SPEED_POS_START,
        TRACK_CURVE_SPEED_POS_FULL,
        TRACK_CURVE_SPEED_SCALE_MIN
    },
    {
        TRACK_SCURVE_ENTER_YAW_RATE,
        TRACK_SCURVE_ENTER_ERROR,
        TRACK_SCURVE_ENTER_DELTA,
        TRACK_SCURVE_ENTER_YAW_CMD,
        TRACK_SCURVE_EXIT_ERROR,
        TRACK_SCURVE_CENTER_ZONE,
        TRACK_SCURVE_CENTER_GAIN,
        TRACK_SCURVE_EDGE_GAIN,
        TRACK_SCURVE_CENTER_SENSOR_GAIN,
        TRACK_SCURVE_INNER_SENSOR_GAIN,
        TRACK_SCURVE_OUTER_SENSOR_GAIN,
        TRACK_SCURVE_EDGE_SENSOR_GAIN,
        TRACK_SCURVE_SIDE_TARGET_POS_START,
        TRACK_SCURVE_SIDE_TARGET_POS_FULL,
        TRACK_SCURVE_SIDE_TARGET_INNER,
        TRACK_SCURVE_SIDE_TARGET_OUTER,
        TRACK_SCURVE_LINE_KP_SCALE,
        TRACK_SCURVE_YAW_LIMIT,
        TRACK_SCURVE_DIFF_RATIO,
        TRACK_SCURVE_DIFF_MIN,
        TRACK_SCURVE_SPEED_SCALE_MIN,
        TRACK_SCURVE_LOSS_SPEED_SCALE_MIN,
        TRACK_SCURVE_EXIT_CENTER_ERROR,
        TRACK_SCURVE_EXIT_CENTER_DELTA,
        TRACK_SCURVE_EXIT_CENTER_YAW_RATE,
        TRACK_SCURVE_EXIT_CONFIRM_COUNT
    },
    {
        HEADING_TRIM,
        HEADING_INTEGRAL_ZONE,
        HEADING_INTEGRAL_ATTEN,
        SPEED_ENTRY,
        SPEED_START_DEFAULT,
        SPEED_RAMP_RATE,
        SPEED_RAMP_DOWN_RATE,
        SPEED_CORE_SLEW_STEP,
        SPEED_OUTPUT_LIMIT,
        SPEED_FEEDFORWARD_GAIN,
        SPEED_NEG_INTEGRAL_ATTEN,
        SPEED_INTEGRAL_RELEASE_RATE,
        PID_DERIV_LPF_ALPHA,
        ENC_SPEED_LPF_ALPHA,
        GYRO_FAST_LPF_ALPHA
    }
};

static TuneRuntime_t s_tuneCurrent;

static const TuneParamMeta_t s_paramTable[] = {
    { "straight.speed.target",        "straight_speed",   "f32", "float",    "speed",    TUNE_PARAM_F32, TUNE_OFF(straightSpeed.target),         0.0f,   80.0f,  0.5f,  0.1f, 1.0f,0u },
    { "straight.speed.kp",            "straight_speed",   "f32", "float",    "gain",     TUNE_PARAM_F32, TUNE_OFF(straightSpeed.kp),             0.0f,   10.0f,  0.05f, 0.01f,1.0f,0u },
    { "straight.speed.ki",            "straight_speed",   "f32", "float",    "gain",     TUNE_PARAM_F32, TUNE_OFF(straightSpeed.ki),             0.0f,    2.0f,  0.005f,0.001f,1.0f,0u },
    { "straight.speed.kd",            "straight_speed",   "f32", "float",    "gain",     TUNE_PARAM_F32, TUNE_OFF(straightSpeed.kd),             0.0f,    5.0f,  0.05f, 0.01f,1.0f,0u },
    { "straight.heading.kp",          "straight_heading", "f32", "float",    "gain",     TUNE_PARAM_F32, TUNE_OFF(straightHeading.kp),           0.0f,   20.0f,  0.1f,  0.01f,1.0f,0u },
    { "straight.heading.ki",          "straight_heading", "f32", "float",    "gain",     TUNE_PARAM_F32, TUNE_OFF(straightHeading.ki),           0.0f,    1.0f,  0.005f,0.001f,1.0f,0u },
    { "straight.heading.kd",          "straight_heading", "f32", "float",    "gain",     TUNE_PARAM_F32, TUNE_OFF(straightHeading.kd),           0.0f,   12.0f,  0.05f, 0.01f,1.0f,0u },
    { "straight.heading.diff_ratio",  "straight_heading", "f32", "float",    "ratio",    TUNE_PARAM_F32, TUNE_OFF(straightHeading.diffRatio),    0.1f,    1.0f,  0.01f, 0.001f,1.0f,0u },
    { "straight.heading.diff_min",    "straight_heading", "f32", "float",    "pwm",      TUNE_PARAM_F32, TUNE_OFF(straightHeading.diffMin),      0.0f,  200.0f,  1.0f,  1.0f, 1.0f,0u },
    { "track.speed.target",           "track_speed",      "f32", "float",    "speed",    TUNE_PARAM_F32, TUNE_OFF(trackSpeed.target),            0.0f,  120.0f,  0.5f,  0.1f, 1.0f,0u },
    { "track.speed.kp",               "track_speed",      "f32", "float",    "gain",     TUNE_PARAM_F32, TUNE_OFF(trackSpeed.kp),                0.0f,   10.0f,  0.05f, 0.01f,1.0f,0u },
    { "track.speed.ki",               "track_speed",      "f32", "float",    "gain",     TUNE_PARAM_F32, TUNE_OFF(trackSpeed.ki),                0.0f,    2.0f,  0.005f,0.001f,1.0f,0u },
    { "track.speed.kd",               "track_speed",      "f32", "float",    "gain",     TUNE_PARAM_F32, TUNE_OFF(trackSpeed.kd),                0.0f,    5.0f,  0.05f, 0.01f,1.0f,0u },

    { "track.line.kp",                "track_line",       "f32", "float",    "deg",      TUNE_PARAM_F32, TUNE_OFF(trackLine.kp),                 0.0f,   16.0f,  0.05f, 0.01f,1.0f,0u },
    { "track.line.kd",                "track_line",       "f32", "float",    "deg",      TUNE_PARAM_F32, TUNE_OFF(trackLine.kd),                 0.0f,   10.0f,  0.05f, 0.01f,1.0f,0u },
    { "track.line.pos_lpf",           "track_line",       "f32", "float",    "ratio",    TUNE_PARAM_F32, TUNE_OFF(trackLine.posLpf),             0.0f,    1.0f,  0.01f, 0.001f,1.0f,0u },
    { "track.line.deriv_lpf",         "track_line",       "f32", "float",    "ratio",    TUNE_PARAM_F32, TUNE_OFF(trackLine.derivLpf),           0.0f,    1.0f,  0.01f, 0.001f,1.0f,0u },
    { "track.line.center_bias",       "track_line",       "f32", "float",    "pos",      TUNE_PARAM_F32, TUNE_OFF(trackLine.centerBias),        -1.5f,    1.5f,  0.01f, 0.001f,1.0f,0u },
    { "track.target_yaw_limit",       "track_line",       "f32", "float",    "deg",      TUNE_PARAM_F32, TUNE_OFF(trackLine.targetYawLimit),     0.0f,   45.0f,  0.5f,  0.1f, 1.0f,0u },
    { "track.authority.diff_ratio",   "track_line",       "f32", "float",    "ratio",    TUNE_PARAM_F32, TUNE_OFF(trackLine.diffRatio),          0.1f,    1.0f,  0.01f, 0.001f,1.0f,0u },
    { "track.authority.diff_min",     "track_line",       "f32", "float",    "pwm",      TUNE_PARAM_F32, TUNE_OFF(trackLine.diffMin),            0.0f,  200.0f,  1.0f,  1.0f, 1.0f,0u },
    { "track.loss.hold_ms",           "track_line",       "u32", "uint32_t", "ms",       TUNE_PARAM_U32, TUNE_OFF(trackLine.lineLossHoldMs),     0.0f, 1000.0f, 5.0f, 1.0f, 1.0f,0u },
    { "track.loss.timeout_ms",        "track_line",       "u32", "uint32_t", "ms",       TUNE_PARAM_U32, TUNE_OFF(trackLine.lineLossTimeoutMs), 50.0f, 8000.0f, 10.0f, 1.0f, 1.0f,0u },
    { "track.loss.yaw_decay",         "track_line",       "f32", "float",    "ratio",    TUNE_PARAM_F32, TUNE_OFF(trackLine.lineLossYawDecay),   0.900f,  1.000f, 0.001f,0.001f,1.0f,0u },
    { "track.loss.speed_scale",       "track_line",       "f32", "float",    "ratio",    TUNE_PARAM_F32, TUNE_OFF(trackLine.lineLossSpeedScale), 0.0f,    1.0f,  0.01f, 0.001f,1.0f,0u },
    { "track.curve.pos_start",        "track_line",       "f32", "float",    "pos",      TUNE_PARAM_F32, TUNE_OFF(trackLine.curveSpeedPosStart), 0.0f,    3.0f,  0.05f, 0.01f,1.0f,0u },
    { "track.curve.pos_full",         "track_line",       "f32", "float",    "pos",      TUNE_PARAM_F32, TUNE_OFF(trackLine.curveSpeedPosFull),  0.1f,    4.0f,  0.05f, 0.01f,1.0f,0u },
    { "track.curve.speed_scale_min",  "track_line",       "f32", "float",    "ratio",    TUNE_PARAM_F32, TUNE_OFF(trackLine.curveSpeedScaleMin), 0.05f,   1.0f,  0.01f, 0.001f,1.0f,0u },

    { "track.scurve.enter_yaw_rate",  "track_scurve",     "f32", "float",    "deg_s",    TUNE_PARAM_F32, TUNE_OFF(trackSCurve.enterYawRate),     0.0f,  200.0f,  1.0f,  0.1f, 1.0f,0u },
    { "track.scurve.enter_error",     "track_scurve",     "f32", "float",    "pos",      TUNE_PARAM_F32, TUNE_OFF(trackSCurve.enterError),       0.0f,    4.0f,  0.05f, 0.01f,1.0f,0u },
    { "track.scurve.enter_delta",     "track_scurve",     "f32", "float",    "pos",      TUNE_PARAM_F32, TUNE_OFF(trackSCurve.enterDelta),       0.0f,    4.0f,  0.05f, 0.01f,1.0f,0u },
    { "track.scurve.enter_yaw_cmd",   "track_scurve",     "f32", "float",    "deg",      TUNE_PARAM_F32, TUNE_OFF(trackSCurve.enterYawCommand),  0.0f,   45.0f,  0.5f,  0.1f, 1.0f,0u },
    { "track.scurve.exit_error",      "track_scurve",     "f32", "float",    "pos",      TUNE_PARAM_F32, TUNE_OFF(trackSCurve.exitError),        0.0f,    3.0f,  0.05f, 0.01f,1.0f,0u },
    { "track.scurve.center_zone",     "track_scurve",     "f32", "float",    "pos",      TUNE_PARAM_F32, TUNE_OFF(trackSCurve.centerZone),       0.0f,    3.0f,  0.05f, 0.01f,1.0f,0u },
    { "track.scurve.center_gain",     "track_scurve",     "f32", "float",    "ratio",    TUNE_PARAM_F32, TUNE_OFF(trackSCurve.centerGain),       0.0f,    1.5f,  0.01f, 0.001f,1.0f,0u },
    { "track.scurve.edge_gain",       "track_scurve",     "f32", "float",    "ratio",    TUNE_PARAM_F32, TUNE_OFF(trackSCurve.edgeGain),         0.5f,    3.0f,  0.01f, 0.001f,1.0f,0u },
    { "track.scurve.center_sensor_gain", "track_scurve",  "f32", "float",    "ratio",    TUNE_PARAM_F32, TUNE_OFF(trackSCurve.centerSensorGain), 0.0f,    2.0f,  0.01f, 0.001f,1.0f,0u },
    { "track.scurve.inner_sensor_gain",  "track_scurve",  "f32", "float",    "ratio",    TUNE_PARAM_F32, TUNE_OFF(trackSCurve.innerSensorGain),  0.0f,    2.5f,  0.01f, 0.001f,1.0f,0u },
    { "track.scurve.outer_sensor_gain",  "track_scurve",  "f32", "float",    "ratio",    TUNE_PARAM_F32, TUNE_OFF(trackSCurve.outerSensorGain),  0.0f,    3.0f,  0.01f, 0.001f,1.0f,0u },
    { "track.scurve.edge_sensor_gain",   "track_scurve",  "f32", "float",    "ratio",    TUNE_PARAM_F32, TUNE_OFF(trackSCurve.edgeSensorGain),   0.0f,    4.0f,  0.01f, 0.001f,1.0f,0u },
    { "track.scurve.side_pos_start",     "track_scurve",  "f32", "float",    "pos",      TUNE_PARAM_F32, TUNE_OFF(trackSCurve.sideTargetPosStart), 0.0f, 3.5f, 0.05f, 0.01f,1.0f,0u },
    { "track.scurve.side_pos_full",      "track_scurve",  "f32", "float",    "pos",      TUNE_PARAM_F32, TUNE_OFF(trackSCurve.sideTargetPosFull),  0.5f, 3.5f, 0.05f, 0.01f,1.0f,0u },
    { "track.scurve.side_target_inner",  "track_scurve",  "f32", "float",    "pos",      TUNE_PARAM_F32, TUNE_OFF(trackSCurve.sideTargetInner),   0.5f, 3.5f, 0.05f, 0.01f,1.0f,0u },
    { "track.scurve.side_target_outer",  "track_scurve",  "f32", "float",    "pos",      TUNE_PARAM_F32, TUNE_OFF(trackSCurve.sideTargetOuter),   1.0f, 3.5f, 0.05f, 0.01f,1.0f,0u },
    { "track.scurve.line_kp_scale",   "track_scurve",     "f32", "float",    "ratio",    TUNE_PARAM_F32, TUNE_OFF(trackSCurve.lineKpScale),      0.5f,    3.0f,  0.05f, 0.01f,1.0f,0u },
    { "track.scurve.yaw_limit",       "track_scurve",     "f32", "float",    "deg",      TUNE_PARAM_F32, TUNE_OFF(trackSCurve.yawLimit),         0.0f,   70.0f,  0.5f,  0.1f, 1.0f,0u },
    { "track.scurve.diff_ratio",      "track_scurve",     "f32", "float",    "ratio",    TUNE_PARAM_F32, TUNE_OFF(trackSCurve.diffRatio),        0.1f,    1.0f,  0.01f, 0.001f,1.0f,0u },
    { "track.scurve.diff_min",        "track_scurve",     "f32", "float",    "pwm",      TUNE_PARAM_F32, TUNE_OFF(trackSCurve.diffMin),          0.0f,  200.0f,  1.0f,  1.0f, 1.0f,0u },
    { "track.scurve.speed_scale_min", "track_scurve",     "f32", "float",    "ratio",    TUNE_PARAM_F32, TUNE_OFF(trackSCurve.speedScaleMin),    0.05f,   1.0f,  0.01f, 0.001f,1.0f,0u },
    { "track.scurve.loss_speed_scale_min", "track_scurve","f32", "float",    "ratio",    TUNE_PARAM_F32, TUNE_OFF(trackSCurve.lossSpeedScaleMin), 0.0f, 1.0f, 0.01f, 0.001f,1.0f,0u },
    { "track.scurve.exit_center_error",    "track_scurve","f32", "float",    "pos",      TUNE_PARAM_F32, TUNE_OFF(trackSCurve.exitCenterError),  0.0f,    3.0f,  0.05f, 0.01f,1.0f,0u },
    { "track.scurve.exit_center_delta",    "track_scurve","f32", "float",    "pos",      TUNE_PARAM_F32, TUNE_OFF(trackSCurve.exitCenterDelta),  0.0f,    2.0f,  0.05f, 0.01f,1.0f,0u },
    { "track.scurve.exit_center_yaw_rate", "track_scurve","f32", "float",    "deg_s",    TUNE_PARAM_F32, TUNE_OFF(trackSCurve.exitCenterYawRate), 0.0f, 200.0f, 1.0f, 0.1f,1.0f,0u },
    { "track.scurve.exit_confirm_count",   "track_scurve","u32", "uint32_t", "count",    TUNE_PARAM_U32, TUNE_OFF(trackSCurve.exitConfirmCount), 1.0f,   10.0f,  1.0f, 1.0f,1.0f,0u },

    { "common.heading_trim",          "common",           "f32", "float",    "deg",      TUNE_PARAM_F32, TUNE_OFF(common.headingTrim),          -10.0f,  10.0f,  0.05f, 0.01f,1.0f,0u },
    { "common.heading_integral_zone", "common",           "f32", "float",    "deg",      TUNE_PARAM_F32, TUNE_OFF(common.headingIntegralZone),   0.1f,   10.0f,  0.05f, 0.01f,1.0f,0u },
    { "common.heading_integral_atten","common",           "f32", "float",    "ratio",    TUNE_PARAM_F32, TUNE_OFF(common.headingIntegralAtten),  0.0f,    1.0f,  0.01f, 0.001f,1.0f,0u },
    { "common.speed_entry",           "common",           "f32", "float",    "speed",    TUNE_PARAM_F32, TUNE_OFF(common.speedEntry),            0.0f,   60.0f,  0.1f,  0.1f, 1.0f,0u },
    { "common.start_speed",           "common",           "f32", "float",    "speed",    TUNE_PARAM_F32, TUNE_OFF(common.startSpeed),            0.0f,  120.0f,  0.1f,  0.1f, 1.0f,0u },
    { "common.speed_ramp_rate",       "common",           "f32", "float",    "speed_s",  TUNE_PARAM_F32, TUNE_OFF(common.speedRampRate),         1.0f,  200.0f,  1.0f,  0.1f, 1.0f,0u },
    { "common.speed_ramp_down_rate",  "common",           "f32", "float",    "speed_s",  TUNE_PARAM_F32, TUNE_OFF(common.speedRampDownRate),     1.0f,  200.0f,  1.0f,  0.1f, 1.0f,0u },
    { "common.speed_core_slew_step",  "common",           "u32", "uint32_t", "pwm",      TUNE_PARAM_U32, TUNE_OFF(common.speedCoreSlewStep),     1.0f,  200.0f,  1.0f,  1.0f,1.0f,0u },
    { "common.speed_output_limit",    "common",           "f32", "float",    "pwm",      TUNE_PARAM_F32, TUNE_OFF(common.speedOutputLimit),     50.0f,  (float)MOTOR_PWM_MAX, 1.0f, 1.0f,1.0f,0u },
    { "common.speed_feedforward",     "common",           "f32", "float",    "gain",     TUNE_PARAM_F32, TUNE_OFF(common.speedFeedforwardGain),  0.0f,   20.0f,  0.1f,  0.01f,1.0f,0u },
    { "common.speed_neg_integral_atten","common",         "f32", "float",    "ratio",    TUNE_PARAM_F32, TUNE_OFF(common.speedNegIntegralAtten), 0.0f,    1.0f,  0.01f, 0.001f,1.0f,0u },
    { "common.speed_integral_release_rate","common",      "f32", "float",    "i_s",      TUNE_PARAM_F32, TUNE_OFF(common.speedIntegralReleaseRate), 0.0f, 200.0f, 1.0f, 0.1f,1.0f,0u },
    { "common.pid_deriv_lpf_alpha",   "common",           "f32", "float",    "ratio",    TUNE_PARAM_F32, TUNE_OFF(common.pidDerivLpfAlpha),      0.0f,    1.0f,  0.01f, 0.001f,1.0f,0u },
    { "common.encoder_speed_lpf",     "common",           "f32", "float",    "ratio",    TUNE_PARAM_F32, TUNE_OFF(common.encoderSpeedLpfAlpha),  0.0f,    1.0f,  0.01f, 0.001f,1.0f,0u },
    { "common.gyro_fast_lpf",         "common",           "f32", "float",    "ratio",    TUNE_PARAM_F32, TUNE_OFF(common.gyroFastLpfAlpha),      0.0f,    1.0f,  0.01f, 0.001f,1.0f,0u }
};

static const TuneParamMeta_t *find_meta(const char *key)
{
    uint16_t i;

    if (!key || !key[0])
        return 0;

    for (i = 0u; i < (uint16_t)(sizeof(s_paramTable) / sizeof(s_paramTable[0])); i++)
    {
        if (strcmp(s_paramTable[i].key, key) == 0)
            return &s_paramTable[i];
    }
    return 0;
}

static uint8_t group_is_all(const char *group)
{
    if (!group || !group[0])
        return 1u;
    if (strcmp(group, "all") == 0)
        return 1u;
    return 0u;
}

static float *meta_ptr_f32(TuneRuntime_t *runtime, const TuneParamMeta_t *meta)
{
    return (float *)((uint8_t *)runtime + meta->offset);
}

static const float *meta_ptr_f32_const(const TuneRuntime_t *runtime, const TuneParamMeta_t *meta)
{
    return (const float *)((const uint8_t *)runtime + meta->offset);
}

static uint32_t *meta_ptr_u32(TuneRuntime_t *runtime, const TuneParamMeta_t *meta)
{
    return (uint32_t *)((uint8_t *)runtime + meta->offset);
}

static const uint32_t *meta_ptr_u32_const(const TuneRuntime_t *runtime, const TuneParamMeta_t *meta)
{
    return (const uint32_t *)((const uint8_t *)runtime + meta->offset);
}

static float clampf(float v, float lo, float hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static uint32_t clamp_u32(uint32_t v, uint32_t lo, uint32_t hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static float quantize_f32(float value, const TuneParamMeta_t *meta)
{
    float step;
    float relative;
    int32_t steps;

    if (!meta)
        return value;

    value = clampf(value, meta->minValue, meta->maxValue);
    step = meta->step;
    if (step <= 0.0f)
        return value;

    relative = (value - meta->minValue) / step;
    if (relative >= 0.0f)
        steps = (int32_t)(relative + 0.5f);
    else
        steps = (int32_t)(relative - 0.5f);

    value = meta->minValue + ((float)steps * step);
    return clampf(value, meta->minValue, meta->maxValue);
}

static uint32_t quantize_u32(uint32_t value, const TuneParamMeta_t *meta)
{
    uint32_t lo;
    uint32_t hi;
    uint32_t step;

    if (!meta)
        return value;

    lo = (uint32_t)(meta->minValue + 0.5f);
    hi = (uint32_t)(meta->maxValue + 0.5f);
    value = clamp_u32(value, lo, hi);

    step = (uint32_t)(meta->step + 0.5f);
    if (step == 0u)
        return value;

    value = lo + (((value - lo) + (step / 2u)) / step) * step;
    return clamp_u32(value, lo, hi);
}

static uint8_t parse_float_text(const char *text, float *out)
{
    float value = 0.0f;
    float fracScale = 0.1f;
    uint8_t sign = 0u;
    uint8_t seenDigit = 0u;
    uint8_t seenDot = 0u;
    const char *p = text;

    if (!text || !out)
        return 0u;

    if (*p == '+')
        p++;
    else if (*p == '-')
    {
        sign = 1u;
        p++;
    }

    while (*p != '\0' && *p != '!')
    {
        if (*p >= '0' && *p <= '9')
        {
            seenDigit = 1u;
            if (!seenDot)
                value = (value * 10.0f) + (float)(*p - '0');
            else
            {
                value += (float)(*p - '0') * fracScale;
                fracScale *= 0.1f;
            }
        }
        else if (*p == '.')
        {
            if (seenDot)
                return 0u;
            seenDot = 1u;
        }
        else
        {
            return 0u;
        }
        p++;
    }

    if (!seenDigit)
        return 0u;

    *out = sign ? -value : value;
    return 1u;
}

static uint8_t parse_u32_text(const char *text, uint32_t *out)
{
    uint32_t value = 0u;
    uint8_t seenDigit = 0u;
    const char *p = text;

    if (!text || !out)
        return 0u;

    if (*p == '+')
        p++;

    while (*p != '\0' && *p != '!')
    {
        if (*p < '0' || *p > '9')
            return 0u;
        seenDigit = 1u;
        value = (value * 10u) + (uint32_t)(*p - '0');
        p++;
    }

    if (!seenDigit)
        return 0u;

    *out = value;
    return 1u;
}

static void format_meta_value(const TuneRuntime_t *runtime, const TuneParamMeta_t *meta, char *out, uint16_t outSize)
{
    if (!runtime || !meta || !out || outSize == 0u)
        return;

    if (meta->type == TUNE_PARAM_F32)
    {
        sprintf(out,
                "PAR:key=%s,group=%s,wire=%s,store=%s,min=%.3f,max=%.3f,step=%.3f,def=%.3f,cur=%.3f,unit=%s,persist=%s\r\n",
                meta->key,
                meta->group,
                meta->wireType,
                meta->storageType,
                (double)meta->minValue,
                (double)meta->maxValue,
                (double)meta->step,
                (double)(*meta_ptr_f32_const(&s_tuneDefaults, meta)),
                (double)(*meta_ptr_f32_const(runtime, meta)),
                meta->unit,
                meta->persist ? "flash" : "ram");
    }
    else
    {
        sprintf(out,
                "PAR:key=%s,group=%s,wire=%s,store=%s,min=%lu,max=%lu,step=%lu,def=%lu,cur=%lu,unit=%s,persist=%s\r\n",
                meta->key,
                meta->group,
                meta->wireType,
                meta->storageType,
                (unsigned long)(uint32_t)(meta->minValue + 0.5f),
                (unsigned long)(uint32_t)(meta->maxValue + 0.5f),
                (unsigned long)(uint32_t)(meta->step + 0.5f),
                (unsigned long)(*meta_ptr_u32_const(&s_tuneDefaults, meta)),
                (unsigned long)(*meta_ptr_u32_const(runtime, meta)),
                meta->unit,
                meta->persist ? "flash" : "ram");
    }
}

void TuneParams_Init(void)
{
    s_tuneCurrent = s_tuneDefaults;
}

const TuneRuntime_t *TuneParams_Get(void)
{
    return &s_tuneCurrent;
}

uint16_t TuneParams_LoadDefaults(const char *group)
{
    uint16_t i;
    uint16_t changed = 0u;

    if (group_is_all(group))
    {
        s_tuneCurrent = s_tuneDefaults;
        return (uint16_t)(sizeof(s_paramTable) / sizeof(s_paramTable[0]));
    }

    for (i = 0u; i < (uint16_t)(sizeof(s_paramTable) / sizeof(s_paramTable[0])); i++)
    {
        const TuneParamMeta_t *meta = &s_paramTable[i];
        if (strcmp(meta->group, group) != 0)
            continue;

        if (meta->type == TUNE_PARAM_F32)
            *meta_ptr_f32(&s_tuneCurrent, meta) = *meta_ptr_f32_const(&s_tuneDefaults, meta);
        else
            *meta_ptr_u32(&s_tuneCurrent, meta) = *meta_ptr_u32_const(&s_tuneDefaults, meta);
        changed++;
    }
    return changed;
}

uint16_t TuneParams_Count(void)
{
    return (uint16_t)(sizeof(s_paramTable) / sizeof(s_paramTable[0]));
}

uint8_t TuneParams_GroupMatches(uint16_t index, const char *group)
{
    if (index >= TuneParams_Count())
        return 0u;
    if (group_is_all(group))
        return 1u;
    return (strcmp(s_paramTable[index].group, group) == 0) ? 1u : 0u;
}

uint8_t TuneParams_FormatListLine(uint16_t index, char *out, uint16_t outSize)
{
    if (index >= TuneParams_Count() || !out || outSize == 0u)
        return 0u;

    format_meta_value(&s_tuneCurrent, &s_paramTable[index], out, outSize);
    return 1u;
}

uint8_t TuneParams_FormatValueLine(const char *key, char *out, uint16_t outSize)
{
    const TuneParamMeta_t *meta = find_meta(key);

    if (!meta || !out || outSize == 0u)
        return 0u;

    format_meta_value(&s_tuneCurrent, meta, out, outSize);
    return 1u;
}

uint8_t TuneParams_SetByText(const char *key, const char *valueText, char *out, uint16_t outSize)
{
    const TuneParamMeta_t *meta = find_meta(key);

    if (!meta || !valueText || !out || outSize == 0u)
        return 0u;

    if (meta->type == TUNE_PARAM_F32)
    {
        float value;
        if (!parse_float_text(valueText, &value))
            return 0u;
        value = quantize_f32(value, meta);
        *meta_ptr_f32(&s_tuneCurrent, meta) = value;
        sprintf(out, "OK:PSET,key=%s,applied=%.3f\r\n", meta->key, (double)value);
    }
    else
    {
        uint32_t value;
        if (!parse_u32_text(valueText, &value))
            return 0u;
        value = quantize_u32(value, meta);
        *meta_ptr_u32(&s_tuneCurrent, meta) = value;
        sprintf(out, "OK:PSET,key=%s,applied=%lu\r\n", meta->key, (unsigned long)value);
    }
    return 1u;
}

const char *TuneParams_MapLegacyKey(ControlMode_t mode, const char *legacyKey)
{
    if (!legacyKey)
        return 0;

    if (strcmp(legacyKey, "SPD") == 0)
        return (mode == MODE_TRACK) ? "track.speed.target" : "straight.speed.target";
    if (strcmp(legacyKey, "SKP") == 0)
        return (mode == MODE_TRACK) ? "track.speed.kp" : "straight.speed.kp";
    if (strcmp(legacyKey, "SKI") == 0)
        return (mode == MODE_TRACK) ? "track.speed.ki" : "straight.speed.ki";
    if (strcmp(legacyKey, "SKD") == 0)
        return (mode == MODE_TRACK) ? "track.speed.kd" : "straight.speed.kd";
    if (strcmp(legacyKey, "AKP") == 0)
        return (mode == MODE_TRACK) ? 0 : "straight.heading.kp";
    if (strcmp(legacyKey, "AKI") == 0)
        return (mode == MODE_TRACK) ? 0 : "straight.heading.ki";
    if (strcmp(legacyKey, "AKD") == 0)
        return (mode == MODE_TRACK) ? 0 : "straight.heading.kd";
    if (strcmp(legacyKey, "LKP") == 0)
        return "track.line.kp";
    if (strcmp(legacyKey, "LKD") == 0)
        return "track.line.kd";
    if (strcmp(legacyKey, "SFF") == 0)
        return "common.speed_feedforward";
    if (strcmp(legacyKey, "HTR") == 0)
        return "common.heading_trim";

    return 0;
}
