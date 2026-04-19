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

static float compute_scurve_target_line_position(float linePosition, int8_t sideSign)
{
    const TuneRuntime_t *tune = TuneParams_Get();
    float absPos = absf(linePosition);
    float ratio;
    float span;
    float targetAbs;

    /* S 弯态不再把线往中心拉，而是把线压到一条侧边目标带:
     * - 偏移还不大时，先把目标放在 S2/S7 一带；
     * - 偏移已经很大时，再把目标推向 S1/S8。 */
    if (absPos <= tune->trackSCurve.sideTargetPosStart)
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

static uint8_t should_enter_scurve(const LineSensor_Data_t *line,
                                   float centerError,
                                   float filteredDelta,
                                   float yawRate,
                                   float prevYawCommand)
{
    const TuneRuntime_t *tune = TuneParams_Get();

    if (!line->lineDetected)
        return 0u;

    /* 进入 S 弯只保留和“弯道已经开始显著出现”直接相关的证据，
     * 不再附带找线或回中链路自己的副作用条件。 */
    if (line_has_outer_bits(line->bits) && line->count <= 3u)
        return 1u;
    if (absf(centerError) >= tune->trackSCurve.enterError)
        return 1u;
    if (absf(filteredDelta) >= tune->trackSCurve.enterDelta)
        return 1u;
    if (absf(yawRate) >= tune->trackSCurve.enterYawRate)
        return 1u;
    if (absf(prevYawCommand) >= tune->trackSCurve.enterYawCommand)
        return 1u;
    return 0u;
}

static uint8_t should_exit_scurve(const LineSensor_Data_t *line,
                                  float centerError,
                                  float filteredDelta,
                                  float yawRate)
{
    const TuneRuntime_t *tune = TuneParams_Get();

    if (!line->lineDetected)
        return 0u;
    if (!line_has_center_bits(line->bits))
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
                                float centerError,
                                float filteredDelta,
                                float yawRate)
{
    const TuneRuntime_t *tune = TuneParams_Get();

    if (!g_lineTrack.sCurveActive)
    {
        if (should_enter_scurve(line, centerError, filteredDelta, yawRate, g_lineTrack.yawCommand))
        {
            if (g_lineTrack.sCurveEnterConfirmCount < 2u)
                g_lineTrack.sCurveEnterConfirmCount++;
            if (g_lineTrack.sCurveEnterConfirmCount >= 2u)
            {
                g_lineTrack.sCurveActive = 1u;
                g_lineTrack.sCurveExitConfirmCount = 0u;
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
            g_lineTrack.sCurveSideSign = 0;
        }
    }
    else
    {
        g_lineTrack.sCurveExitConfirmCount = 0u;
    }
}

static void update_runtime_limits(uint8_t sCurveActive)
{
    const TuneRuntime_t *tune = TuneParams_Get();

    if (sCurveActive)
    {
        g_lineTrack.activeLineKpScale = tune->trackSCurve.lineKpScale;
        g_lineTrack.activeYawLimit = tune->trackSCurve.yawLimit;
        g_lineTrack.headingDiffRatio = tune->trackSCurve.diffRatio;
        g_lineTrack.headingDiffMin = tune->trackSCurve.diffMin;
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
    sCurveActive = should_enter_scurve(&line, centerError, 0.0f, yawRate, 0.0f);
    observedPosition = sCurveActive ? compute_scurve_sensor_position(&line) : line.position;

    if (sCurveActive)
    {
        sideSign = resolve_scurve_side_sign(0, observedPosition - tune->trackLine.centerBias, line.bits);
        targetLinePosition = compute_scurve_target_line_position(observedPosition, sideSign);
        out->lineKpScale = tune->trackSCurve.lineKpScale;
        out->yawLimit = tune->trackSCurve.yawLimit;
        out->headingDiffRatio = tune->trackSCurve.diffRatio;
        out->headingDiffMin = tune->trackSCurve.diffMin;
    }
    else
    {
        targetLinePosition = tune->trackLine.centerBias;
        out->lineKpScale = 1.0f;
        out->yawLimit = tune->trackLine.targetYawLimit;
        out->headingDiffRatio = tune->trackLine.diffRatio;
        out->headingDiffMin = tune->trackLine.diffMin;
    }

    positionError = observedPosition - targetLinePosition;
    effectiveError = sCurveActive ? shape_scurve_error(positionError) : positionError;
    yawCommand = (tune->trackLine.kp * out->lineKpScale) * effectiveError;
    yawCommand = clampf(yawCommand, -out->yawLimit, out->yawLimit);

    out->sCurveActive = sCurveActive;
    out->edgeLockActive = (sCurveActive && compute_edge_lock_active(&line, observedPosition)) ? 1u : 0u;
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
    update_runtime_limits(g_lineTrack.sCurveActive);
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

        update_runtime_limits(g_lineTrack.sCurveActive);

        if (unseenMs > tune->trackLine.lineLossHoldMs)
        {
            g_lineTrack.yawCommand *= tune->trackLine.lineLossYawDecay;
            g_lineTrack.speedScale = tune->trackLine.lineLossSpeedScale;
            if (g_lineTrack.sCurveActive)
                g_lineTrack.speedScale = maxf(g_lineTrack.speedScale,
                                              tune->trackSCurve.lossSpeedScaleMin);
        }
        g_lineTrack.targetYaw = wrap_deg(currentYaw + g_lineTrack.yawCommand);
        g_lineTrack.edgeLockActive = 0u;
        return;
    }

    g_lineTrack.lastSeenTick = tickMs;

    prevFiltered = g_lineTrack.filteredPosition;
    baseCenterError = line.position - tune->trackLine.centerBias;
    update_scurve_state(&line, baseCenterError, g_lineTrack.filteredDelta, yawRate);

    if (g_lineTrack.sCurveActive)
        observedPosition = compute_scurve_sensor_position(&line);
    else
        observedPosition = line.position;

    g_lineTrack.rawPosition = observedPosition;
    g_lineTrack.filteredPosition += tune->trackLine.posLpf * (observedPosition - g_lineTrack.filteredPosition);

    delta = g_lineTrack.filteredPosition - prevFiltered;
    g_lineTrack.filteredDelta += tune->trackLine.derivLpf * (delta - g_lineTrack.filteredDelta);

    update_runtime_limits(g_lineTrack.sCurveActive);

    if (g_lineTrack.sCurveActive)
    {
        g_lineTrack.sCurveSideSign = resolve_scurve_side_sign(g_lineTrack.sCurveSideSign,
                                                              g_lineTrack.filteredPosition - tune->trackLine.centerBias,
                                                              line.bits);
        targetLinePosition = compute_scurve_target_line_position(g_lineTrack.filteredPosition,
                                                                 g_lineTrack.sCurveSideSign);
    }
    else
    {
        targetLinePosition = tune->trackLine.centerBias;
    }

    g_lineTrack.edgeLockActive = compute_edge_lock_active(&line, g_lineTrack.filteredPosition);
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
