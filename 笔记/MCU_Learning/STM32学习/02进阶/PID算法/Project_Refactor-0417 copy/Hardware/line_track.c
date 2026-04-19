/*
 * exp0409 回退版循迹前端:
 * 1. 只保留当时真实在跑的两层状态: capture 和 scurve。
 * 2. 去掉后加的找线 / 回收 / 锁中分支，避免前端状态继续膨胀。
 * 3. S 弯态负责抬高 line_kp_scale / yaw_limit / diff 权限，并使用更偏向边缘的观测。
 */
#include "line_track.h"
#include "config.h"
#include "sensor_fusion.h"
#include "tune_params.h"
#include <string.h>

LineTrack_State_t g_lineTrack;

/* 保持与 sensor_fusion.c 相同的基础坐标系。
   S 弯态只是在此基础上重新分配各传感器参与系数，不改底层读线定义。 */
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

static float recenter_influence_scale(float scale)
{
    scale = clampf(scale, 0.0f, 1.0f);
    /* 线性 rc 对“换边第一拍”的抑制还不够。
       这里改成前段更敏感的凸形映射，让 0.15~0.40 这段残余 rc
       也能对新一侧 capture 产生明显影响。 */
    return scale * (2.0f - scale);
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

static float compute_curve_speed_scale(float positionError)
{
    const TuneRuntime_t *tune = TuneParams_Get();
    float absPos = absf(positionError);
    float span;
    float ratio;

    /* exp0409 的速度链仍然是“偏得越远，弯中越主动收速”的基本模式。 */
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

static float apply_capture_speed_floor(float speedScale, uint8_t captureActive)
{
    const TuneRuntime_t *tune = TuneParams_Get();

    if (!captureActive)
        return clampf(speedScale, 0.0f, 1.0f);

    /* 只要还能看到边缘线，就维持 capture 专属底速。
       这正是 exp0409 日志里 `ca=1 -> ss=0.84` 的来源。 */
    if (speedScale < tune->trackLine.captureSpeedScaleMin)
        speedScale = tune->trackLine.captureSpeedScaleMin;

    return clampf(speedScale, 0.0f, 1.0f);
}

static float get_capture_effective_scale(void)
{
    return clampf(g_lineTrack.captureAuthorityScale * g_lineTrack.captureRateReliefScale, 0.0f, 1.0f);
}

static float compute_capture_authority_scale(uint8_t captureCandidate)
{
    const TuneRuntime_t *tune = TuneParams_Get();
    float entryScale = clampf(tune->trackLine.captureEntryScale, 0.0f, 1.0f);
    float releaseScale;
    float ramp;
    uint32_t fullConfirmCount;

    if (!g_lineTrack.captureActive)
        return 0.0f;

    if (g_lineTrack.captureSwitchActive)
    {
        /* 换边第一段采用更软的 capture 接管:
           第一拍起始权限更低，拉满也更慢，
           这样线刚从另一侧出现时不会立刻把车头狠狠干到反向盲区。 */
        entryScale = clampf(tune->trackLine.captureSwitchEntryScale, 0.0f, 1.0f);
    }

    if (!captureCandidate)
    {
        /* 只要已经脱离“稀疏边缘位型”，capture 权限就主动衰减。
           这样车仍保持回线方向，但不会继续沿着第一拍的力度越拉越深。 */
        releaseScale = g_lineTrack.captureAuthorityScale * tune->trackLine.captureReleaseDecay;
        return clampf(maxf(releaseScale, entryScale * 0.75f), 0.0f, 1.0f);
    }

    fullConfirmCount = tune->trackLine.captureFullConfirmCount;
    if (g_lineTrack.captureSwitchActive &&
        tune->trackLine.captureSwitchConfirmCount > fullConfirmCount)
    {
        fullConfirmCount = tune->trackLine.captureSwitchConfirmCount;
    }
    if (fullConfirmCount == 0u)
        fullConfirmCount = 1u;

    ramp = (float)g_lineTrack.captureConfirmCount / (float)fullConfirmCount;
    return lerpf(entryScale, 1.0f, clampf(ramp, 0.0f, 1.0f));
}

static float compute_capture_rate_relief_scale(float rawError, float yawRate)
{
    const TuneRuntime_t *tune = TuneParams_Get();
    float alignedYawRate;
    float startRate;
    float fullRate;
    float ratio;
    float minScale;

    if (!g_lineTrack.captureActive)
        return 1.0f;
    if (absf(rawError) < 0.05f)
        return 1.0f;

    alignedYawRate = signf_nonzero(rawError) * yawRate;
    startRate = tune->trackLine.captureRateReliefStart;
    fullRate = maxf(tune->trackLine.captureRateReliefFull, startRate + 1.0f);
    minScale = clampf(tune->trackLine.captureRateReliefMinScale, 0.05f, 1.0f);

    if (alignedYawRate <= startRate)
        return 1.0f;

    ratio = clampf((alignedYawRate - startRate) / (fullRate - startRate), 0.0f, 1.0f);
    return lerpf(1.0f, minScale, ratio);
}

static float apply_scurve_speed_floor(float speedScale, uint8_t sCurveActive)
{
    const TuneRuntime_t *tune = TuneParams_Get();

    if (sCurveActive && speedScale < tune->trackSCurve.speedScaleMin)
        speedScale = tune->trackSCurve.speedScaleMin;

    return clampf(speedScale, 0.0f, 1.0f);
}

static float apply_min_yaw_floor(float yawCommand, float signHint, float minFloor)
{
    if (minFloor <= 0.0f)
        return yawCommand;
    if (absf(yawCommand) >= minFloor)
        return yawCommand;
    return signf_nonzero(signHint) * minFloor;
}

static void compute_active_frontend_gains_for_state(uint8_t sCurveActive,
                                                    float sCurveAuthorityScale,
                                                    float *lineKpScale,
                                                    float *yawLimit)
{
    const TuneRuntime_t *tune = TuneParams_Get();
    float kpScale = 1.0f;
    float limit = tune->trackLine.targetYawLimit;

    if (sCurveActive && sCurveAuthorityScale > 0.0f)
    {
        kpScale = lerpf(1.0f,
                        tune->trackSCurve.lineKpScale,
                        sCurveAuthorityScale);
        limit = lerpf(tune->trackLine.targetYawLimit,
                      tune->trackSCurve.yawLimit,
                      sCurveAuthorityScale);
    }

    *lineKpScale = kpScale;
    *yawLimit = limit;
}

static void compute_heading_authority_for_state(uint8_t captureActive,
                                                float captureScale,
                                                uint8_t sCurveActive,
                                                float sCurveScale,
                                                float *diffRatio,
                                                float *diffMin)
{
    const TuneRuntime_t *tune = TuneParams_Get();
    float ratio = tune->trackLine.diffRatio;
    float minValue = tune->trackLine.diffMin;

    if (sCurveActive && sCurveScale > 0.0f)
    {
        ratio = lerpf(ratio,
                      maxf(ratio, tune->trackSCurve.diffRatio),
                      sCurveScale);
        minValue = lerpf(minValue,
                         maxf(minValue, tune->trackSCurve.diffMin),
                         sCurveScale);
    }

    if (captureActive)
    {
        ratio = lerpf(ratio,
                      maxf(ratio, tune->trackLine.captureDiffRatio),
                      captureScale);
        minValue = lerpf(minValue,
                         maxf(minValue, tune->trackLine.captureDiffMin),
                         captureScale);
    }

    *diffRatio = ratio;
    *diffMin = minValue;
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

    /* exp0409 之前已经引入了“中心弱、边缘强”的 S 弯观测。
       这里恢复的是那条简单链路:
       1. S4/S5 可弱化，避免弯中反复硬回中；
       2. S1/S8 权重更高，边缘一出现就尽快提供拉回方向。 */
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

static float shape_scurve_error_with_scale(float error, uint8_t sCurveActive, float authorityScale)
{
    const TuneRuntime_t *tune = TuneParams_Get();
    float absErr;
    float centerZone;
    float shaped;

    if (!sCurveActive || authorityScale <= 0.001f)
        return error;

    absErr = absf(error);
    centerZone = tune->trackSCurve.centerZone;

    if (centerZone <= 0.0f)
        shaped = absErr * tune->trackSCurve.edgeGain;
    else if (absErr <= centerZone)
        shaped = absErr * tune->trackSCurve.centerGain;
    else
    {
        shaped = centerZone * tune->trackSCurve.centerGain;
        shaped += (absErr - centerZone) * tune->trackSCurve.edgeGain;
    }

    /* S 弯尾段不再整段都按满额的“中心弱、边缘强”去塑形。
       authorityScale 越低，误差越接近原始 filtered/capture 误差，
       这样 capture 退掉后会自然回到更平顺的控制面。 */
    return lerpf(error, signf_nonzero(error) * shaped, authorityScale);
}

static float shape_scurve_error(float error)
{
    return shape_scurve_error_with_scale(error,
                                         g_lineTrack.sCurveActive,
                                         g_lineTrack.sCurveAuthorityScale);
}

static float compute_capture_error(float filteredError, float rawError, uint8_t sensorCount,
                                   float captureAuthorityScale)
{
    const TuneRuntime_t *tune = TuneParams_Get();
    float captureError = filteredError;

    /* 稀疏边缘位型时更信原始误差，让第一下拉回更果断。 */
    if (sensorCount <= 2u && absf(rawError) >= tune->trackLine.captureErrorStart)
    {
        float boostedRaw = rawError * tune->trackLine.captureErrorGain;
        if (absf(boostedRaw) > absf(captureError))
            captureError = lerpf(filteredError, boostedRaw, clampf(captureAuthorityScale, 0.0f, 1.0f));
    }

    return captureError;
}

static float compute_recenter_scale(uint8_t sensorBits, uint8_t sensorCount, float centerError)
{
    const TuneRuntime_t *tune = TuneParams_Get();
    float ratio;

    if ((!g_lineTrack.sCurveActive && !g_lineTrack.captureActive) ||
        !line_has_center_bits(sensorBits) ||
        sensorCount < 2u)
        return 0.0f;

    if (tune->trackLine.recenterErrorLimit <= 0.001f)
        return 0.0f;

    /* 只有“中间灯重新出现且原始位置已接近中心”时才触发回中重锁。
       这样不会把外侧刚露头的正常拉边也误判成“已经回正”。 */
    ratio = 1.0f - absf(centerError) / tune->trackLine.recenterErrorLimit;
    return clampf(ratio, 0.0f, 1.0f);
}

static float update_recenter_state(float recenterCandidateScale)
{
    const TuneRuntime_t *tune = TuneParams_Get();
    float carriedScale;

    recenterCandidateScale = clampf(recenterCandidateScale, 0.0f, 1.0f);
    carriedScale = g_lineTrack.recenterScale * tune->trackLine.recenterReleaseDecay;
    g_lineTrack.recenterScale = maxf(recenterCandidateScale, carriedScale);
    if (g_lineTrack.recenterScale < 0.001f)
        g_lineTrack.recenterScale = 0.0f;
    g_lineTrack.recenterActive = (g_lineTrack.recenterScale > 0.001f) ? 1u : 0u;
    return g_lineTrack.recenterScale;
}

static void apply_recenter_authority_caps(void)
{
    const TuneRuntime_t *tune = TuneParams_Get();
    float influence;
    float authorityCap;

    if (!g_lineTrack.recenterActive)
        return;

    /* 回中时主动把 capture/scurve 余力收一档。
       这一步的目标不是完全放弃边缘拉回，而是避免上一拍的边缘权限
       在中间灯已经回来后继续把车头甩出线外。 */
    influence = recenter_influence_scale(g_lineTrack.recenterScale);
    authorityCap = lerpf(1.0f,
                         tune->trackLine.recenterAuthorityScale,
                         influence);
    g_lineTrack.captureAuthorityScale = clampf(g_lineTrack.captureAuthorityScale, 0.0f, authorityCap);
    g_lineTrack.sCurveAuthorityScale = clampf(g_lineTrack.sCurveAuthorityScale, 0.0f, authorityCap);
}

static float boost_sparse_capture_position(float position, uint8_t sensorBits, uint8_t sensorCount)
{
    const TuneRuntime_t *tune = TuneParams_Get();
    float error = position - tune->trackLine.centerBias;
    float gain = 1.0f;

    if (sensorCount == 0u)
        return position;
    if (line_has_center_bits(sensorBits))
        return position;

    if (sensorCount == 1u)
        gain = tune->trackLine.captureSparsePosGain;
    else if (sensorCount == 2u)
        gain = lerpf(1.0f, tune->trackLine.captureSparsePosGain, 0.65f);
    else
        return position;

    return tune->trackLine.centerBias + error * gain;
}

static uint8_t should_capture(float rawError, uint8_t sensorCount)
{
    const TuneRuntime_t *tune = TuneParams_Get();
    return (sensorCount <= 2u && absf(rawError) >= tune->trackLine.captureErrorStart) ? 1u : 0u;
}

static uint8_t should_soften_switch_capture(float rawError)
{
    const TuneRuntime_t *tune = TuneParams_Get();

    if ((!g_lineTrack.sCurveActive && !g_lineTrack.recenterActive) ||
        absf(g_lineTrack.positionError) > tune->trackLine.captureSwitchErrorLimit)
    {
        return 0u;
    }

    /* 只有误差符号真的从上一拍翻到另一侧，才认为是“反向换边第一段”。 */
    if (rawError * g_lineTrack.positionError >= 0.0f)
        return 0u;

    return 1u;
}

static void update_capture_state(uint8_t captureCandidate, uint8_t switchCaptureCandidate,
                                 float positionError)
{
    if (captureCandidate)
    {
        if (!g_lineTrack.captureActive)
        {
            g_lineTrack.captureActive = 1u;
            g_lineTrack.captureConfirmCount = 0u;
            g_lineTrack.captureSwitchActive = switchCaptureCandidate;
        }

        if (g_lineTrack.captureConfirmCount < 255u)
            g_lineTrack.captureConfirmCount++;
        g_lineTrack.captureAuthorityScale = compute_capture_authority_scale(1u);
        return;
    }

    if (!g_lineTrack.captureActive)
    {
        g_lineTrack.captureConfirmCount = 0u;
        g_lineTrack.captureAuthorityScale = 0.0f;
        g_lineTrack.captureRateReliefScale = 1.0f;
        g_lineTrack.captureSwitchActive = 0u;
        return;
    }

    g_lineTrack.captureConfirmCount = 0u;
    g_lineTrack.captureAuthorityScale = compute_capture_authority_scale(0u);
    if (absf(positionError) <= TuneParams_Get()->trackLine.captureExitError)
    {
        g_lineTrack.captureActive = 0u;
        g_lineTrack.captureAuthorityScale = 0.0f;
        g_lineTrack.captureRateReliefScale = 1.0f;
        g_lineTrack.captureSwitchActive = 0u;
    }
}

static float compute_scurve_authority_scale(uint8_t sensorBits, float filteredError,
                                            float filteredDelta, float yawRate,
                                            uint8_t captureActive)
{
    const TuneRuntime_t *tune = TuneParams_Get();
    float errorRatio;
    float deltaRatio;
    float yawRatio;
    float authorityLimit;
    float authorityScale;

    if (!g_lineTrack.sCurveActive)
        return 0.0f;

    if (captureActive)
        return get_capture_effective_scale();

    /* capture 退出后，scurve 只保留一层“尾段余力”。
       这里不再让它继续按满权限工作，而是根据剩余误差/变化率/角速度
       算一个 0~0.85 的缩放量，用来逐步放权。 */
    errorRatio = absf(filteredError) / maxf(tune->trackSCurve.enterError * 3.0f, 0.10f);
    deltaRatio = absf(filteredDelta) / maxf(tune->trackSCurve.enterDelta * 2.0f, 0.05f);
    yawRatio = absf(yawRate) / maxf(tune->trackSCurve.enterYawRate * 2.2f, 1.0f);

    authorityLimit = line_has_center_bits(sensorBits) ? 0.85f : 0.65f;
    authorityScale = clampf(errorRatio, 0.0f, 1.0f) * authorityLimit;
    authorityScale = maxf(authorityScale,
                          clampf(deltaRatio, 0.0f, 1.0f) * authorityLimit * 0.75f);
    authorityScale = maxf(authorityScale,
                          clampf(yawRatio, 0.0f, 1.0f) * authorityLimit * 0.55f);

    return clampf(authorityScale, 0.0f, authorityLimit);
}

static uint8_t should_enter_scurve(uint8_t sensorBits, float rawError, float filteredError,
                                   float filteredDelta, float yawRate, float yawCommand,
                                   uint8_t captureCandidate)
{
    const TuneRuntime_t *tune = TuneParams_Get();
    float absRaw = absf(rawError);
    float absFiltered = absf(filteredError);
    float absDelta = absf(filteredDelta);
    float absYawRate = absf(yawRate);
    float absYawCmd = absf(yawCommand);

    if (captureCandidate)
        return 2u;

    /* 进入 scurve 不再是“任一条件成立就切进”。
       exp0547 的问题就在这里: 仅凭单拍 yawRate 或 yawCommand 抖动，
       就会把前端提前推进高权限 S 弯态。现在改成:
       1. 明显边缘位型可立即进入；
       2. 其余情况至少要“偏差 + 动态证据”同时满足，且连续确认。 */
    if (absRaw >= tune->trackSCurve.enterError * 1.6f)
        return 2u;
    if (absFiltered >= tune->trackSCurve.enterError * 1.8f &&
        absDelta >= tune->trackSCurve.enterDelta * 0.8f)
        return 2u;

    if ((absRaw >= tune->trackSCurve.enterError ||
         absFiltered >= tune->trackSCurve.enterError) &&
        (absDelta >= tune->trackSCurve.enterDelta ||
         absYawRate >= tune->trackSCurve.enterYawRate ||
         absYawCmd >= tune->trackSCurve.enterYawCommand))
        return 1u;

    if (line_has_center_bits(sensorBits) &&
        absYawRate >= tune->trackSCurve.enterYawRate * 1.4f &&
        absDelta >= tune->trackSCurve.enterDelta * 0.8f)
        return 1u;

    return 0u;
}

static uint8_t should_exit_scurve(uint8_t sensorBits, float filteredError, float filteredDelta,
                                  float yawRate, uint8_t captureActive,
                                  float authorityScale)
{
    const TuneRuntime_t *tune = TuneParams_Get();

    /* exp0409 没有后来的多相位退相链。
       这里只保留一个简单但偏保守的退出证据:
       重新回到中心附近、位置变化放缓、角速度不再很猛，并且当前不在边缘捕获。 */
    if (captureActive)
        return 0u;
    /* 即使还挂着 scurve 布尔态，只要尾段权限已经明显收下来了，
       就允许开始累计退出证据，避免整段长时间挂在高权限状态。 */
    if (authorityScale > 0.35f)
        return 0u;
    if (!line_has_center_bits(sensorBits) &&
        absf(filteredError) > tune->trackSCurve.exitCenterError * 1.5f)
        return 0u;
    if (absf(filteredDelta) > tune->trackSCurve.exitCenterDelta)
        return 0u;
    if (absf(yawRate) > tune->trackSCurve.exitCenterYawRate)
        return 0u;
    return 1u;
}

static void update_scurve_state(uint8_t sensorBits, float rawError, float filteredError,
                                float filteredDelta, float yawRate, float yawCommand,
                                uint8_t captureCandidate)
{
    const TuneRuntime_t *tune = TuneParams_Get();
    uint8_t enterEvidence;

    if (!g_lineTrack.sCurveActive)
    {
        g_lineTrack.sCurveAuthorityScale = 0.0f;
        enterEvidence = should_enter_scurve(sensorBits, rawError, filteredError,
                                            filteredDelta, yawRate, yawCommand,
                                            captureCandidate);

        if (enterEvidence >= 2u)
        {
            g_lineTrack.sCurveActive = 1u;
            g_lineTrack.sCurveEnterConfirmCount = 0u;
            g_lineTrack.sCurveExitConfirmCount = 0u;
            g_lineTrack.sCurveAuthorityScale = maxf(get_capture_effective_scale(), 0.55f);
            return;
        }

        if (enterEvidence == 1u)
        {
            if (g_lineTrack.sCurveEnterConfirmCount < 255u)
                g_lineTrack.sCurveEnterConfirmCount++;
            if (g_lineTrack.sCurveEnterConfirmCount >= 2u)
            {
                g_lineTrack.sCurveActive = 1u;
                g_lineTrack.sCurveEnterConfirmCount = 0u;
                g_lineTrack.sCurveExitConfirmCount = 0u;
                g_lineTrack.sCurveAuthorityScale = 0.65f;
            }
        }
        else
        {
            g_lineTrack.sCurveEnterConfirmCount = 0u;
        }
        return;
    }

    g_lineTrack.sCurveEnterConfirmCount = 0u;
    g_lineTrack.sCurveAuthorityScale = compute_scurve_authority_scale(sensorBits,
                                                                      filteredError,
                                                                      filteredDelta,
                                                                      yawRate,
                                                                      g_lineTrack.captureActive);

    if (should_exit_scurve(sensorBits, filteredError, filteredDelta, yawRate,
                           g_lineTrack.captureActive, g_lineTrack.sCurveAuthorityScale))
    {
        if (g_lineTrack.sCurveExitConfirmCount < 255u)
            g_lineTrack.sCurveExitConfirmCount++;
    }
    else
    {
        g_lineTrack.sCurveExitConfirmCount = 0u;
    }

    if (g_lineTrack.sCurveExitConfirmCount >= tune->trackSCurve.exitConfirmCount)
    {
        g_lineTrack.sCurveActive = 0u;
        g_lineTrack.sCurveAuthorityScale = 0.0f;
        g_lineTrack.sCurveExitConfirmCount = 0u;
    }
}

static float apply_capture_yaw_floor_with_scale(float yawCommand, float error,
                                                uint8_t captureActive, float captureScale)
{
    const TuneRuntime_t *tune = TuneParams_Get();
    float minFloor;

    if (!captureActive)
        return yawCommand;

    minFloor = tune->trackLine.captureYawFloor * captureScale;
    return apply_min_yaw_floor(yawCommand, error, minFloor);
}

static float apply_capture_yaw_floor(float yawCommand, float error, uint8_t captureActive)
{
    return apply_capture_yaw_floor_with_scale(yawCommand, error,
                                              captureActive,
                                              get_capture_effective_scale());
}

static void update_active_frontend_gains(void)
{
    /* 运行态和待机快照都要共用同一套权限映射，不再各自手写一遍。 */
    compute_active_frontend_gains_for_state(g_lineTrack.sCurveActive,
                                            g_lineTrack.sCurveAuthorityScale,
                                            &g_lineTrack.activeLineKpScale,
                                            &g_lineTrack.activeYawLimit);
}

static void update_heading_authority(uint8_t captureActive)
{
    compute_heading_authority_for_state(captureActive,
                                        get_capture_effective_scale(),
                                        g_lineTrack.sCurveActive,
                                        g_lineTrack.sCurveAuthorityScale,
                                        &g_lineTrack.headingDiffRatio,
                                        &g_lineTrack.headingDiffMin);
}

void LineTrack_CollectIdleSnapshot(float currentYaw, float yawRate, LineTrack_Snapshot_t *out)
{
    const TuneRuntime_t *tune = TuneParams_Get();
    LineSensor_Data_t line;
    float activePosition;
    float rawError;
    float effectiveError;
    float lineKpScale;
    float yawLimit;
    float headingDiffRatio;
    float headingDiffMin;
    float yawCommandSeed;
    uint8_t captureCandidate;
    uint8_t sCurveCandidate;

    if (!out)
        return;

    memset(out, 0, sizeof(*out));
    out->speedScale = 1.0f;
    out->captureRateReliefScale = 1.0f;
    out->yawLimit = tune->trackLine.targetYawLimit;
    out->headingDiffRatio = tune->trackLine.diffRatio;
    out->headingDiffMin = tune->trackLine.diffMin;
    out->lineKpScale = 1.0f;
    out->targetYaw = currentYaw;

    /* 这份快照只服务待机观测和脚本预触发:
       1. 直接读一帧当前线位；
       2. 用“无历史”的即时判据估算 ca/sc；
       3. 不改 g_lineTrack，不污染真正起跑时的状态机。 */
    LineSensor_Read(&line);
    out->sensorBits = line.bits;
    out->sensorCount = line.count;
    out->lineDetected = line.lineDetected;

    if (!line.lineDetected)
        return;

    activePosition = line.position;
    rawError = activePosition - tune->trackLine.centerBias;
    captureCandidate = should_capture(rawError, line.count);

    if (captureCandidate)
    {
        activePosition = compute_scurve_sensor_position(&line);
        rawError = activePosition - tune->trackLine.centerBias;
        captureCandidate = should_capture(rawError, line.count);
    }

    if (captureCandidate && line.count <= 2u)
    {
        activePosition = boost_sparse_capture_position(activePosition, line.bits, line.count);
        rawError = activePosition - tune->trackLine.centerBias;
        captureCandidate = should_capture(rawError, line.count);
    }

    out->linePosition = activePosition;
    out->positionError = rawError;
    out->captureActive = captureCandidate;
    out->captureAuthorityScale = captureCandidate ? 1.0f : 0.0f;
    out->captureSwitchActive = 0u;
    out->recenterScale = 0.0f;

    effectiveError = rawError;
    if (captureCandidate)
    {
        effectiveError = compute_capture_error(rawError, rawError, line.count, 1.0f);
    }

    yawCommandSeed = g_lineTrack.kp * effectiveError;
    sCurveCandidate = should_enter_scurve(line.bits,
                                          rawError,
                                          rawError,
                                          0.0f,
                                          yawRate,
                                          yawCommandSeed,
                                          captureCandidate) ? 1u : 0u;
    out->sCurveActive = sCurveCandidate;

    if (sCurveCandidate)
        effectiveError = shape_scurve_error_with_scale(effectiveError, 1u, 1.0f);
    out->effectiveError = effectiveError;

    compute_active_frontend_gains_for_state(sCurveCandidate, sCurveCandidate ? 1.0f : 0.0f,
                                            &lineKpScale, &yawLimit);
    compute_heading_authority_for_state(captureCandidate, captureCandidate ? 1.0f : 0.0f,
                                        sCurveCandidate, sCurveCandidate ? 1.0f : 0.0f,
                                        &headingDiffRatio, &headingDiffMin);

    out->lineKpScale = lineKpScale;
    out->yawLimit = yawLimit;
    out->headingDiffRatio = headingDiffRatio;
    out->headingDiffMin = headingDiffMin;
    out->speedScale = compute_curve_speed_scale(effectiveError);
    out->speedScale = apply_scurve_speed_floor(out->speedScale, sCurveCandidate);
    out->speedScale = apply_capture_speed_floor(out->speedScale, captureCandidate);

    out->yawCommand = (g_lineTrack.kp * lineKpScale) * effectiveError;
    out->yawCommand = apply_capture_yaw_floor_with_scale(out->yawCommand, effectiveError,
                                                         captureCandidate,
                                                         captureCandidate ? 1.0f : 0.0f);
    out->yawCommand = clampf(out->yawCommand, -yawLimit, yawLimit);
    out->targetYaw = wrap_deg(currentYaw + out->yawCommand);
}

void LineTrack_RefreshTune(void)
{
    const TuneRuntime_t *tune = TuneParams_Get();

    g_lineTrack.kp = tune->trackLine.kp;
    g_lineTrack.kd = tune->trackLine.kd;
}

void LineTrack_Init(void)
{
    const TuneRuntime_t *tune = TuneParams_Get();

    memset(&g_lineTrack, 0, sizeof(g_lineTrack));
    LineTrack_RefreshTune();
    g_lineTrack.speedScale = 1.0f;
    g_lineTrack.activeLineKpScale = 1.0f;
    g_lineTrack.activeYawLimit = tune->trackLine.targetYawLimit;
    g_lineTrack.captureAuthorityScale = 0.0f;
    g_lineTrack.captureRateReliefScale = 1.0f;
    g_lineTrack.captureSwitchActive = 0u;
    g_lineTrack.sCurveAuthorityScale = 0.0f;
    g_lineTrack.recenterActive = 0u;
    g_lineTrack.recenterScale = 0.0f;
    update_heading_authority(0u);
}

void LineTrack_Start(uint32_t tickMs, float currentYaw)
{
    const TuneRuntime_t *tune = TuneParams_Get();

    g_lineTrack.state = LT_STATE_RUNNING;
    g_lineTrack.sensorBits = 0u;
    g_lineTrack.sensorCount = 0u;
    g_lineTrack.lineDetected = 0u;
    g_lineTrack.captureActive = 0u;
    g_lineTrack.captureConfirmCount = 0u;
    g_lineTrack.captureSwitchActive = 0u;
    g_lineTrack.sCurveActive = 0u;
    g_lineTrack.sCurveEnterConfirmCount = 0u;
    g_lineTrack.sCurveExitConfirmCount = 0u;
    g_lineTrack.rawPosition = 0.0f;
    g_lineTrack.filteredPosition = 0.0f;
    g_lineTrack.filteredDelta = 0.0f;
    g_lineTrack.positionError = 0.0f;
    g_lineTrack.effectiveError = 0.0f;
    g_lineTrack.yawCommand = 0.0f;
    g_lineTrack.targetYaw = wrap_deg(currentYaw);
    g_lineTrack.speedScale = 1.0f;
    LineTrack_RefreshTune();
    g_lineTrack.activeLineKpScale = 1.0f;
    g_lineTrack.activeYawLimit = tune->trackLine.targetYawLimit;
    g_lineTrack.captureAuthorityScale = 0.0f;
    g_lineTrack.captureRateReliefScale = 1.0f;
    g_lineTrack.captureSwitchActive = 0u;
    g_lineTrack.sCurveAuthorityScale = 0.0f;
    g_lineTrack.recenterActive = 0u;
    g_lineTrack.recenterScale = 0.0f;
    update_heading_authority(0u);
    g_lineTrack.lastSeenTick = tickMs;
}

void LineTrack_Stop(void)
{
    const TuneRuntime_t *tune = TuneParams_Get();

    g_lineTrack.state = LT_STATE_IDLE;
    g_lineTrack.sensorBits = 0u;
    g_lineTrack.sensorCount = 0u;
    g_lineTrack.lineDetected = 0u;
    g_lineTrack.captureActive = 0u;
    g_lineTrack.captureConfirmCount = 0u;
    g_lineTrack.captureSwitchActive = 0u;
    g_lineTrack.sCurveActive = 0u;
    g_lineTrack.sCurveEnterConfirmCount = 0u;
    g_lineTrack.sCurveExitConfirmCount = 0u;
    g_lineTrack.rawPosition = 0.0f;
    g_lineTrack.filteredPosition = 0.0f;
    g_lineTrack.filteredDelta = 0.0f;
    g_lineTrack.positionError = 0.0f;
    g_lineTrack.effectiveError = 0.0f;
    g_lineTrack.yawCommand = 0.0f;
    g_lineTrack.targetYaw = 0.0f;
    g_lineTrack.speedScale = 1.0f;
    LineTrack_RefreshTune();
    g_lineTrack.activeLineKpScale = 1.0f;
    g_lineTrack.activeYawLimit = tune->trackLine.targetYawLimit;
    g_lineTrack.captureAuthorityScale = 0.0f;
    g_lineTrack.captureRateReliefScale = 1.0f;
    g_lineTrack.captureSwitchActive = 0u;
    g_lineTrack.sCurveAuthorityScale = 0.0f;
    g_lineTrack.recenterActive = 0u;
    g_lineTrack.recenterScale = 0.0f;
    update_heading_authority(0u);
}

void LineTrack_Update(uint32_t tickMs, float currentYaw, float yawRate)
{
    const TuneRuntime_t *tune = TuneParams_Get();
    LineSensor_Data_t line;
    uint32_t unseenMs;
    float basePosition;
    float activePosition;
    float prevFiltered;
    float rawError;
    float effectiveError;
    float yawCommand;
    float posAlpha;
    float recenterCandidateScale;
    float recenterScale;
    uint8_t switchCaptureCandidate;
    float delta;
    float carryRecenterInfluence;
    uint8_t captureCandidate;

    if (g_lineTrack.state != LT_STATE_RUNNING)
        return;

    LineTrack_RefreshTune();
    LineSensor_Read(&line);
    g_lineTrack.sensorBits = line.bits;
    g_lineTrack.sensorCount = line.count;
    g_lineTrack.lineDetected = line.lineDetected;

    if (!line.lineDetected)
    {
        float blindYawFloor = 0.0f;

        unseenMs = tickMs - g_lineTrack.lastSeenTick;
        if (unseenMs >= tune->trackLine.lineLossTimeoutMs)
        {
            LineTrack_Stop();
            return;
        }

        update_active_frontend_gains();

        /* 这里保留 exp0409 的“沿当前 yawCommand 衰减保持”，
           但补上一个关键修正:
           capture 的硬钳位只允许在盲区前几十毫秒短暂维持，
           超过这个窗口就撤掉 capture 强约束，避免整排全灭时越拽越深。 */
        g_lineTrack.yawCommand = clampf(g_lineTrack.yawCommand * tune->trackLine.lineLossYawDecay,
                                        -g_lineTrack.activeYawLimit,
                                        g_lineTrack.activeYawLimit);

        if (g_lineTrack.captureActive && unseenMs < tune->trackLine.lineLossCaptureHoldMs)
        {
            blindYawFloor = tune->trackLine.captureYawFloor
                          * tune->trackLine.lineLossCaptureYawFloorScale
                          * get_capture_effective_scale();
            g_lineTrack.yawCommand = apply_min_yaw_floor(g_lineTrack.yawCommand,
                                                         g_lineTrack.yawCommand,
                                                         blindYawFloor);
        }
        else if (g_lineTrack.captureActive)
        {
            /* 一旦盲区时间超过短保持窗，就主动退出 capture 态。
               这样后面即使还没重新见线，也不会继续按“边缘已确认”的力度死命回正。 */
            g_lineTrack.captureActive = 0u;
            g_lineTrack.captureConfirmCount = 0u;
            g_lineTrack.captureAuthorityScale = 0.0f;
            g_lineTrack.captureRateReliefScale = 1.0f;
        }

        /* 盲区里的 scurve 也不再直接按满权限顶住，
           而是根据当前残余误差和角速度自动退到“尾段余力”。 */
        g_lineTrack.sCurveAuthorityScale = compute_scurve_authority_scale(0u,
                                                                          g_lineTrack.positionError,
                                                                          g_lineTrack.filteredDelta,
                                                                          yawRate,
                                                                          g_lineTrack.captureActive);

        g_lineTrack.targetYaw = wrap_deg(currentYaw + g_lineTrack.yawCommand);
        if (unseenMs < tune->trackLine.lineLossHoldMs)
        {
            /* 短空窗沿用上一拍速度倍率，复现 exp0409 里 `ld=0` 初期仍保持 0.84 的行为。 */
            g_lineTrack.speedScale = apply_capture_speed_floor(g_lineTrack.speedScale,
                                                               g_lineTrack.captureActive);
        }
        else
        {
            g_lineTrack.speedScale = tune->trackLine.lineLossSpeedScale;
            if (g_lineTrack.sCurveActive)
                g_lineTrack.speedScale = maxf(g_lineTrack.speedScale,
                                              tune->trackSCurve.lossSpeedScaleMin);
            g_lineTrack.speedScale = clampf(g_lineTrack.speedScale, 0.0f, 1.0f);
        }

        g_lineTrack.effectiveError = g_lineTrack.positionError;
        g_lineTrack.recenterActive = 0u;
        g_lineTrack.recenterScale = 0.0f;
        g_lineTrack.captureSwitchActive = 0u;
        if (!g_lineTrack.captureActive)
            g_lineTrack.captureRateReliefScale = 1.0f;
        update_heading_authority(g_lineTrack.captureActive);
        return;
    }

    g_lineTrack.lastSeenTick = tickMs;
    carryRecenterInfluence = recenter_influence_scale(g_lineTrack.recenterScale);
    switchCaptureCandidate = 0u;

    basePosition = line.position;
    activePosition = basePosition;
    rawError = activePosition - tune->trackLine.centerBias;
    captureCandidate = should_capture(rawError, line.count);

    if (g_lineTrack.sCurveActive || g_lineTrack.captureActive || captureCandidate)
    {
        /* exp0409 的 S 弯态已经在用“中心弱、边缘强”的观测面。
           所以只要当前还在 scurve 或 capture，位置估计就切到这套加权。 */
        activePosition = compute_scurve_sensor_position(&line);
        rawError = activePosition - tune->trackLine.centerBias;
        captureCandidate = should_capture(rawError, line.count);
    }

    if ((captureCandidate || g_lineTrack.captureActive) && line.count <= 2u)
    {
        float boostedPosition;

        /* 这一步专门处理 `sb=2/4/72/128` 这类稀疏边缘位型。
           普通权重平均会把位置看得偏里，导致明明还压着边缘，前端却提前觉得“快回来了”。
           对稀疏边缘位型把位置再往外推一点，等价于让车更愿意继续卡在线边。 */
        boostedPosition = boost_sparse_capture_position(activePosition, line.bits, line.count);
        if (carryRecenterInfluence > 0.0f)
        {
            /* 如果上一拍刚经历过回中，这一拍即便又见到稀疏边缘，
               也不要立刻把位置再次推到最外侧。
               这里保留一部分原始 scurve 位置，专门压“回中后一摆头就又被甩出去”的瞬时换边。 */
            boostedPosition = lerpf(boostedPosition,
                                    activePosition,
                                    clampf(carryRecenterInfluence * 0.75f, 0.0f, 1.0f));
        }
        activePosition = boostedPosition;
        rawError = activePosition - tune->trackLine.centerBias;
        captureCandidate = should_capture(rawError, line.count);
    }

    if (!g_lineTrack.captureActive && captureCandidate)
    {
        switchCaptureCandidate = should_soften_switch_capture(rawError);
    }

    recenterCandidateScale = compute_recenter_scale(line.bits,
                                                    line.count,
                                                    basePosition - tune->trackLine.centerBias);
    if (recenterCandidateScale > 0.0f)
    {
        /* 回中重锁不用直接放弃“中心弱、边缘强”的 S 弯观测，
           而是在它和原始位置之间做受控混合。
           这样既保留弯中的边缘方向感，又能在中间灯回来时更快把位置估计拉回中线。 */
        activePosition = lerpf(activePosition,
                               basePosition,
                               tune->trackLine.recenterBlend * recenterCandidateScale);
        rawError = activePosition - tune->trackLine.centerBias;
    }

    g_lineTrack.rawPosition = activePosition;

    prevFiltered = g_lineTrack.filteredPosition;
    posAlpha = tune->trackLine.posLpf;
    if (captureCandidate && posAlpha < tune->trackLine.capturePosAlpha)
        posAlpha = tune->trackLine.capturePosAlpha;
    if (recenterCandidateScale > 0.0f)
    {
        float recenterAlpha = lerpf(posAlpha,
                                    tune->trackLine.recenterPosAlpha,
                                    recenterCandidateScale);
        if (posAlpha < recenterAlpha)
            posAlpha = recenterAlpha;
    }
    g_lineTrack.filteredPosition += posAlpha * (activePosition - g_lineTrack.filteredPosition);

    delta = g_lineTrack.filteredPosition - prevFiltered;
    g_lineTrack.filteredDelta += tune->trackLine.derivLpf * (delta - g_lineTrack.filteredDelta);

    g_lineTrack.positionError = g_lineTrack.filteredPosition - tune->trackLine.centerBias;
    update_capture_state(captureCandidate, switchCaptureCandidate, g_lineTrack.positionError);
    g_lineTrack.captureRateReliefScale = compute_capture_rate_relief_scale(rawError, yawRate);

    update_scurve_state(line.bits,
                        rawError,
                        g_lineTrack.positionError,
                        g_lineTrack.filteredDelta,
                        yawRate,
                        g_lineTrack.yawCommand,
                        captureCandidate);
    recenterScale = update_recenter_state(recenterCandidateScale);
    apply_recenter_authority_caps();

    update_active_frontend_gains();

    effectiveError = compute_capture_error(g_lineTrack.positionError, rawError, line.count,
                                           get_capture_effective_scale());
    effectiveError = shape_scurve_error(effectiveError);
    if (recenterScale > 0.0f)
    {
        float influence = recenter_influence_scale(recenterScale);

        /* 中间灯回来后，把前端误差再压一档。
           目的不是“回中后完全不纠偏”，而是避免刚回中就继续沿旧方向狠狠干拽。 */
        effectiveError *= lerpf(1.0f,
                                tune->trackLine.recenterErrorScale,
                                influence);
    }
    g_lineTrack.effectiveError = effectiveError;

    yawCommand = (g_lineTrack.kp * g_lineTrack.activeLineKpScale) * effectiveError
               + g_lineTrack.kd * g_lineTrack.filteredDelta;
    yawCommand = apply_capture_yaw_floor(yawCommand, effectiveError, g_lineTrack.captureActive);
    yawCommand = clampf(yawCommand, -g_lineTrack.activeYawLimit, g_lineTrack.activeYawLimit);

    g_lineTrack.yawCommand = yawCommand;
    g_lineTrack.targetYaw = wrap_deg(currentYaw + yawCommand);

    g_lineTrack.speedScale = compute_curve_speed_scale(effectiveError);
    g_lineTrack.speedScale = apply_scurve_speed_floor(g_lineTrack.speedScale, g_lineTrack.sCurveActive);
    g_lineTrack.speedScale = apply_capture_speed_floor(g_lineTrack.speedScale, g_lineTrack.captureActive);

    update_heading_authority(g_lineTrack.captureActive);
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
    /* exp0409 的速度约束来源很简单:
       1. capture 正在主动拉边；
       2. 或 speedScale 本拍确实被压低。 */
    if (g_lineTrack.captureActive)
        return 1u;
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

float LineTrack_GetCaptureAuthorityScale(void)
{
    return get_capture_effective_scale();
}

float LineTrack_GetCaptureRateReliefScale(void)
{
    return g_lineTrack.captureRateReliefScale;
}

float LineTrack_GetSCurveAuthorityScale(void)
{
    return g_lineTrack.sCurveAuthorityScale;
}

float LineTrack_GetRecenterScale(void)
{
    return g_lineTrack.recenterScale;
}
