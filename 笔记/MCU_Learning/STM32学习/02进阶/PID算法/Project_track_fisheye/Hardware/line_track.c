#include "line_track.h"
#include "sensor_fusion.h"
#include "motor_driver.h"
#include <stdio.h>
#include <string.h>

LineTrack_State_t g_lineTrack;
LineTrack_RuntimeConfig_t g_lineTrackCfg;

static const int16_t s_sensorLinePos[LINE_SENSOR_COUNT] = {
    TRACK_LINE_POS_S1,
    TRACK_LINE_POS_S2,
    TRACK_LINE_POS_S3,
    TRACK_LINE_POS_S4,
    TRACK_LINE_POS_S5,
    TRACK_LINE_POS_S6,
    TRACK_LINE_POS_S7,
    TRACK_LINE_POS_S8
};

static uint8_t track_is_crossing(uint8_t bits);

static void track_load_default_runtime_config(void)
{
    static const float s_defaultSensorScale[LINE_SENSOR_COUNT] = {
        1.00f, 1.00f, 1.00f, 0.98f, 0.98f, 1.00f, 1.00f, 1.00f
    };
    uint8_t i;

    for (i = 0u; i < LINE_SENSOR_COUNT; i++)
        g_lineTrackCfg.sensorScale[i] = s_defaultSensorScale[i];

    g_lineTrackCfg.devRatio = TRACK_FOLLOW_DEV_RATIO;
    g_lineTrackCfg.deadband = TRACK_FOLLOW_DEADBAND;
    g_lineTrackCfg.errorScale = TRACK_FOLLOW_ERROR_SCALE;
    g_lineTrackCfg.posFilterAlpha = TRACK_FOLLOW_POS_LPF_ALPHA;
    g_lineTrackCfg.dFilterAlpha = TRACK_FOLLOW_D_LPF_ALPHA;
    g_lineTrackCfg.baseMinPwm = TRACK_FOLLOW_BASE_MIN_PWM;
    g_lineTrackCfg.staticSteerBias = TRACK_STATIC_STEER_BIAS;
    g_lineTrackCfg.devStepLimit = TRACK_FOLLOW_DEV_STEP_LIMIT;
    g_lineTrackCfg.recoverTicks = TRACK_RECOVER_TICKS;
    g_lineTrackCfg.searchArcPwmFast = TRACK_SEARCH_ARC_PWM_FAST;
    g_lineTrackCfg.searchArcPwmSlow = TRACK_SEARCH_ARC_PWM_SLOW;
    g_lineTrackCfg.searchTurnPwmFast = TRACK_SEARCH_TURN_PWM_FAST;
    g_lineTrackCfg.searchTurnPwmSlow = TRACK_SEARCH_TURN_PWM_SLOW;
    g_lineTrackCfg.searchTimeoutTicks = TRACK_SEARCH_TIMEOUT_TICKS;
}

static float track_absf(float value)
{
    return (value >= 0.0f) ? value : -value;
}

static float track_lerpf(float a, float b, float ratio)
{
    return a + (b - a) * ratio;
}

static uint8_t track_is_search_state(uint8_t state)
{
    return (state == LT_TRACK_SEARCH_LEFT || state == LT_TRACK_SEARCH_RIGHT) ? 1u : 0u;
}

static int16_t track_abs_i16(int16_t value)
{
    return (value >= 0) ? value : (int16_t)(-value);
}

static float track_clamp_paramf(float value, float minValue, float maxValue)
{
    if (value < minValue)
        return minValue;
    if (value > maxValue)
        return maxValue;
    return value;
}

static int16_t track_round_to_i16(float value)
{
    return (value >= 0.0f) ? (int16_t)(value + 0.5f) : (int16_t)(value - 0.5f);
}

static uint8_t track_round_to_u8(float value)
{
    int16_t iv = track_round_to_i16(value);
    if (iv < 0)
        return 0u;
    if (iv > 255)
        return 255u;
    return (uint8_t)iv;
}

static uint16_t track_round_to_u16(float value)
{
    int32_t iv = (value >= 0.0f) ? (int32_t)(value + 0.5f) : (int32_t)(value - 0.5f);
    if (iv < 0)
        return 0u;
    if (iv > 65535)
        return 65535u;
    return (uint16_t)iv;
}

static uint8_t track_parse_sensor_scale_index(const char *key, uint8_t *sensorIndex)
{
    static const char s_prefix[] = "track.sensor_scale";
    const char *p;
    uint16_t index = 0u;

    if (key == 0 || sensorIndex == 0)
        return 0u;
    if (strncmp(key, s_prefix, sizeof(s_prefix) - 1u) != 0)
        return 0u;

    p = key + (sizeof(s_prefix) - 1u);
    if (*p < '0' || *p > '9')
        return 0u;

    while (*p >= '0' && *p <= '9')
    {
        index = (uint16_t)(index * 10u + (uint16_t)(*p - '0'));
        p++;
    }

    if (*p != '\0')
        return 0u;
    if (index < 1u || index > LINE_SENSOR_COUNT)
        return 0u;

    *sensorIndex = (uint8_t)index;
    return 1u;
}

static void track_reset_follow_control(void)
{
    g_lineTrack.lastDevSpeedCmd = 0;
    g_lineTrack.filteredTrackError = 0.0f;
    g_lineTrack.lastFilteredTrackError = 0.0f;
    g_lineTrack.filteredDTerm = 0.0f;
    g_lineTrack.smoothedLinePos = 0.0f;
}

static uint8_t track_bit_count(uint8_t bits)
{
    uint8_t count = 0u;
    while (bits != 0u)
    {
        count = (uint8_t)(count + (bits & 0x01u));
        bits >>= 1;
    }
    return count;
}

static uint8_t track_mask_bit_count(uint8_t bits, uint8_t mask)
{
    return track_bit_count((uint8_t)(bits & mask));
}

static float track_sensor_fit_pos(uint8_t index)
{
    float trim;

    if (index >= LINE_SENSOR_COUNT)
        return 0.0f;

    trim = (g_lineTrackCfg.sensorScale[index] - 1.0f) * TRACK_SENSOR_POS_TRIM_RANGE;
    return (float)s_sensorLinePos[index] + trim;
}

static float track_boundary_fit_pos(uint8_t boundaryIndex)
{
    float step = (float)TRACK_LINE_POS_STEP;

    if (boundaryIndex == 0u)
        return track_sensor_fit_pos(0u) - step;
    if (boundaryIndex >= LINE_SENSOR_COUNT)
        return track_sensor_fit_pos(LINE_SENSOR_COUNT - 1u) + step;

    return 0.5f * (track_sensor_fit_pos((uint8_t)(boundaryIndex - 1u))
                 + track_sensor_fit_pos(boundaryIndex));
}

static int16_t track_gradient_run_center(uint8_t start, uint8_t end)
{
    float leftBoundary;
    float rightBoundary;

    if (start >= LINE_SENSOR_COUNT)
        return 0;
    if (end >= LINE_SENSOR_COUNT)
        end = LINE_SENSOR_COUNT - 1u;
    if (end < start)
        end = start;

    leftBoundary = track_boundary_fit_pos(start);
    rightBoundary = track_boundary_fit_pos((uint8_t)(end + 1u));
    return track_round_to_i16(0.5f * (leftBoundary + rightBoundary));
}

static uint8_t track_select_preferred_run(uint8_t bits, uint8_t *bestStart, uint8_t *bestEnd)
{
    uint8_t i = 0u;
    uint8_t found = 0u;
    uint8_t selectedStart = 0u;
    uint8_t selectedEnd = 0u;
    uint8_t selectedLen = 0u;
    uint8_t selectedHasCenter = 0u;
    uint8_t selectedHasInner = 0u;
    int16_t selectedCenter = 0;
    int16_t referencePos = g_lineTrack.lastValidLinePos;

    if (bestStart == 0 || bestEnd == 0)
        return 0u;

    if (referencePos == 0)
    {
        if (g_lineTrack.lastTurnDir == LT_DIR_LEFT)
            referencePos = -TRACK_LINE_POS_SMALL_MAX;
        else if (g_lineTrack.lastTurnDir == LT_DIR_RIGHT)
            referencePos = TRACK_LINE_POS_SMALL_MAX;
    }

    while (i < LINE_SENSOR_COUNT)
    {
        uint8_t runStart;
        uint8_t runEnd;
        uint8_t runLen;
        uint8_t runMask;
        uint8_t runHasCenter;
        uint8_t runHasInner;
        int16_t runCenter;
        int16_t runDistance;
        int16_t selectedDistance;

        if ((bits & (1u << i)) == 0u)
        {
            i++;
            continue;
        }

        runStart = i;
        while ((i + 1u) < LINE_SENSOR_COUNT && (bits & (1u << (i + 1u))) != 0u)
            i++;
        runEnd = i;
        runLen = (uint8_t)(runEnd - runStart + 1u);
        runMask = (uint8_t)(bits & (uint8_t)(((uint16_t)0xFFu >> (7u - runEnd)) & (uint16_t)(0xFFu << runStart)));
        runHasCenter = ((runMask & LT_MASK_CENTER) != 0u) ? 1u : 0u;
        runHasInner = ((runMask & LT_MASK_INNER_GUIDE) != 0u) ? 1u : 0u;
        runCenter = track_gradient_run_center(runStart, runEnd);
        runDistance = track_abs_i16((int16_t)(runCenter - referencePos));

        if (!found)
        {
            found = 1u;
            selectedStart = runStart;
            selectedEnd = runEnd;
            selectedLen = runLen;
            selectedHasCenter = runHasCenter;
            selectedHasInner = runHasInner;
            selectedCenter = runCenter;
            i++;
            continue;
        }

        if (runHasCenter != selectedHasCenter)
        {
            if (runHasCenter)
            {
                selectedStart = runStart;
                selectedEnd = runEnd;
                selectedLen = runLen;
                selectedHasCenter = runHasCenter;
                selectedHasInner = runHasInner;
                selectedCenter = runCenter;
            }
            i++;
            continue;
        }

        selectedDistance = track_abs_i16((int16_t)(selectedCenter - referencePos));
        if (runDistance < selectedDistance)
        {
            selectedStart = runStart;
            selectedEnd = runEnd;
            selectedLen = runLen;
            selectedHasCenter = runHasCenter;
            selectedHasInner = runHasInner;
            selectedCenter = runCenter;
            i++;
            continue;
        }

        if (runDistance == selectedDistance)
        {
            if (runLen > selectedLen
                || (runLen == selectedLen && runHasInner > selectedHasInner)
                || (runLen == selectedLen
                    && runHasInner == selectedHasInner
                    && track_abs_i16(runCenter) < track_abs_i16(selectedCenter)))
            {
                selectedStart = runStart;
                selectedEnd = runEnd;
                selectedLen = runLen;
                selectedHasCenter = runHasCenter;
                selectedHasInner = runHasInner;
                selectedCenter = runCenter;
            }
        }

        i++;
    }

    if (!found)
        return 0u;

    *bestStart = selectedStart;
    *bestEnd = selectedEnd;
    return 1u;
}

static int16_t track_smooth_follow_line_pos(uint8_t bits, int16_t rawLinePos)
{
    float prev = g_lineTrack.smoothedLinePos;
    float target = (float)rawLinePos;
    float alpha = g_lineTrackCfg.posFilterAlpha;
    float next;
    float delta;
    float maxStep;
    uint8_t innerBits = LT_MASK_CENTER_BAND;

    if ((bits & LT_MASK_CENTER) == LT_MASK_CENTER && (bits & (LT_MASK_LEFT_OUTER | LT_MASK_RIGHT_OUTER)) == 0u)
    {
        alpha = track_clamp_paramf(alpha + 0.20f, 0.10f, 0.95f);
        target = 0.0f;
    }
    else if ((prev > 0.0f && target < 0.0f) || (prev < 0.0f && target > 0.0f))
    {
        if ((bits & innerBits) != 0u
            && track_absf(prev) <= (float)TRACK_LINE_POS_MEDIUM_MAX
            && track_absf(target) <= (float)TRACK_LINE_POS_MEDIUM_MAX)
        {
            alpha *= 0.35f;
            target = 0.0f;
        }
    }

    next = prev + alpha * (target - prev);
    maxStep = (track_absf(target) <= (float)TRACK_LINE_POS_MEDIUM_MAX)
            ? (float)(TRACK_LINE_POS_STEP + 15)
            : (float)(TRACK_LINE_POS_STEP * 2);
    delta = next - prev;
    if (delta > maxStep)
        next = prev + maxStep;
    else if (delta < -maxStep)
        next = prev - maxStep;

    if ((prev > 0.0f && next < 0.0f) || (prev < 0.0f && next > 0.0f))
        next = 0.0f;

    g_lineTrack.smoothedLinePos = next;
    return track_round_to_i16(next);
}

static uint8_t track_follow_stage_from_abs_pos(int16_t absPos)
{
    if (absPos <= TRACK_LINE_POS_SMALL_MAX)
        return 0u;
    if (absPos <= TRACK_LINE_POS_MEDIUM_MAX)
        return 1u;
    return 2u;
}

static float track_apply_deadband(float linePos)
{
    float absPos = track_absf(linePos);
    float deadband = g_lineTrackCfg.deadband;

    if (deadband <= 0.0f)
        return linePos;

    if (absPos <= deadband)
        return 0.0f;

    absPos -= deadband;
    return (linePos >= 0.0f) ? absPos : -absPos;
}

static float track_build_follow_error(uint8_t bits, int16_t linePos, uint8_t *stage)
{
    float error;
    int16_t absPos = track_abs_i16(linePos);

    if (stage != 0)
        *stage = track_follow_stage_from_abs_pos(absPos);

    if (bits == 0u || track_is_crossing(bits))
        return 0.0f;

    error = track_apply_deadband((float)linePos);
    return error / g_lineTrackCfg.errorScale;
}

static void track_select_follow_profile(int16_t linePos,
                                        float *kp,
                                        float *kd,
                                        float *devRatio,
                                        uint8_t *stage)
{
    int16_t absPos = track_abs_i16(linePos);
    float localKp = g_lineTrack.kp;
    float localKd = g_lineTrack.kd;
    float localDevRatio = g_lineTrackCfg.devRatio;
    uint8_t localStage = track_follow_stage_from_abs_pos(absPos);

    if (kp != 0) *kp = localKp;
    if (kd != 0) *kd = localKd;
    if (devRatio != 0) *devRatio = localDevRatio;
    if (stage != 0) *stage = localStage;
}

static uint8_t track_is_crossing(uint8_t bits)
{
    uint8_t count;

    if ((bits & LT_MASK_CENTER) == 0u)
        return 0u;
    if ((bits & LT_MASK_LEFT_ZONE) == 0u)
        return 0u;
    if ((bits & LT_MASK_RIGHT_ZONE) == 0u)
        return 0u;

    count = track_bit_count(bits);
    return (count >= TRACK_CROSS_MIN_ACTIVE) ? 1u : 0u;
}

static int16_t track_calculate_line_pos(uint8_t bits)
{
    uint8_t runStart = 0u;
    uint8_t runEnd = 0u;

    if (bits == 0u)
        return 0;

    if (track_is_crossing(bits))
        return 0;

    if (!track_select_preferred_run(bits, &runStart, &runEnd))
        return 0;

    return track_gradient_run_center(runStart, runEnd);
}

static int8_t track_map_bearing_dev(int16_t linePos)
{
    int16_t absPos;
    int8_t sign;

    if (linePos == 0)
        return 0;

    sign = (linePos > 0) ? 1 : -1;
    absPos = (linePos > 0) ? linePos : (int16_t)(-linePos);

    if (absPos <= TRACK_LINE_POS_CENTER_MAX)
        return 0;
    if (absPos <= TRACK_LINE_POS_SMALL_MAX)
        return (int8_t)(sign * 1);
    if (absPos <= TRACK_LINE_POS_MEDIUM_MAX)
        return (int8_t)(sign * 2);
    if (absPos <= TRACK_LINE_POS_LARGE_MAX)
        return (int8_t)(sign * 4);

    return (int8_t)(sign * 7);
}

static void track_apply_search_dir(uint8_t dir)
{
    g_lineTrack.searchDir = dir;
    g_lineTrack.trackState = (dir == LT_DIR_LEFT) ? LT_TRACK_SEARCH_LEFT : LT_TRACK_SEARCH_RIGHT;
    g_lineTrack.dbgTurnDir = dir;
}

static uint8_t track_is_severe_loss(void)
{
    uint8_t last = g_lineTrack.lastData;

    if (track_abs_i16(g_lineTrack.lastValidLinePos) >= TRACK_LINE_POS_MEDIUM_MAX)
        return 1u;
    if ((last & LT_MASK_LEFT_OUTER) != 0u || (last & LT_MASK_RIGHT_OUTER) != 0u)
        return 1u;
    if (g_lineTrack.bearingDev <= -TRACK_EDGE_BEARING_MIN
        || g_lineTrack.bearingDev >= TRACK_EDGE_BEARING_MIN)
    {
        return 1u;
    }
    return 0u;
}

static uint8_t track_get_lost_confirm_ticks(void)
{
    return track_is_severe_loss() ? TRACK_LOST_FAST_CONFIRM_TICKS : TRACK_LOST_CONFIRM_TICKS;
}

static uint8_t track_pick_search_dir(void)
{
    uint8_t last = g_lineTrack.lastData;
    uint8_t leftCount = track_mask_bit_count(last, LT_MASK_LEFT_ZONE);
    uint8_t rightCount = track_mask_bit_count(last, LT_MASK_RIGHT_ZONE);

    if (leftCount > rightCount)
        return LT_DIR_LEFT;
    if (rightCount > leftCount)
        return LT_DIR_RIGHT;
    if (g_lineTrack.lastValidLinePos < -TRACK_LINE_POS_CENTER_MAX)
        return LT_DIR_LEFT;
    if (g_lineTrack.lastValidLinePos > TRACK_LINE_POS_CENTER_MAX)
        return LT_DIR_RIGHT;
    if (g_lineTrack.lastDevSpeedCmd < 0)
        return LT_DIR_LEFT;
    if (g_lineTrack.lastDevSpeedCmd > 0)
        return LT_DIR_RIGHT;
    if (g_lineTrack.recoverTicks > 0u
        && (g_lineTrack.recoverDir == LT_DIR_LEFT || g_lineTrack.recoverDir == LT_DIR_RIGHT))
    {
        return g_lineTrack.recoverDir;
    }
    if (g_lineTrack.lastTurnDir == LT_DIR_LEFT || g_lineTrack.lastTurnDir == LT_DIR_RIGHT)
        return g_lineTrack.lastTurnDir;
    if (g_lineTrack.bearingDev < 0)
        return LT_DIR_LEFT;
    if (g_lineTrack.bearingDev > 0)
        return LT_DIR_RIGHT;
    return LT_DIR_RIGHT;
}

static void track_update_history(uint8_t bits, int16_t linePos)
{
    if (bits == 0u || track_is_crossing(bits))
        return;

    g_lineTrack.lastData = bits;
    g_lineTrack.lastValidLinePos = linePos;

    if ((bits & LT_MASK_LEFT_ZONE) && !(bits & LT_MASK_RIGHT_ZONE))
        g_lineTrack.lastTurnDir = LT_DIR_LEFT;
    else if ((bits & LT_MASK_RIGHT_ZONE) && !(bits & LT_MASK_LEFT_ZONE))
        g_lineTrack.lastTurnDir = LT_DIR_RIGHT;
    else if (linePos < -TRACK_LINE_POS_CENTER_MAX)
        g_lineTrack.lastTurnDir = LT_DIR_LEFT;
    else if (linePos > TRACK_LINE_POS_CENTER_MAX)
        g_lineTrack.lastTurnDir = LT_DIR_RIGHT;
}

static void track_motor_stop(void)
{
    MotorDriver_Stop();
    MotorDriver_Disable();
}

static void track_motor_forward(int16_t left, int16_t right)
{
    if (left > TRACK_PWM_MAX)  left = TRACK_PWM_MAX;
    if (left < TRACK_PWM_MIN)  left = TRACK_PWM_MIN;
    if (right > TRACK_PWM_MAX) right = TRACK_PWM_MAX;
    if (right < TRACK_PWM_MIN) right = TRACK_PWM_MIN;

    MotorDriver_Enable();
    MotorDriver_SetTurnPWM(left, right);
}

static void track_enter_search(uint8_t dir)
{
    if (dir != LT_DIR_LEFT && dir != LT_DIR_RIGHT)
        dir = track_pick_search_dir();

    track_apply_search_dir(dir);
    g_lineTrack.searchPhase = LT_SEARCH_PHASE_PIVOT;
    g_lineTrack.searchSeenTicks = 0u;
    g_lineTrack.searchTicks = 0u;
}

static uint8_t track_search_reacquire_class(uint8_t bits, uint8_t dir)
{
    uint8_t sideBits;

    (void)dir;

    if (bits == 0u)
        return 0u;
    if (track_is_crossing(bits))
        return 2u;
    if (bits & LT_MASK_CENTER)
        return 2u;
    if ((bits & LT_MASK_INNER_GUIDE) != 0u)
        return 2u;

    sideBits = (uint8_t)(bits & (LT_MASK_LEFT_OUTER | LT_MASK_RIGHT_OUTER));
    if (sideBits != 0u)
        return 1u;

    return 0u;
}

static void track_progress_search(void)
{
    if (g_lineTrack.searchTicks < 0xFFFFu)
        g_lineTrack.searchTicks++;

    if (g_lineTrack.searchPhase == LT_SEARCH_PHASE_ARC
        && g_lineTrack.searchTicks >= TRACK_SEARCH_ARC_TICKS)
    {
        g_lineTrack.searchPhase = LT_SEARCH_PHASE_PIVOT;
    }

    if (g_lineTrackCfg.searchTimeoutTicks > 0u
        && g_lineTrack.searchTicks >= g_lineTrackCfg.searchTimeoutTicks)
    {
        g_lineTrack.searchTicks = g_lineTrackCfg.searchTimeoutTicks;
    }
}

static void track_update_cross_state(uint8_t bits)
{
    uint8_t crossActive = track_is_crossing(bits);

    g_lineTrack.dbgCrossActive = crossActive;

    if (crossActive)
    {
        if (g_lineTrack.crossState == LT_CROSS_READY)
        {
            if (g_lineTrack.crossCount < 255u)
                g_lineTrack.crossCount++;
            g_lineTrack.crossState = LT_CROSS_SEEN;
            g_lineTrack.filterTimes = 0u;

        }
        return;
    }

    if (g_lineTrack.crossState == LT_CROSS_SEEN)
    {
        if (g_lineTrack.filterTimes < 255u)
            g_lineTrack.filterTimes++;

        if (g_lineTrack.filterTimes >= TRACK_CROSS_RELEASE_TICKS)
        {
            g_lineTrack.crossState = LT_CROSS_READY;
            g_lineTrack.filterTimes = 0u;
        }
    }
}

static uint8_t track_read_sensor_bits(void)
{
    LineSensor_Data_t sensor;
    LineSensor_Read(&sensor);
    return sensor.bits;
}

static void track_signal_update(void)
{
    uint8_t bits;
    int16_t rawLinePos;
    int16_t linePos;
    int8_t bearingDev;
    uint8_t crossActive;

    bits = track_read_sensor_bits();
    g_lineTrack.sensorData = bits;
    crossActive = track_is_crossing(bits);
    track_update_cross_state(bits);

    if (bits == 0u)
    {
        if (track_is_search_state(g_lineTrack.trackState))
        {
            track_progress_search();
            g_lineTrack.linePos = (g_lineTrack.searchDir == LT_DIR_LEFT) ? TRACK_LINE_POS_S1 : TRACK_LINE_POS_S8;
            g_lineTrack.bearingDev = (g_lineTrack.searchDir == LT_DIR_LEFT) ? -7 : 7;
        }
        else
        {
            uint8_t confirmTicks;

            if (g_lineTrack.overrunCount < 255u)
                g_lineTrack.overrunCount++;

            confirmTicks = track_get_lost_confirm_ticks();
            if (g_lineTrack.overrunCount >= confirmTicks)
            {
                uint8_t dir = track_pick_search_dir();
                track_enter_search(dir);
                g_lineTrack.linePos = (dir == LT_DIR_LEFT) ? TRACK_LINE_POS_S1 : TRACK_LINE_POS_S8;
                g_lineTrack.bearingDev = (dir == LT_DIR_LEFT) ? -7 : 7;
            }
        }

        g_lineTrack.dbgTrackState = g_lineTrack.trackState;
        return;
    }

    rawLinePos = track_calculate_line_pos(bits);
    linePos = track_smooth_follow_line_pos(bits, rawLinePos);
    bearingDev = track_map_bearing_dev(linePos);

    if (track_is_search_state(g_lineTrack.trackState))
    {
        uint8_t reacquireClass = 0u;

        if (g_lineTrack.searchTicks > TRACK_SEARCH_BLIND_TICKS)
        {
            reacquireClass = track_search_reacquire_class(bits, g_lineTrack.searchDir);
        }

        if (reacquireClass == 1u)
        {
            if (g_lineTrack.searchSeenTicks < 0xFFu)
                g_lineTrack.searchSeenTicks++;
        }
        else if (reacquireClass == 2u)
        {
            g_lineTrack.searchSeenTicks = TRACK_SEARCH_SIDE_EXIT_TICKS;
        }
        else
        {
            g_lineTrack.searchSeenTicks = 0u;
        }

        if (g_lineTrack.searchSeenTicks >= TRACK_SEARCH_SIDE_EXIT_TICKS)
        {
            g_lineTrack.recoverDir = g_lineTrack.searchDir;
            g_lineTrack.recoverTicks = g_lineTrackCfg.recoverTicks;
            g_lineTrack.trackState = crossActive ? LT_TRACK_CROSS : LT_TRACK_FOLLOW;
            g_lineTrack.searchDir = LT_DIR_NONE;
            g_lineTrack.searchPhase = LT_SEARCH_PHASE_ARC;
            g_lineTrack.searchSeenTicks = 0u;
            g_lineTrack.searchTicks = 0u;
            g_lineTrack.cornerDone = 1u;
        }
        else
        {
            track_progress_search();
        }
    }
    else
    {
        g_lineTrack.trackState = crossActive ? LT_TRACK_CROSS : LT_TRACK_FOLLOW;
    }

    if (g_lineTrack.trackState == LT_TRACK_CROSS)
    {
        g_lineTrack.recoverTicks = 0u;
        g_lineTrack.recoverDir = LT_DIR_NONE;
    }
    else if (!track_is_search_state(g_lineTrack.trackState) && g_lineTrack.recoverTicks > 0u)
    {
        g_lineTrack.recoverTicks--;
        if (g_lineTrack.recoverTicks == 0u)
            g_lineTrack.recoverDir = LT_DIR_NONE;
    }

    if (g_lineTrack.trackState == LT_TRACK_FOLLOW || g_lineTrack.trackState == LT_TRACK_CROSS)
    {
        g_lineTrack.overrunCount = 0u;
        track_update_history(bits, linePos);
    }

    if (g_lineTrack.trackState == LT_TRACK_CROSS)
    {
        linePos = 0;
        bearingDev = 0;
    }

    g_lineTrack.linePos = linePos;
    g_lineTrack.bearingDev = bearingDev;
    g_lineTrack.dbgTrackState = g_lineTrack.trackState;
    g_lineTrack.dbgTurnDir = g_lineTrack.searchDir;
}

static int16_t track_limit_follow_base(int16_t basePwm, int16_t linePos)
{
    int16_t absPos;
    float ratio;

    if (basePwm <= g_lineTrackCfg.baseMinPwm)
        return basePwm;

    absPos = track_abs_i16(linePos);
    if (absPos <= TRACK_LINE_POS_CENTER_MAX)
        return basePwm;
    if (absPos >= TRACK_LINE_POS_LARGE_MAX)
        return g_lineTrackCfg.baseMinPwm;

    ratio = (float)(absPos - TRACK_LINE_POS_CENTER_MAX)
          / (float)(TRACK_LINE_POS_LARGE_MAX - TRACK_LINE_POS_CENTER_MAX);
    return track_round_to_i16(track_lerpf((float)basePwm,
                                          (float)g_lineTrackCfg.baseMinPwm,
                                          ratio));
}

static int16_t track_dev_speed_follow_pd(int16_t driveBase,
                                         float targetError,
                                         float kp,
                                         float kd,
                                         int16_t linePos,
                                         float yawRate)
{
    (void)driveBase;
    (void)linePos;
    (void)yawRate;
    float filteredError;
    float delta;
    float output;
    int16_t devCmd;
    int16_t deltaCmd;

    filteredError = g_lineTrack.filteredTrackError
                  + g_lineTrackCfg.posFilterAlpha * (targetError - g_lineTrack.filteredTrackError);
    delta = filteredError - g_lineTrack.lastFilteredTrackError;
    g_lineTrack.filteredDTerm = g_lineTrack.filteredDTerm
                             + g_lineTrackCfg.dFilterAlpha * (delta - g_lineTrack.filteredDTerm);

    output = kp * filteredError + kd * g_lineTrack.filteredDTerm;
    devCmd = (output >= 0.0f) ? (int16_t)(output + 0.5f) : (int16_t)(output - 0.5f);

    deltaCmd = (int16_t)(devCmd - g_lineTrack.lastDevSpeedCmd);
    if (deltaCmd > g_lineTrackCfg.devStepLimit)
        devCmd = (int16_t)(g_lineTrack.lastDevSpeedCmd + g_lineTrackCfg.devStepLimit);
    else if (deltaCmd < -g_lineTrackCfg.devStepLimit)
        devCmd = (int16_t)(g_lineTrack.lastDevSpeedCmd - g_lineTrackCfg.devStepLimit);

    g_lineTrack.filteredTrackError = filteredError;
    g_lineTrack.lastFilteredTrackError = filteredError;
    g_lineTrack.lastDevSpeedCmd = devCmd;

    return devCmd;
}

static void track_drive_follow(int16_t basePwm, float yawRate)
{
    int16_t driveBase;
    int16_t left;
    int16_t right;
    int16_t devMax;
    float activeKp;
    float activeKd;
    float activeDevRatio;
    uint8_t gainStage;
    float targetError;

    driveBase = basePwm;
    targetError = track_build_follow_error(g_lineTrack.sensorData, g_lineTrack.linePos, &gainStage);
    track_select_follow_profile(g_lineTrack.linePos, &activeKp, &activeKd, &activeDevRatio, &gainStage);
    g_lineTrack.activeKp = activeKp;
    g_lineTrack.activeKd = activeKd;
    g_lineTrack.gainStage = gainStage;
    driveBase = track_limit_follow_base(driveBase, g_lineTrack.linePos);

    if (g_lineTrack.trackState == LT_TRACK_CROSS)
    {
        track_reset_follow_control();
        g_lineTrack.devSpeed = 0;
        g_lineTrack.gainStage = 0u;
    }
    else
    {
        g_lineTrack.devSpeed = track_dev_speed_follow_pd(driveBase,
                                                         targetError,
                                                         activeKp,
                                                         activeKd,
                                                         g_lineTrack.linePos,
                                                         yawRate);
    }

    if (g_lineTrack.trackState == LT_TRACK_FOLLOW && g_lineTrackCfg.staticSteerBias != 0)
        g_lineTrack.devSpeed = (int16_t)(g_lineTrack.devSpeed + g_lineTrackCfg.staticSteerBias);

    devMax = (int16_t)(driveBase * activeDevRatio);
    if (g_lineTrack.devSpeed > devMax)
        g_lineTrack.devSpeed = devMax;
    if (g_lineTrack.devSpeed < -devMax)
        g_lineTrack.devSpeed = -devMax;

    g_lineTrack.lastDevSpeedCmd = g_lineTrack.devSpeed;

    left = (int16_t)(driveBase + g_lineTrack.devSpeed);
    right = (int16_t)(driveBase - g_lineTrack.devSpeed);
    track_motor_forward(left, right);
}

static void track_drive_search(void)
{
    int16_t fastPwm;
    int16_t slowPwm;

    g_lineTrack.devSpeed = 0;
    g_lineTrack.gainStage = 2u;
    g_lineTrack.activeKp = g_lineTrack.kp;
    g_lineTrack.activeKd = g_lineTrack.kd;
    track_reset_follow_control();

    MotorDriver_Enable();
    if (g_lineTrack.searchPhase == LT_SEARCH_PHASE_ARC)
    {
        fastPwm = (int16_t)g_lineTrackCfg.searchArcPwmFast;
        slowPwm = (int16_t)g_lineTrackCfg.searchArcPwmSlow;

        if (g_lineTrack.searchDir == LT_DIR_LEFT)
            MotorDriver_SetTurnPWM(slowPwm, fastPwm);
        else
            MotorDriver_SetTurnPWM(fastPwm, slowPwm);
    }
    else
    {
        fastPwm = (int16_t)g_lineTrackCfg.searchTurnPwmFast;
        slowPwm = (int16_t)g_lineTrackCfg.searchTurnPwmSlow;

        if (g_lineTrack.searchDir == LT_DIR_LEFT)
            MotorDriver_SetTurnPWM((int16_t)(-slowPwm), fastPwm);
        else
            MotorDriver_SetTurnPWM(fastPwm, (int16_t)(-slowPwm));
    }
}

static void track_update_telem_debug(void)
{
    uint8_t bits = g_lineTrack.sensorData;
    uint16_t flags = 0u;
    uint8_t state = LT_TLM_STATE_TRACK;
    uint8_t scoreEnabled = 1u;

    if ((bits & LT_MASK_CENTER) != 0u)
        flags |= LT_TLM_FLAG_CENTER;
    if (bits == 0u)
        flags |= LT_TLM_FLAG_LOST;
    if (g_lineTrack.trackState == LT_TRACK_CROSS)
        flags |= LT_TLM_FLAG_CROSS;
    if (track_is_search_state(g_lineTrack.trackState))
        flags |= LT_TLM_FLAG_SEARCH;
    if (track_is_search_state(g_lineTrack.trackState)
        && g_lineTrack.searchPhase == LT_SEARCH_PHASE_PIVOT)
    {
        flags |= LT_TLM_FLAG_PIVOT;
    }
    if (g_lineTrack.recoverTicks > 0u)
        flags |= LT_TLM_FLAG_TRIM;
    if ((bits & (LT_MASK_LEFT_OUTER | LT_MASK_RIGHT_OUTER)) != 0u)
    {
        flags |= LT_TLM_FLAG_EDGE;
    }

    if (!track_is_search_state(g_lineTrack.trackState)
        && g_lineTrack.trackState != LT_TRACK_CROSS)
    {
        if ((flags & LT_TLM_FLAG_CENTER) != 0u
            && (bits & (LT_MASK_LEFT_OUTER | LT_MASK_RIGHT_OUTER)) == 0u
            && track_abs_i16(g_lineTrack.linePos) <= TRACK_LINE_POS_CENTER_MAX)
        {
            flags |= LT_TLM_FLAG_STRAIGHT;
        }
        else if ((bits & LT_MASK_INNER_GUIDE) != 0u || g_lineTrack.gainStage == 1u)
        {
            flags |= LT_TLM_FLAG_SCURVE;
        }
    }

    if (g_lineTrack.trackState == LT_TRACK_CROSS)
    {
        state = LT_TLM_STATE_CROSS;
        scoreEnabled = 0u;
    }
    else if (g_lineTrack.trackState == LT_TRACK_SEARCH_LEFT)
    {
        state = LT_TLM_STATE_SEARCH_LEFT;
        scoreEnabled = 0u;
    }
    else if (g_lineTrack.trackState == LT_TRACK_SEARCH_RIGHT)
    {
        state = LT_TLM_STATE_SEARCH_RIGHT;
        scoreEnabled = 0u;
    }
    else if (g_lineTrack.recoverTicks > 0u)
    {
        state = (g_lineTrack.recoverDir == LT_DIR_LEFT)
              ? LT_TLM_STATE_TRIM_LEFT
              : LT_TLM_STATE_TRIM_RIGHT;
    }
    else if ((flags & LT_TLM_FLAG_STRAIGHT) != 0u)
    {
        state = LT_TLM_STATE_STRAIGHT;
    }
    else if ((flags & LT_TLM_FLAG_SCURVE) != 0u)
    {
        state = LT_TLM_STATE_SCURVE;
    }
    else if ((flags & LT_TLM_FLAG_EDGE) != 0u)
    {
        state = LT_TLM_STATE_EDGE;
    }

    g_lineTrack.dbgTelemState = state;
    g_lineTrack.dbgScoreEnabled = (bits == 0u) ? 0u : scoreEnabled;
    g_lineTrack.dbgTelemFlags = flags;
}

void LineTrack_Init(void)
{
    track_load_default_runtime_config();
    g_lineTrack.kp = PID_TRACK_LINE_KP;
    g_lineTrack.kd = PID_TRACK_LINE_KD;
    g_lineTrack.activeKp = PID_TRACK_LINE_KP;
    g_lineTrack.activeKd = PID_TRACK_LINE_KD;
    g_lineTrack.gainStage = 1u;
    g_lineTrack.lastData = LT_MASK_CENTER;
    g_lineTrack.lastValidLinePos = 0;
    g_lineTrack.lastTurnDir = LT_DIR_NONE;
    g_lineTrack.searchPhase = LT_SEARCH_PHASE_ARC;
    g_lineTrack.searchSeenTicks = 0u;
    g_lineTrack.crossState = LT_CROSS_READY;
    g_lineTrack.recoverDir = LT_DIR_NONE;
    g_lineTrack.recoverTicks = 0u;
    track_reset_follow_control();
    LineTrack_Stop();
}

void LineTrack_Start(uint8_t crossings)
{
    float kp = g_lineTrack.kp;
    float kd = g_lineTrack.kd;

    (void)crossings;

    g_lineTrack.state = LT_STATE_STARTING;
    g_lineTrack.trackState = LT_TRACK_FOLLOW;
    g_lineTrack.sensorData = 0u;
    g_lineTrack.lastData = LT_MASK_CENTER;
    g_lineTrack.bearingDev = 0;
    g_lineTrack.linePos = 0;
    g_lineTrack.lastValidLinePos = 0;
    g_lineTrack.devSpeed = 0;
    g_lineTrack.filterTimes = 0u;
    g_lineTrack.crossCount = 0u;
    g_lineTrack.crossState = LT_CROSS_READY;
    g_lineTrack.overrunCount = 0u;
    g_lineTrack.lastTurnDir = LT_DIR_NONE;
    g_lineTrack.searchDir = LT_DIR_NONE;
    g_lineTrack.searchPhase = LT_SEARCH_PHASE_ARC;
    g_lineTrack.searchSeenTicks = 0u;
    g_lineTrack.cornerDone = 0u;
    g_lineTrack.recoverDir = LT_DIR_NONE;
    g_lineTrack.recoverTicks = 0u;
    g_lineTrack.searchTicks = 0u;
    g_lineTrack.activeKp = kp;
    g_lineTrack.activeKd = kd;
    g_lineTrack.gainStage = 1u;
    g_lineTrack.dbgTrackState = LT_TRACK_FOLLOW;
    g_lineTrack.dbgTurnDir = LT_DIR_NONE;
    g_lineTrack.dbgCrossActive = 0u;
    g_lineTrack.dbgTelemState = LT_TLM_STATE_TRACK;
    g_lineTrack.dbgScoreEnabled = 1u;
    g_lineTrack.dbgTelemFlags = 0u;
    g_lineTrack.kp = kp;
    g_lineTrack.kd = kd;
    track_reset_follow_control();
}

void LineTrack_Stop(void)
{
    g_lineTrack.state = LT_STATE_IDLE;
    g_lineTrack.trackState = LT_TRACK_FOLLOW;
    g_lineTrack.sensorData = 0u;
    g_lineTrack.bearingDev = 0;
    g_lineTrack.linePos = 0;
    g_lineTrack.lastValidLinePos = 0;
    g_lineTrack.devSpeed = 0;
    g_lineTrack.overrunCount = 0u;
    g_lineTrack.searchDir = LT_DIR_NONE;
    g_lineTrack.searchPhase = LT_SEARCH_PHASE_ARC;
    g_lineTrack.searchSeenTicks = 0u;
    g_lineTrack.cornerDone = 0u;
    g_lineTrack.recoverDir = LT_DIR_NONE;
    g_lineTrack.recoverTicks = 0u;
    g_lineTrack.searchTicks = 0u;
    g_lineTrack.activeKp = g_lineTrack.kp;
    g_lineTrack.activeKd = g_lineTrack.kd;
    g_lineTrack.gainStage = 1u;
    g_lineTrack.dbgTrackState = LT_TRACK_FOLLOW;
    g_lineTrack.dbgTurnDir = LT_DIR_NONE;
    g_lineTrack.dbgCrossActive = 0u;
    g_lineTrack.dbgTelemState = LT_TLM_STATE_TRACK;
    g_lineTrack.dbgScoreEnabled = 0u;
    g_lineTrack.dbgTelemFlags = 0u;
    track_reset_follow_control();
    track_motor_stop();
}

void LineTrack_Update(uint32_t tickMs, int16_t basePwm, float currentYawRate)
{
    (void)tickMs;
    (void)currentYawRate;

    if (g_lineTrack.state == LT_STATE_STARTING)
    {
        g_lineTrack.state = LT_STATE_RUNNING;
        MotorDriver_Enable();
        return;
    }

    if (g_lineTrack.state != LT_STATE_RUNNING)
        return;

    track_signal_update();

    if (g_lineTrack.trackState == LT_TRACK_SEARCH_LEFT
        || g_lineTrack.trackState == LT_TRACK_SEARCH_RIGHT)
    {
        track_drive_search();
    }
    else
    {
        track_drive_follow(basePwm, currentYawRate);
    }

    track_update_telem_debug();
}

uint8_t LineTrack_IsRunning(void)
{
    return (g_lineTrack.state == LT_STATE_RUNNING) ? 1u : 0u;
}

void LineTrack_SetPID(float kp, float kd)
{
    g_lineTrack.kp = kp;
    g_lineTrack.kd = kd;
    g_lineTrack.activeKp = kp;
    g_lineTrack.activeKd = kd;
}

void LineTrack_ResetRuntimeConfig(void)
{
    track_load_default_runtime_config();
    track_reset_follow_control();
}

uint8_t LineTrack_ParamSet(const char *key, float value, float *appliedValue)
{
    float applied = value;
    uint8_t sensorIndex;

    if (key == 0)
        return 0u;

    if (track_parse_sensor_scale_index(key, &sensorIndex))
    {
        applied = track_clamp_paramf(value, 0.40f, 1.80f);
        g_lineTrackCfg.sensorScale[sensorIndex - 1u] = applied;
    }
    else if (strcmp(key, "track.lkp") == 0)
    {
        applied = track_clamp_paramf(value, 0.0f, 80.0f);
        g_lineTrack.kp = applied;
        g_lineTrack.activeKp = applied;
    }
    else if (strcmp(key, "track.lkd") == 0)
    {
        applied = track_clamp_paramf(value, 0.0f, 80.0f);
        g_lineTrack.kd = applied;
        g_lineTrack.activeKd = applied;
    }
    else if (strcmp(key, "track.dev_ratio") == 0)
    {
        applied = track_clamp_paramf(value, 0.0f, 1.00f);
        g_lineTrackCfg.devRatio = applied;
    }
    else if (strcmp(key, "track.static_bias") == 0)
    {
        applied = (float)track_round_to_i16(track_clamp_paramf(value, -120.0f, 120.0f));
        g_lineTrackCfg.staticSteerBias = (int16_t)applied;
    }
    else if (strcmp(key, "track.deadband") == 0)
    {
        applied = track_clamp_paramf(value, 0.0f, 200.0f);
        g_lineTrackCfg.deadband = applied;
    }
    else if (strcmp(key, "track.pos_lpf") == 0)
    {
        applied = track_clamp_paramf(value, 0.05f, 0.95f);
        g_lineTrackCfg.posFilterAlpha = applied;
    }
    else if (strcmp(key, "track.d_lpf") == 0)
    {
        applied = track_clamp_paramf(value, 0.05f, 0.95f);
        g_lineTrackCfg.dFilterAlpha = applied;
    }
    else if (strcmp(key, "track.recover_ticks") == 0)
    {
        applied = (float)track_round_to_u8(track_clamp_paramf(value, 0.0f, 50.0f));
        g_lineTrackCfg.recoverTicks = (uint8_t)applied;
    }
    else if (strcmp(key, "track.search_turn_fast") == 0)
    {
        applied = (float)track_round_to_u16(track_clamp_paramf(value, 0.0f, 600.0f));
        g_lineTrackCfg.searchTurnPwmFast = (uint16_t)applied;
    }
    else if (strcmp(key, "track.search_turn_slow") == 0)
    {
        applied = (float)track_round_to_u16(track_clamp_paramf(value, 0.0f, 600.0f));
        g_lineTrackCfg.searchTurnPwmSlow = (uint16_t)applied;
    }
    else if (strcmp(key, "track.search_timeout") == 0)
    {
        applied = (float)track_round_to_u16(track_clamp_paramf(value, 1.0f, 300.0f));
        g_lineTrackCfg.searchTimeoutTicks = (uint16_t)applied;
    }
    else
    {
        return 0u;
    }

    if (appliedValue != 0)
        *appliedValue = applied;
    if (!track_is_search_state(g_lineTrack.trackState))
        track_reset_follow_control();
    return 1u;
}

uint8_t LineTrack_ParamGet(const char *key, float *value)
{
    uint8_t sensorIndex;

    if (key == 0 || value == 0)
        return 0u;

    if (track_parse_sensor_scale_index(key, &sensorIndex))
        *value = g_lineTrackCfg.sensorScale[sensorIndex - 1u];
    else if (strcmp(key, "track.lkp") == 0)
        *value = g_lineTrack.kp;
    else if (strcmp(key, "track.lkd") == 0)
        *value = g_lineTrack.kd;
    else if (strcmp(key, "track.dev_ratio") == 0)
        *value = g_lineTrackCfg.devRatio;
    else if (strcmp(key, "track.static_bias") == 0)
        *value = (float)g_lineTrackCfg.staticSteerBias;
    else if (strcmp(key, "track.deadband") == 0)
        *value = g_lineTrackCfg.deadband;
    else if (strcmp(key, "track.pos_lpf") == 0)
        *value = g_lineTrackCfg.posFilterAlpha;
    else if (strcmp(key, "track.d_lpf") == 0)
        *value = g_lineTrackCfg.dFilterAlpha;
    else if (strcmp(key, "track.recover_ticks") == 0)
        *value = (float)g_lineTrackCfg.recoverTicks;
    else if (strcmp(key, "track.search_turn_fast") == 0)
        *value = (float)g_lineTrackCfg.searchTurnPwmFast;
    else if (strcmp(key, "track.search_turn_slow") == 0)
        *value = (float)g_lineTrackCfg.searchTurnPwmSlow;
    else if (strcmp(key, "track.search_timeout") == 0)
        *value = (float)g_lineTrackCfg.searchTimeoutTicks;
    else
        return 0u;

    return 1u;
}

void LineTrack_ParamList(char *out, uint16_t outSize)
{
    if (out == 0 || outSize == 0u)
        return;

    snprintf(out, outSize,
             "track.sensor_scale1,track.sensor_scale2,track.sensor_scale3,track.sensor_scale4,"
             "track.sensor_scale5,track.sensor_scale6,track.sensor_scale7,track.sensor_scale8,"
             "track.lkp,track.lkd,track.dev_ratio,track.static_bias,"
             "track.deadband,track.pos_lpf,track.d_lpf,track.recover_ticks,track.search_turn_fast,"
             "track.search_turn_slow,track.search_timeout");
}
