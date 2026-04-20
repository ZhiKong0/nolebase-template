/*
 * 纯两态循迹前端实现:
 * 1. 直线态只做“中线位置 -> yawCommand -> 差速权限/降速”。
 * 2. S 弯态只做“贴边观测 -> 侧边目标带 -> yawCommand -> S 弯权限/降速”。
 * 3. 不再保留找线、回中、capture/recenter、换边软接管等附加控制链。
 */
#include "line_track.h"
#include "config.h"
#include "sensor_fusion.h"
#include "tune_params.h"
#include <string.h>

LineTrack_State_t g_lineTrack;

/* 统一基础坐标:
 * 左侧为负，右侧为正，0 附近对应车体几何中心。
 * S 弯态只是重新分配各灯参与权重，不改这套底层坐标。 */
static const float s_lineBaseWeights[LINE_SENSOR_COUNT] = {
    -3.5f, -2.5f, -1.5f, -0.5f, 0.5f, 1.5f, 2.5f, 3.5f
};

static float clampf(float v, float lo, float hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static float absf(float v)
{
    return (v < 0.0f) ? -v : v;
}

static float maxf(float a, float b)
{
    return (a > b) ? a : b;
}

static float lerpf(float a, float b, float t)
{
    return a + (b - a) * clampf(t, 0.0f, 1.0f);
}

static float signf_nonzero(float v)
{
    return (v < 0.0f) ? -1.0f : 1.0f;
}

static uint8_t scurve_logic_enabled(void)
{
    return (TuneParams_Get()->trackSCurve.enabled != 0u) ? 1u : 0u;
}

static float wrap_deg(float deg)
{
    while (deg > 180.0f) deg -= 360.0f;
    while (deg < -180.0f) deg += 360.0f;
    return deg;
}

static uint8_t line_has_center_bits(uint8_t bits)
{
    return (bits & ((1u << 3) | (1u << 4))) ? 1u : 0u;
}

static uint8_t line_has_outer_bits(uint8_t bits)
{
    return (bits & ((1u << 0) | (1u << 1) | (1u << 6) | (1u << 7))) ? 1u : 0u;
}

static uint8_t line_is_scurve_exit_window(const LineSensor_Data_t *line)
{
    if (!line || !line->lineDetected)
        return 0u;
    /* S 弯退出窗只接受真正“窄且居中”的位型:
       1. 必须仍然看到中心灯，避免在全灭或纯边缘位型里误退相；
       2. 不能再带外侧灯，避免像 sb=255 这类宽位型把弯中段误判成已经回正；
       3. 传感器计数限制到 3 以内，压住大面积全亮或交叠位型导致的早退。 */
    if (!line_has_center_bits(line->bits))
        return 0u;
    if (line_has_outer_bits(line->bits))
        return 0u;
    if (line->count > 3u)
        return 0u;
    return 1u;
}

static uint8_t line_has_inner_bits(uint8_t bits)
{
    return (bits & ((1u << 2) | (1u << 5))) ? 1u : 0u;
}

static uint8_t line_is_center_turn_window(const LineSensor_Data_t *line)
{
    if (!line || !line->lineDetected)
        return 0u;
    /* 进入 S 弯前，经常会先出现“线还在中间，但车身已经连续转起来”的阶段。
       这里把窄中心位型单独识别出来，给 should_enter_scurve 一个提前切态的窗口，
       避免一直等到外侧灯亮才进弯。 */
    if (!line_has_center_bits(line->bits))
        return 0u;
    if (line_has_outer_bits(line->bits))
        return 0u;
    if (line->count > 3u)
        return 0u;
    return 1u;
}

static int8_t resolve_edge_side_sign(const LineSensor_Data_t *line, float position)
{
    const TuneRuntime_t *tune = TuneParams_Get();
    uint8_t leftOuter;
    uint8_t rightOuter;
    float centerError = position - tune->trackLine.centerBias;

    if (!line || !line->lineDetected)
        return 0;

    leftOuter = (line->bits & ((1u << 0) | (1u << 1))) ? 1u : 0u;
    rightOuter = (line->bits & ((1u << 6) | (1u << 7))) ? 1u : 0u;
    if (leftOuter && !rightOuter)
        return -1;
    if (rightOuter && !leftOuter)
        return 1;
    if (centerError <= -0.25f)
        return -1;
    if (centerError >= 0.25f)
        return 1;
    return 0;
}

static uint8_t edge_boost_logic_enabled(void)
{
    return (TuneParams_Get()->trackEdge.enabled != 0u) ? 1u : 0u;
}

static float compute_edge_release_position(float rawPosition, float filteredPosition)
{
    const TuneRuntime_t *tune = TuneParams_Get();
    float rawError = rawPosition - tune->trackLine.centerBias;
    float filteredError = filteredPosition - tune->trackLine.centerBias;

    /* 释放判定优先看“离中心更远”的那个位置:
       - rawPosition 响应快，但容易被离散灯位瞬时跳动拉回;
       - filteredPosition 更稳，能代表车身是否真的已经回到中心附近。
       这样可以避免像 exp597 那样，在过渡位型里因为瞬时 raw 回缩就提前撤掉外侧增强。 */
    if (absf(filteredError) > absf(rawError))
        return filteredPosition;
    return rawPosition;
}

static uint32_t clamp_u32_local(uint32_t v, uint32_t lo, uint32_t hi)
{
    if (v < lo)
        return lo;
    if (v > hi)
        return hi;
    return v;
}

static float compute_curve_speed_scale(float positionError)
{
    const TuneRuntime_t *tune = TuneParams_Get();
    float absPos = absf(positionError);
    float span;
    float ratio;

    if (absPos <= tune->trackLine.curveSpeedPosStart)
        return 1.0f;
    if (absPos >= tune->trackLine.curveSpeedPosFull)
        return clampf(tune->trackLine.curveSpeedScaleMin, 0.0f, 1.0f);

    span = tune->trackLine.curveSpeedPosFull - tune->trackLine.curveSpeedPosStart;
    if (span <= 0.001f)
        return clampf(tune->trackLine.curveSpeedScaleMin, 0.0f, 1.0f);

    ratio = (absPos - tune->trackLine.curveSpeedPosStart) / span;
    return clampf(1.0f + ratio * (tune->trackLine.curveSpeedScaleMin - 1.0f), 0.0f, 1.0f);
}

static uint8_t should_use_edge_boost(const LineSensor_Data_t *line)
{
    if (!edge_boost_logic_enabled())
        return 0u;
    if (!line || !line->lineDetected)
        return 0u;

    return line_has_outer_bits(line->bits);
}

static uint8_t compute_edge_boost_active(const LineSensor_Data_t *line, float basePosition)
{
    const TuneRuntime_t *tune = TuneParams_Get();
    float centerError;

    if (!edge_boost_logic_enabled())
        return 0u;
    if (!line || !line->lineDetected)
        return 0u;

    if (line_has_outer_bits(line->bits))
        return 1u;

    centerError = absf(basePosition - tune->trackLine.centerBias);
    if (!g_lineTrack.edgeLockActive)
        return 0u;

    /* 纯 PID 外侧增强链只在“已经真实命中过外侧位型”后才允许延时释放。
       这样能避免提前接管导致的早拉偏，同时在离散灯位从外侧退回内侧时
       继续保留一小段约束，把车真正拉回到靠近中心的位置。 */
    if (centerError >= tune->trackEdge.releasePos)
        return 1u;
    return 0u;
}

static float compute_edge_boost_sensor_position(const LineSensor_Data_t *line)
{
    const TuneRuntime_t *tune = TuneParams_Get();
    float sensorGains[LINE_SENSOR_COUNT];
    float weightedSum = 0.0f;
    float activeGainSum = 0.0f;
    uint8_t i;

    if (!line || !line->lineDetected)
        return tune->trackLine.centerBias;

    sensorGains[0] = tune->trackEdge.edgeSensorGain;   /* S1 */
    sensorGains[1] = tune->trackEdge.outerSensorGain;  /* S2 */
    sensorGains[2] = tune->trackEdge.innerSensorGain;  /* S3 */
    sensorGains[3] = 0.0f;                             /* S4 */
    sensorGains[4] = 0.0f;                             /* S5 */
    sensorGains[5] = tune->trackEdge.innerSensorGain;  /* S6 */
    sensorGains[6] = tune->trackEdge.outerSensorGain;  /* S7 */
    sensorGains[7] = tune->trackEdge.edgeSensorGain;   /* S8 */

    for (i = 0u; i < LINE_SENSOR_COUNT; i++)
    {
        if (!(line->bits & (1u << i)))
            continue;
        if (sensorGains[i] <= 0.0001f)
            continue;
        weightedSum += s_lineBaseWeights[i] * sensorGains[i];
        activeGainSum += sensorGains[i];
    }

    if (activeGainSum <= 0.0001f)
        return line->position;

    return weightedSum / activeGainSum;
}

static float compute_edge_target_line_position(const LineSensor_Data_t *line, float position)
{
    const TuneRuntime_t *tune = TuneParams_Get();
    int8_t sideSign = resolve_edge_side_sign(line, position);

    if (sideSign == 0)
        return tune->trackLine.centerBias;
    return tune->trackLine.centerBias + ((float)sideSign * tune->trackEdge.targetPos);
}

static float compute_pid_only_sensor_position(const LineSensor_Data_t *line)
{
    return line ? line->position : TuneParams_Get()->trackLine.centerBias;
}

static float compute_scurve_sensor_position(const LineSensor_Data_t *line)
{
    const TuneRuntime_t *tune = TuneParams_Get();
    float sensorGains[LINE_SENSOR_COUNT];
    float weightedSum = 0.0f;
    float activeGainSum = 0.0f;
    uint8_t i;

    if (!line || !line->lineDetected)
        return tune->trackLine.centerBias;

    sensorGains[0] = tune->trackSCurve.edgeSensorGain;   /* S1 */
    sensorGains[1] = tune->trackSCurve.outerSensorGain;  /* S2 */
    sensorGains[2] = tune->trackSCurve.innerSensorGain;  /* S3 */
    sensorGains[3] = tune->trackSCurve.centerSensorGain; /* S4 */
    sensorGains[4] = tune->trackSCurve.centerSensorGain; /* S5 */
    sensorGains[5] = tune->trackSCurve.innerSensorGain;  /* S6 */
    sensorGains[6] = tune->trackSCurve.outerSensorGain;  /* S7 */
    sensorGains[7] = tune->trackSCurve.edgeSensorGain;   /* S8 */

    /* S 弯态的观测面明确遵循:
     * 1. S4/S5 退出约束；
     * 2. S3/S6 只保留很弱参与；
     * 3. S2/S7 成为主导约束层；
     * 4. S1/S8 权重最大，负责把车钳在弯道边缘内。 */
    for (i = 0u; i < LINE_SENSOR_COUNT; i++)
    {
        if (!(line->bits & (1u << i)))
            continue;
        weightedSum += s_lineBaseWeights[i] * sensorGains[i];
        activeGainSum += sensorGains[i];
    }

    if (activeGainSum <= 0.0001f)
        return tune->trackLine.centerBias;

    return weightedSum / activeGainSum;
}

static int8_t resolve_scurve_side_sign(int8_t prevSign, float centerError, uint8_t sensorBits)
{
    const TuneRuntime_t *tune = TuneParams_Get();

    if (centerError >= tune->trackSCurve.centerZone)
        return 1;
    if (centerError <= -tune->trackSCurve.centerZone)
        return -1;

    if ((sensorBits & ((1u << 0) | (1u << 1) | (1u << 2))) &&
        !(sensorBits & ((1u << 5) | (1u << 6) | (1u << 7))))
        return -1;
    if ((sensorBits & ((1u << 5) | (1u << 6) | (1u << 7))) &&
        !(sensorBits & ((1u << 0) | (1u << 1) | (1u << 2))))
        return 1;

    if (prevSign != 0)
        return prevSign;
    return (centerError < 0.0f) ? -1 : 1;
}

static float compute_scurve_target_line_position(const LineSensor_Data_t *line,
                                                 float linePosition,
                                                 int8_t sideSign)
{
    const TuneRuntime_t *tune = TuneParams_Get();
    float absPos = absf(linePosition);
    float ratio;
    float span;
    float targetAbs;

    /* S 弯态不再把线往中心拉，而是把线压到一条侧边目标带:
     * - 偏移还不大时，先把目标放在 S2/S7 一带；
     * - 偏移已经很大时，再把目标推向 S1/S8。 */
    if (line && line->lineDetected && line_has_outer_bits(line->bits) && (line->count <= 2u))
    {
        /* 单灯/双灯压到外侧时，优先先追内带，把车头从边缘往回拉。
         * 如果这里直接把目标放到 outer 带，tp 会跟 lp 一起跑到太外，
         * pe 会变得过小，首段就缺少真正“回正”的约束力。 */
        targetAbs = tune->trackSCurve.sideTargetInner;
    }
    else if (absPos <= tune->trackSCurve.sideTargetPosStart)
        targetAbs = tune->trackSCurve.sideTargetInner;
    else if (absPos >= tune->trackSCurve.sideTargetPosFull)
        targetAbs = tune->trackSCurve.sideTargetOuter;
    else
    {
        span = tune->trackSCurve.sideTargetPosFull - tune->trackSCurve.sideTargetPosStart;
        if (span <= 0.001f)
            targetAbs = tune->trackSCurve.sideTargetOuter;
        else
        {
            ratio = (absPos - tune->trackSCurve.sideTargetPosStart) / span;
            targetAbs = lerpf(tune->trackSCurve.sideTargetInner,
                              tune->trackSCurve.sideTargetOuter,
                              ratio);
        }
    }

    return (float)sideSign * targetAbs;
}

static float compute_scurve_inner_target_line_position(int8_t sideSign)
{
    return (float)sideSign * TuneParams_Get()->trackSCurve.sideTargetInner;
}

static float compute_scurve_entry_target_line_position(int8_t sideSign)
{
    return (float)sideSign * TuneParams_Get()->trackSCurve.sideTargetEntry;
}

static float compute_scurve_locked_target_line_position(int8_t sideSign)
{
    return (float)sideSign * TuneParams_Get()->trackSCurve.sideTargetLock;
}

static float shape_scurve_error(float error)
{
    const TuneRuntime_t *tune = TuneParams_Get();
    float absErr = absf(error);
    float centerZone = tune->trackSCurve.centerZone;
    float shapedAbs;

    if (absErr <= centerZone)
        shapedAbs = absErr * tune->trackSCurve.centerGain;
    else
        shapedAbs = (centerZone * tune->trackSCurve.centerGain)
                  + ((absErr - centerZone) * tune->trackSCurve.edgeGain);

    return signf_nonzero(error) * shapedAbs;
}

typedef enum
{
    SCURVE_ENTER_NONE = 0u,
    SCURVE_ENTER_CONFIRM = 1u,
    SCURVE_ENTER_URGENT = 2u,
} SCurveEnterDecision_t;

static SCurveEnterDecision_t should_enter_scurve(const LineSensor_Data_t *line,
                                                 uint32_t elapsedMs,
                                                 float centerError,
                                                 float filteredDelta,
                                                 float yawRate,
                                                 float prevYawCommand)
{
    const TuneRuntime_t *tune = TuneParams_Get();
    uint8_t strongSide;
    uint8_t dynamicEvidence;
    uint8_t urgentOuterHit;
    uint8_t severeSideError;
    uint8_t centerTurnWindow;
    uint8_t guidedTurnEvidence;
    float urgentErrorThreshold;
    float guidedYawRateThreshold;
    float guidedYawCmdThreshold;

    if (!scurve_logic_enabled())
        return SCURVE_ENTER_NONE;
    if (!line->lineDetected)
        return SCURVE_ENTER_NONE;
    if (elapsedMs < tune->trackSCurve.enterGraceMs)
        return SCURVE_ENTER_NONE;

    /* 简化 S 弯进入逻辑:
     * 1. 必须先看到明显的侧向证据，避免起步轻微歪头就切态；
     * 2. 再叠加姿态/误差变化证据，确保真的是在进弯而不是静态偏到边线。 */
    strongSide = ((line_has_outer_bits(line->bits) && (line->count <= 3u))
                || (absf(centerError) >= tune->trackSCurve.enterError)) ? 1u : 0u;
    dynamicEvidence = ((absf(filteredDelta) >= tune->trackSCurve.enterDelta)
                     || (absf(yawRate) >= tune->trackSCurve.enterYawRate)
                     || (absf(prevYawCommand) >= tune->trackSCurve.enterYawCommand)) ? 1u : 0u;
    urgentOuterHit = (line_has_outer_bits(line->bits) && (line->count <= 2u)) ? 1u : 0u;
    /* 除了单灯/双灯外，再补一条“重度侧偏直接进弯”:
       当基础 8 路位置已经偏到 S2/S7 之外时，说明车身事实上已经压进弯里。
       这时若还继续等动态证据，很容易出现 exp624 那种 `first_sc == first_ld0`。 */
    urgentErrorThreshold = maxf(tune->trackSCurve.enterError + 0.45f, 1.35f);
    severeSideError = (absf(centerError) >= urgentErrorThreshold) ? 1u : 0u;
    centerTurnWindow = line_is_center_turn_window(line);
    guidedYawRateThreshold = maxf(tune->trackSCurve.enterYawRate * 0.6f, 10.0f);
    guidedYawCmdThreshold = maxf(tune->trackSCurve.enterYawCommand * 0.8f, 8.0f);
    guidedTurnEvidence = (centerTurnWindow
                       && (absf(yawRate) >= guidedYawRateThreshold)
                       && (absf(prevYawCommand) >= guidedYawCmdThreshold)) ? 1u : 0u;

    /* 外侧单灯/双灯已经命中，或者基础位置已经出现重度侧偏时，直接按 urgent 进入:
     * 这类帧本身就说明车头已经压到 S 弯边缘，继续再等姿态/导数证据，
     * 很容易演变成“first_sc 和 first_ld0 同拍”。
     * graceMs 仍然保留，专门挡掉起步初期轻微歪头的误触发。 */
    if (urgentOuterHit || severeSideError)
        return SCURVE_ENTER_URGENT;
    if (guidedTurnEvidence)
        return SCURVE_ENTER_CONFIRM;

    return (strongSide && dynamicEvidence) ? SCURVE_ENTER_CONFIRM : SCURVE_ENTER_NONE;
}

static uint8_t should_exit_scurve(const LineSensor_Data_t *line,
                                  float centerError,
                                  float filteredDelta,
                                  float yawRate)
{
    const TuneRuntime_t *tune = TuneParams_Get();

    if (!line_is_scurve_exit_window(line))
        return 0u;
    if (absf(centerError) > tune->trackSCurve.exitCenterError)
        return 0u;
    if (absf(filteredDelta) > tune->trackSCurve.exitCenterDelta)
        return 0u;
    if (absf(yawRate) > tune->trackSCurve.exitCenterYawRate)
        return 0u;
    return 1u;
}

static void update_scurve_state(const LineSensor_Data_t *line,
                                uint32_t tickMs,
                                float centerError,
                                float filteredDelta,
                                float yawRate)
{
    const TuneRuntime_t *tune = TuneParams_Get();

    if (!scurve_logic_enabled())
    {
        g_lineTrack.sCurveActive = 0u;
        g_lineTrack.sCurveEnterConfirmCount = 0u;
        g_lineTrack.sCurveExitConfirmCount = 0u;
        g_lineTrack.sCurveEntryBoostCount = 0u;
        g_lineTrack.sCurveSideSign = 0;
        return;
    }

    if (!g_lineTrack.sCurveActive)
    {
        SCurveEnterDecision_t enterDecision;

        enterDecision = should_enter_scurve(line,
                                            tickMs - g_lineTrack.startTick,
                                            centerError,
                                            filteredDelta,
                                            yawRate,
                                            g_lineTrack.yawCommand);

        if (enterDecision != SCURVE_ENTER_NONE)
        {
            if (enterDecision == SCURVE_ENTER_URGENT)
            {
                g_lineTrack.sCurveEnterConfirmCount = tune->trackSCurve.enterConfirmCount;
            }
            else if (g_lineTrack.sCurveEnterConfirmCount < tune->trackSCurve.enterConfirmCount)
            {
                g_lineTrack.sCurveEnterConfirmCount++;
            }

            if (g_lineTrack.sCurveEnterConfirmCount >= tune->trackSCurve.enterConfirmCount)
            {
                g_lineTrack.sCurveActive = 1u;
                g_lineTrack.sCurveExitConfirmCount = 0u;
                g_lineTrack.sCurveEntryBoostCount =
                    (uint8_t)clamp_u32_local(tune->trackSCurve.entryBoostFrames, 0u, 255u);
                g_lineTrack.sCurveSideSign = resolve_scurve_side_sign(g_lineTrack.sCurveSideSign,
                                                                      centerError,
                                                                      line->bits);
            }
        }
        else
        {
            g_lineTrack.sCurveEnterConfirmCount = 0u;
        }
        return;
    }

    g_lineTrack.sCurveSideSign = resolve_scurve_side_sign(g_lineTrack.sCurveSideSign,
                                                          centerError,
                                                          line->bits);

    if (should_exit_scurve(line, centerError, filteredDelta, yawRate))
    {
        if (g_lineTrack.sCurveExitConfirmCount < tune->trackSCurve.exitConfirmCount)
            g_lineTrack.sCurveExitConfirmCount++;
        if (g_lineTrack.sCurveExitConfirmCount >= tune->trackSCurve.exitConfirmCount)
        {
            g_lineTrack.sCurveActive = 0u;
            g_lineTrack.sCurveEnterConfirmCount = 0u;
            g_lineTrack.sCurveExitConfirmCount = 0u;
            g_lineTrack.sCurveEntryBoostCount = 0u;
            g_lineTrack.sCurveSideSign = 0;
        }
    }
    else
    {
        g_lineTrack.sCurveExitConfirmCount = 0u;
    }
}

static void update_runtime_limits(uint8_t sCurveActive,
                                  uint8_t edgeLockActive,
                                  uint8_t entryBoostActive)
{
    const TuneRuntime_t *tune = TuneParams_Get();

    if (sCurveActive && entryBoostActive)
    {
        g_lineTrack.activeLineKpScale = tune->trackSCurve.entryLineKpScale;
        g_lineTrack.activeYawLimit = tune->trackSCurve.entryYawLimit;
        g_lineTrack.headingDiffRatio = tune->trackSCurve.entryDiffRatio;
        g_lineTrack.headingDiffMin = tune->trackSCurve.entryDiffMin;
    }
    else if (sCurveActive && edgeLockActive)
    {
        g_lineTrack.activeLineKpScale = tune->trackSCurve.lockLineKpScale;
        g_lineTrack.activeYawLimit = tune->trackSCurve.lockYawLimit;
        g_lineTrack.headingDiffRatio = tune->trackSCurve.lockDiffRatio;
        g_lineTrack.headingDiffMin = tune->trackSCurve.lockDiffMin;
    }
    else if (sCurveActive)
    {
        g_lineTrack.activeLineKpScale = tune->trackSCurve.lineKpScale;
        g_lineTrack.activeYawLimit = tune->trackSCurve.yawLimit;
        g_lineTrack.headingDiffRatio = tune->trackSCurve.diffRatio;
        g_lineTrack.headingDiffMin = tune->trackSCurve.diffMin;
    }
    else if (edgeLockActive)
    {
        /* 纯 PID 模式下，外侧增强一旦命中就直接给全量权限，
           不再经过 blend 稀释。这样 tune 里的 edge 参数就是最终生效值。 */
        g_lineTrack.activeLineKpScale = tune->trackEdge.lineKpScale;
        g_lineTrack.activeYawLimit = tune->trackEdge.yawLimit;
        g_lineTrack.headingDiffRatio = tune->trackEdge.diffRatio;
        g_lineTrack.headingDiffMin = tune->trackEdge.diffMin;
    }
    else
    {
        g_lineTrack.activeLineKpScale = 1.0f;
        g_lineTrack.activeYawLimit = tune->trackLine.targetYawLimit;
        g_lineTrack.headingDiffRatio = tune->trackLine.diffRatio;
        g_lineTrack.headingDiffMin = tune->trackLine.diffMin;
    }
}

static uint8_t compute_edge_lock_active(const LineSensor_Data_t *line, float linePosition)
{
    const TuneRuntime_t *tune = TuneParams_Get();
    float centerError = linePosition - tune->trackLine.centerBias;

    if (!scurve_logic_enabled())
        return 0u;
    if (!g_lineTrack.sCurveActive || !line->lineDetected)
        return 0u;
    if (line->count <= 2u)
        return 1u;
    if (line_has_outer_bits(line->bits))
        return 1u;
    if (absf(centerError) >= tune->trackSCurve.sideTargetPosStart)
        return 1u;
    return 0u;
}

static void fill_snapshot(float currentYaw,
                          float yawRate,
                          LineTrack_Snapshot_t *out)
{
    const TuneRuntime_t *tune = TuneParams_Get();
    LineSensor_Data_t line;
    float observedPosition;
    float centerError;
    uint8_t sCurveActive;
    int8_t sideSign = 0;
    float targetLinePosition;
    float positionError;
    float effectiveError;
    float yawCommand;

    memset(out, 0, sizeof(*out));
    LineSensor_Read(&line);

    out->sensorBits = line.bits;
    out->sensorCount = line.count;
    out->lineDetected = line.lineDetected;

    if (!line.lineDetected)
    {
        out->linePosition = tune->trackLine.centerBias;
        out->targetLinePosition = tune->trackLine.centerBias;
        out->speedScale = tune->trackLine.lineLossSpeedScale;
        out->headingDiffRatio = tune->trackLine.diffRatio;
        out->headingDiffMin = tune->trackLine.diffMin;
        out->lineKpScale = 1.0f;
        out->yawLimit = tune->trackLine.targetYawLimit;
        out->targetYaw = currentYaw;
        return;
    }

    centerError = line.position - tune->trackLine.centerBias;
    sCurveActive = 0u;
    out->edgeLockActive = 0u;
    if (sCurveActive)
        observedPosition = compute_scurve_sensor_position(&line);
    else
    {
        if (scurve_logic_enabled())
            observedPosition = compute_pid_only_sensor_position(&line);
        else
        {
            out->edgeLockActive = should_use_edge_boost(&line);
            observedPosition = out->edgeLockActive ? compute_edge_boost_sensor_position(&line)
                                                   : compute_pid_only_sensor_position(&line);
        }
    }

    if (sCurveActive)
    {
        sideSign = resolve_scurve_side_sign(0, observedPosition - tune->trackLine.centerBias, line.bits);
        out->edgeLockActive = compute_edge_lock_active(&line, observedPosition);
        if (out->edgeLockActive)
        {
            targetLinePosition = compute_scurve_locked_target_line_position(sideSign);
            out->lineKpScale = tune->trackSCurve.lockLineKpScale;
            out->yawLimit = tune->trackSCurve.lockYawLimit;
            out->headingDiffRatio = tune->trackSCurve.lockDiffRatio;
            out->headingDiffMin = tune->trackSCurve.lockDiffMin;
        }
        else
        {
            targetLinePosition = compute_scurve_target_line_position(&line, observedPosition, sideSign);
            out->lineKpScale = tune->trackSCurve.lineKpScale;
            out->yawLimit = tune->trackSCurve.yawLimit;
            out->headingDiffRatio = tune->trackSCurve.diffRatio;
            out->headingDiffMin = tune->trackSCurve.diffMin;
        }
    }
    else
    {
        if (out->edgeLockActive)
        {
            targetLinePosition = compute_edge_target_line_position(&line, observedPosition);
            out->lineKpScale = tune->trackEdge.lineKpScale;
            out->yawLimit = tune->trackEdge.yawLimit;
            out->headingDiffRatio = tune->trackEdge.diffRatio;
            out->headingDiffMin = tune->trackEdge.diffMin;
        }
        else
        {
            targetLinePosition = tune->trackLine.centerBias;
            out->lineKpScale = 1.0f;
            out->yawLimit = tune->trackLine.targetYawLimit;
            out->headingDiffRatio = tune->trackLine.diffRatio;
            out->headingDiffMin = tune->trackLine.diffMin;
        }
    }

    positionError = observedPosition - targetLinePosition;
    effectiveError = sCurveActive ? shape_scurve_error(positionError) : positionError;
    yawCommand = (tune->trackLine.kp * out->lineKpScale) * effectiveError;
    yawCommand = clampf(yawCommand, -out->yawLimit, out->yawLimit);

    out->sCurveActive = sCurveActive;
    out->linePosition = observedPosition;
    out->targetLinePosition = targetLinePosition;
    out->positionError = positionError;
    out->effectiveError = effectiveError;
    out->yawCommand = yawCommand;
    out->targetYaw = wrap_deg(currentYaw + yawCommand);
    out->speedScale = compute_curve_speed_scale(positionError);
    if (sCurveActive && out->speedScale < tune->trackSCurve.speedScaleMin)
        out->speedScale = tune->trackSCurve.speedScaleMin;
}

void LineTrack_CollectIdleSnapshot(float currentYaw, float yawRate, LineTrack_Snapshot_t *out)
{
    if (!out)
        return;
    fill_snapshot(currentYaw, yawRate, out);
}

void LineTrack_RefreshTune(void)
{
    const TuneRuntime_t *tune = TuneParams_Get();

    g_lineTrack.kp = tune->trackLine.kp;
    g_lineTrack.kd = tune->trackLine.kd;
    update_runtime_limits(g_lineTrack.sCurveActive,
                          g_lineTrack.edgeLockActive,
                          (g_lineTrack.sCurveEntryBoostCount > 0u) ? 1u : 0u);
}

void LineTrack_Init(void)
{
    memset(&g_lineTrack, 0, sizeof(g_lineTrack));
    g_lineTrack.activeLineKpScale = 1.0f;
    g_lineTrack.activeYawLimit = TRACK_TARGET_YAW_LIMIT;
    g_lineTrack.headingDiffRatio = TRACK_HEADING_DIFF_RATIO;
    g_lineTrack.headingDiffMin = TRACK_HEADING_DIFF_MIN;
    LineTrack_RefreshTune();
}

void LineTrack_Start(uint32_t tickMs, float currentYaw)
{
    LineSensor_Data_t line;
    const TuneRuntime_t *tune = TuneParams_Get();

    memset(&g_lineTrack, 0, sizeof(g_lineTrack));
    g_lineTrack.state = LT_STATE_RUNNING;
    g_lineTrack.startTick = tickMs;
    g_lineTrack.lastSeenTick = tickMs;
    g_lineTrack.targetYaw = currentYaw;
    g_lineTrack.targetLinePosition = tune->trackLine.centerBias;
    g_lineTrack.speedScale = 1.0f;
    g_lineTrack.activeLineKpScale = 1.0f;
    g_lineTrack.activeYawLimit = tune->trackLine.targetYawLimit;
    g_lineTrack.headingDiffRatio = tune->trackLine.diffRatio;
    g_lineTrack.headingDiffMin = tune->trackLine.diffMin;
    LineTrack_RefreshTune();

    LineSensor_Read(&line);
    g_lineTrack.sensorBits = line.bits;
    g_lineTrack.sensorCount = line.count;
    g_lineTrack.lineDetected = line.lineDetected;
    g_lineTrack.sCurveActive = 0u;
    g_lineTrack.sCurveEnterConfirmCount = 0u;
    g_lineTrack.sCurveExitConfirmCount = 0u;
    g_lineTrack.sCurveSideSign = 0;
    if (line.lineDetected)
    {
        g_lineTrack.rawPosition = line.position;
        g_lineTrack.filteredPosition = line.position;
    }
    else
    {
        g_lineTrack.rawPosition = tune->trackLine.centerBias;
        g_lineTrack.filteredPosition = tune->trackLine.centerBias;
    }
}

void LineTrack_Stop(void)
{
    memset(&g_lineTrack, 0, sizeof(g_lineTrack));
    g_lineTrack.activeLineKpScale = 1.0f;
    g_lineTrack.activeYawLimit = TRACK_TARGET_YAW_LIMIT;
    g_lineTrack.headingDiffRatio = TRACK_HEADING_DIFF_RATIO;
    g_lineTrack.headingDiffMin = TRACK_HEADING_DIFF_MIN;
    LineTrack_RefreshTune();
}

void LineTrack_Update(uint32_t tickMs, float currentYaw, float yawRate)
{
    const TuneRuntime_t *tune = TuneParams_Get();
    LineSensor_Data_t line;
    float prevFiltered;
    float delta;
    float observedPosition;
    float baseCenterError;
    float targetLinePosition;
    float positionError;
    float effectiveError;
    float yawCommand;
    uint32_t unseenMs;

    if (g_lineTrack.state != LT_STATE_RUNNING)
        return;

    LineSensor_Read(&line);
    g_lineTrack.sensorBits = line.bits;
    g_lineTrack.sensorCount = line.count;
    g_lineTrack.lineDetected = line.lineDetected;

    if (!line.lineDetected)
    {
        unseenMs = tickMs - g_lineTrack.lastSeenTick;
        if (unseenMs >= tune->trackLine.lineLossTimeoutMs)
        {
            LineTrack_Stop();
            return;
        }

        if (!(g_lineTrack.edgeLockActive
              && edge_boost_logic_enabled()
              && !scurve_logic_enabled()
              && (unseenMs <= tune->trackEdge.lossHoldMs)))
        {
            g_lineTrack.edgeLockActive = 0u;
        }

        update_runtime_limits(g_lineTrack.sCurveActive,
                              g_lineTrack.edgeLockActive,
                              (g_lineTrack.sCurveEntryBoostCount > 0u) ? 1u : 0u);

        if (unseenMs > tune->trackLine.lineLossHoldMs)
        {
            g_lineTrack.yawCommand *= tune->trackLine.lineLossYawDecay;
            g_lineTrack.speedScale = tune->trackLine.lineLossSpeedScale;
            if (g_lineTrack.sCurveActive)
                g_lineTrack.speedScale = maxf(g_lineTrack.speedScale,
                                              tune->trackSCurve.lossSpeedScaleMin);
        }
        g_lineTrack.targetYaw = wrap_deg(currentYaw + g_lineTrack.yawCommand);
        return;
    }

    g_lineTrack.lastSeenTick = tickMs;

    prevFiltered = g_lineTrack.filteredPosition;
    baseCenterError = line.position - tune->trackLine.centerBias;
    update_scurve_state(&line, tickMs, baseCenterError, g_lineTrack.filteredDelta, yawRate);

    if (g_lineTrack.sCurveActive)
        observedPosition = compute_scurve_sensor_position(&line);
    else
    {
        if (scurve_logic_enabled())
        {
            g_lineTrack.edgeLockActive = 0u;
            observedPosition = compute_pid_only_sensor_position(&line);
        }
        else
        {
            g_lineTrack.edgeLockActive = compute_edge_boost_active(&line,
                                                                   compute_edge_release_position(line.position,
                                                                                                 prevFiltered));
            observedPosition = g_lineTrack.edgeLockActive ? compute_edge_boost_sensor_position(&line)
                                                          : compute_pid_only_sensor_position(&line);
        }
    }

    g_lineTrack.rawPosition = observedPosition;
    g_lineTrack.filteredPosition += tune->trackLine.posLpf * (observedPosition - g_lineTrack.filteredPosition);

    delta = g_lineTrack.filteredPosition - prevFiltered;
    g_lineTrack.filteredDelta += tune->trackLine.derivLpf * (delta - g_lineTrack.filteredDelta);

    if (g_lineTrack.sCurveActive)
    {
        uint8_t entryBoostActive = (g_lineTrack.sCurveEntryBoostCount > 0u) ? 1u : 0u;
        g_lineTrack.edgeLockActive = compute_edge_lock_active(&line, g_lineTrack.filteredPosition);
        update_runtime_limits(g_lineTrack.sCurveActive,
                              g_lineTrack.edgeLockActive,
                              entryBoostActive);
        g_lineTrack.sCurveSideSign = resolve_scurve_side_sign(g_lineTrack.sCurveSideSign,
                                                              g_lineTrack.filteredPosition - tune->trackLine.centerBias,
                                                              line.bits);
        if (entryBoostActive)
            targetLinePosition = compute_scurve_entry_target_line_position(g_lineTrack.sCurveSideSign);
        else if (g_lineTrack.edgeLockActive)
            targetLinePosition = compute_scurve_locked_target_line_position(g_lineTrack.sCurveSideSign);
        else
            targetLinePosition = compute_scurve_target_line_position(&line,
                                                                     g_lineTrack.filteredPosition,
                                                                     g_lineTrack.sCurveSideSign);
        if (g_lineTrack.sCurveEntryBoostCount > 0u)
            g_lineTrack.sCurveEntryBoostCount--;
    }
    else
    {
        update_runtime_limits(g_lineTrack.sCurveActive, g_lineTrack.edgeLockActive, 0u);
        if (g_lineTrack.edgeLockActive)
            targetLinePosition = compute_edge_target_line_position(&line, g_lineTrack.filteredPosition);
        else
            targetLinePosition = tune->trackLine.centerBias;
    }
    g_lineTrack.targetLinePosition = targetLinePosition;
    positionError = g_lineTrack.filteredPosition - targetLinePosition;
    effectiveError = g_lineTrack.sCurveActive ? shape_scurve_error(positionError) : positionError;

    g_lineTrack.positionError = positionError;
    g_lineTrack.effectiveError = effectiveError;

    yawCommand = (g_lineTrack.kp * g_lineTrack.activeLineKpScale) * effectiveError
               + g_lineTrack.kd * g_lineTrack.filteredDelta;
    yawCommand = clampf(yawCommand, -g_lineTrack.activeYawLimit, g_lineTrack.activeYawLimit);
    g_lineTrack.yawCommand = yawCommand;
    g_lineTrack.targetYaw = wrap_deg(currentYaw + yawCommand);

    g_lineTrack.speedScale = compute_curve_speed_scale(positionError);
    if (g_lineTrack.sCurveActive && g_lineTrack.speedScale < tune->trackSCurve.speedScaleMin)
        g_lineTrack.speedScale = tune->trackSCurve.speedScaleMin;
    else if (g_lineTrack.edgeLockActive)
    {
        if (g_lineTrack.speedScale < tune->trackEdge.speedScaleMin)
            g_lineTrack.speedScale = tune->trackEdge.speedScaleMin;
    }
}

uint8_t LineTrack_IsRunning(void)
{
    return (g_lineTrack.state == LT_STATE_RUNNING) ? 1u : 0u;
}

uint8_t LineTrack_IsSCurveActive(void)
{
    return g_lineTrack.sCurveActive;
}

uint8_t LineTrack_IsSpeedConstraintActive(void)
{
    return (g_lineTrack.speedScale < 0.999f) ? 1u : 0u;
}

void LineTrack_SetPID(float kp, float kd)
{
    g_lineTrack.kp = kp;
    g_lineTrack.kd = kd;
}

float LineTrack_GetTargetYaw(void)
{
    return g_lineTrack.targetYaw;
}

float LineTrack_GetYawCommand(void)
{
    return g_lineTrack.yawCommand;
}

float LineTrack_GetTargetLinePosition(void)
{
    return g_lineTrack.targetLinePosition;
}

float LineTrack_GetSpeedScale(void)
{
    return g_lineTrack.speedScale;
}

float LineTrack_GetHeadingDiffRatio(void)
{
    return g_lineTrack.headingDiffRatio;
}

float LineTrack_GetHeadingDiffMin(void)
{
    return g_lineTrack.headingDiffMin;
}

float LineTrack_GetLineKpScale(void)
{
    return g_lineTrack.activeLineKpScale;
}

float LineTrack_GetYawLimit(void)
{
    return g_lineTrack.activeYawLimit;
}
