#include "line_track.h"
#include "sensor_fusion.h"
#include "motor_driver.h"
#include <stdio.h>
#include <string.h>

LineTrack_State_t g_lineTrack;
LineTrack_RuntimeConfig_t g_lineTrackCfg;

static const int8_t s_sensorScores[LINE_SENSOR_COUNT] = {
    -7, -5, -3, -1, 1, 3, 5, 7
};

static uint8_t track_is_crossing(uint8_t bits);

static void track_load_default_runtime_config(void)
{
    static const float s_defaultSensorScale[LINE_SENSOR_COUNT] = {
        0.92f, 0.87f, 1.00f, 0.88f, 0.98f, 0.94f, 1.10f, 1.08f
    };
    uint8_t i;

    for (i = 0u; i < LINE_SENSOR_COUNT; i++)
        g_lineTrackCfg.sensorScale[i] = s_defaultSensorScale[i];

    g_lineTrackCfg.centerDirectSmallRatio = TRACK_CENTER_DIRECT_SMALL_RATIO;
    g_lineTrackCfg.centerDirectSmallMin = TRACK_CENTER_DIRECT_SMALL_MIN;
    g_lineTrackCfg.centerDirectMidRatio = TRACK_CENTER_DIRECT_MID_RATIO;
    g_lineTrackCfg.centerDirectMidMin = TRACK_CENTER_DIRECT_MID_MIN;
    g_lineTrackCfg.edgeDirectRatio = TRACK_EDGE_DIRECT_RATIO;
    g_lineTrackCfg.edgeDirectMin = TRACK_EDGE_DIRECT_MIN;
    g_lineTrackCfg.recenterDecayStep = TRACK_RECENTER_DECAY_STEP;
    g_lineTrackCfg.staticSteerBias = TRACK_STATIC_STEER_BIAS;
    g_lineTrackCfg.centerDeadband = TRACK_CTRL_CENTER_DEADBAND;
    g_lineTrackCfg.posFilterAlpha = TRACK_CTRL_POS_FILTER_ALPHA;
    g_lineTrackCfg.dFilterAlpha = TRACK_CTRL_D_FILTER_ALPHA;
    g_lineTrackCfg.offcenterBoost = TRACK_CTRL_OFFCENTER_BOOST;
    g_lineTrackCfg.centerSingleHoldTicks = TRACK_CENTER_SINGLE_HOLD_TICKS;
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

static float track_clampf(float value, float minValue, float maxValue)
{
    if (value < minValue)
        return minValue;
    if (value > maxValue)
        return maxValue;
    return value;
}

static uint8_t track_is_search_state(uint8_t state)
{
    return (state == LT_TRACK_SEARCH_LEFT || state == LT_TRACK_SEARCH_RIGHT) ? 1u : 0u;
}

static int16_t track_abs_i16(int16_t value)
{
    return (value >= 0) ? value : (int16_t)(-value);
}

static int16_t track_max_i16(int16_t a, int16_t b)
{
    return (a >= b) ? a : b;
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
    g_lineTrack.lastBearingDev = 0;
    g_lineTrack.lastDevSpeedCmd = 0;
    g_lineTrack.filteredLinePos = 0.0f;
    g_lineTrack.ctrlError = 0.0f;
    g_lineTrack.lastCtrlError = 0.0f;
    g_lineTrack.filteredDTerm = 0.0f;
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

static void track_sync_direct_control(int16_t devCmd)
{
    g_lineTrack.filteredLinePos = (float)g_lineTrack.linePos;
    g_lineTrack.ctrlError = 0.0f;
    g_lineTrack.lastCtrlError = 0.0f;
    g_lineTrack.filteredDTerm = 0.0f;
    g_lineTrack.lastDevSpeedCmd = devCmd;
    g_lineTrack.lastBearingDev = g_lineTrack.bearingDev;
}

static void track_reset_center_clamp(void)
{
    g_lineTrack.centerHoldDir = LT_DIR_NONE;
    g_lineTrack.centerHoldTicks = 0u;
}

static void track_reset_recenter_latch(void)
{
    g_lineTrack.recenterDir = LT_DIR_NONE;
    g_lineTrack.recenterDevCmd = 0;
}

static int16_t track_apply_recenter_decay(int16_t currentCmd, int16_t targetCmd)
{
    int16_t currentAbs;
    int16_t targetAbs;
    int16_t step;

    if (targetCmd == 0)
        return 0;

    currentAbs = track_abs_i16(currentCmd);
    targetAbs = track_abs_i16(targetCmd);
    step = g_lineTrackCfg.recenterDecayStep;
    if (step <= 0)
        step = 1;

    if (currentCmd == 0
        || ((currentCmd > 0) != (targetCmd > 0))
        || targetAbs >= currentAbs)
    {
        return targetCmd;
    }

    if ((currentAbs - targetAbs) <= step)
        return targetCmd;

    currentAbs = (int16_t)(currentAbs - step);
    return (targetCmd > 0) ? currentAbs : (int16_t)(-currentAbs);
}

static uint8_t track_get_progressive_recenter_target(uint8_t bits,
                                                     uint8_t dir,
                                                     int16_t smallDev,
                                                     int16_t midDev,
                                                     int16_t edgeDev,
                                                     int16_t *targetDev,
                                                     uint8_t *stage,
                                                     float *devRatio)
{
    int16_t outerDev;

    if (targetDev == 0 || stage == 0 || devRatio == 0)
        return 0u;

    outerDev = track_max_i16((int16_t)((edgeDev + midDev) / 2), (int16_t)(midDev + 8));

    if (dir == LT_DIR_LEFT)
    {
        if ((bits & (LT_BIT_S5 | LT_MASK_RIGHT_ZONE)) != 0u)
            return 0u;
        if ((bits & LT_BIT_S1) != 0u)
        {
            *targetDev = (int16_t)(-edgeDev);
            *stage = 2u;
            *devRatio = TRACK_EDGE_DEV_RATIO;
            return 1u;
        }
        if ((bits & LT_BIT_S2) != 0u)
        {
            *targetDev = (int16_t)(-outerDev);
            *stage = 2u;
            *devRatio = TRACK_EDGE_DEV_RATIO;
            return 1u;
        }
        if ((bits & LT_BIT_S3) != 0u)
        {
            *targetDev = (int16_t)(-midDev);
            *stage = 1u;
            *devRatio = TRACK_MID_DEV_RATIO;
            return 1u;
        }
        if (((bits & LT_MASK_CENTER) == LT_BIT_S4))
        {
            *targetDev = (int16_t)(-smallDev);
            *stage = 0u;
            *devRatio = TRACK_CENTER_DEV_RATIO;
            return 1u;
        }
        return 0u;
    }

    if (dir == LT_DIR_RIGHT)
    {
        if ((bits & (LT_BIT_S4 | LT_MASK_LEFT_ZONE)) != 0u)
            return 0u;
        if ((bits & LT_BIT_S8) != 0u)
        {
            *targetDev = edgeDev;
            *stage = 2u;
            *devRatio = TRACK_EDGE_DEV_RATIO;
            return 1u;
        }
        if ((bits & LT_BIT_S7) != 0u)
        {
            *targetDev = outerDev;
            *stage = 2u;
            *devRatio = TRACK_EDGE_DEV_RATIO;
            return 1u;
        }
        if ((bits & LT_BIT_S6) != 0u)
        {
            *targetDev = midDev;
            *stage = 1u;
            *devRatio = TRACK_MID_DEV_RATIO;
            return 1u;
        }
        if (((bits & LT_MASK_CENTER) == LT_BIT_S5))
        {
            *targetDev = smallDev;
            *stage = 0u;
            *devRatio = TRACK_CENTER_DEV_RATIO;
            return 1u;
        }
        return 0u;
    }

    return 0u;
}

static uint8_t track_try_progressive_recenter_drive(uint8_t bits,
                                                    int16_t smallDev,
                                                    int16_t midDev,
                                                    int16_t edgeDev,
                                                    int16_t *devCmd,
                                                    uint8_t *stage,
                                                    float *devRatio)
{
    uint8_t dir;
    int16_t targetDev;
    uint8_t targetStage;
    float targetRatio;

    if (devCmd == 0 || stage == 0 || devRatio == 0)
        return 0u;

    if (bits == 0u || track_is_crossing(bits))
    {
        track_reset_recenter_latch();
        return 0u;
    }

    if ((bits & LT_MASK_CENTER) == LT_MASK_CENTER
        && (bits & (LT_MASK_LEFT_ZONE | LT_MASK_RIGHT_ZONE)) == 0u)
    {
        track_reset_recenter_latch();
        return 0u;
    }

    dir = g_lineTrack.recenterDir;
    if (dir == LT_DIR_NONE)
    {
        if ((bits & (LT_BIT_S1 | LT_BIT_S2 | LT_BIT_S3)) != 0u
            && (bits & (LT_BIT_S5 | LT_BIT_S6 | LT_BIT_S7 | LT_BIT_S8)) == 0u)
        {
            dir = LT_DIR_LEFT;
        }
        else if ((bits & (LT_BIT_S6 | LT_BIT_S7 | LT_BIT_S8)) != 0u
                 && (bits & (LT_BIT_S1 | LT_BIT_S2 | LT_BIT_S3 | LT_BIT_S4)) == 0u)
        {
            dir = LT_DIR_RIGHT;
        }
        else
        {
            return 0u;
        }
    }

    if (!track_get_progressive_recenter_target(bits,
                                               dir,
                                               smallDev,
                                               midDev,
                                               edgeDev,
                                               &targetDev,
                                               &targetStage,
                                               &targetRatio))
    {
        track_reset_recenter_latch();
        return 0u;
    }

    g_lineTrack.recenterDir = dir;
    g_lineTrack.recenterDevCmd = track_apply_recenter_decay(g_lineTrack.recenterDevCmd, targetDev);
    *devCmd = g_lineTrack.recenterDevCmd;
    *stage = targetStage;
    *devRatio = targetRatio;
    return 1u;
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
    float sum = 0.0f;
    uint8_t count = 0u;
    uint8_t i;

    if (bits == 0u)
        return 0;

    if (track_is_crossing(bits))
        return 0;

    for (i = 0u; i < LINE_SENSOR_COUNT; i++)
    {
        if (bits & (1u << i))
        {
            sum += ((float)s_sensorScores[i] * g_lineTrackCfg.sensorScale[i]) * (float)TRACK_LINE_POS_UNIT;
            count++;
        }
    }

    if (count == 0u)
        return 0;

    return track_round_to_i16(sum / (float)count);
}

static int16_t track_apply_center_clamp(uint8_t bits, int16_t rawLinePos)
{
    uint8_t centerBits;
    uint8_t hasLeftInner;
    uint8_t hasRightInner;
    uint8_t hasOuter;
    uint8_t dir;

    if (bits == 0u || track_is_crossing(bits))
    {
        track_reset_center_clamp();
        track_reset_recenter_latch();
        return rawLinePos;
    }

    centerBits = (uint8_t)(bits & LT_MASK_CENTER);
    hasLeftInner = (bits & LT_BIT_S3) ? 1u : 0u;
    hasRightInner = (bits & LT_BIT_S6) ? 1u : 0u;
    hasOuter = (bits & (LT_MASK_LEFT_OUTER | LT_MASK_RIGHT_OUTER)) ? 1u : 0u;

    if (centerBits == LT_MASK_CENTER && !hasLeftInner && !hasRightInner)
    {
        track_reset_center_clamp();
        return 0;
    }

    if (centerBits != 0u && !hasLeftInner && !hasRightInner && !hasOuter)
    {
        dir = (centerBits == LT_BIT_S4) ? LT_DIR_LEFT : LT_DIR_RIGHT;

        if (g_lineTrack.centerHoldDir == dir)
        {
            if (g_lineTrack.centerHoldTicks < 255u)
                g_lineTrack.centerHoldTicks++;
        }
        else
        {
            g_lineTrack.centerHoldDir = dir;
            g_lineTrack.centerHoldTicks = 1u;
        }

        if (g_lineTrack.centerHoldTicks < g_lineTrackCfg.centerSingleHoldTicks)
            return 0;

        return (dir == LT_DIR_LEFT) ? -TRACK_CENTER_SINGLE_POS : TRACK_CENTER_SINGLE_POS;
    }

    track_reset_center_clamp();

    if (hasLeftInner && !hasRightInner)
    {
        if (rawLinePos > -TRACK_CENTER_INNER_MIN_POS)
            rawLinePos = -TRACK_CENTER_INNER_MIN_POS;
        rawLinePos = (int16_t)(rawLinePos * TRACK_CENTER_INNER_BOOST);
    }
    else if (hasRightInner && !hasLeftInner)
    {
        if (rawLinePos < TRACK_CENTER_INNER_MIN_POS)
            rawLinePos = TRACK_CENTER_INNER_MIN_POS;
        rawLinePos = (int16_t)(rawLinePos * TRACK_CENTER_INNER_BOOST);
    }

    return rawLinePos;
}

static uint8_t track_try_center_direct_drive(uint8_t bits,
                                             int16_t driveBase,
                                             int16_t *devCmd,
                                             uint8_t *stage,
                                             float *devRatio)
{
    uint8_t centerBits;
    uint8_t hasCenter;
    uint8_t hasLeftInner;
    uint8_t hasRightInner;
    uint8_t hasLeftOuter;
    uint8_t hasRightOuter;
    uint8_t hasOuter;
    int16_t smallDev;
    int16_t midDev;
    int16_t edgeDev;
    uint8_t dir;

    if (devCmd == 0 || stage == 0 || devRatio == 0)
        return 0u;

    if (bits == 0u || track_is_crossing(bits))
    {
        track_reset_recenter_latch();
        return 0u;
    }

    centerBits = (uint8_t)(bits & LT_MASK_CENTER);
    hasCenter = (centerBits != 0u) ? 1u : 0u;
    hasLeftInner = (bits & LT_BIT_S3) ? 1u : 0u;
    hasRightInner = (bits & LT_BIT_S6) ? 1u : 0u;
    hasLeftOuter = (bits & LT_MASK_LEFT_OUTER) ? 1u : 0u;
    hasRightOuter = (bits & LT_MASK_RIGHT_OUTER) ? 1u : 0u;
    hasOuter = (hasLeftOuter || hasRightOuter) ? 1u : 0u;

    smallDev = track_max_i16((int16_t)(driveBase * g_lineTrackCfg.centerDirectSmallRatio),
                             g_lineTrackCfg.centerDirectSmallMin);
    midDev = track_max_i16((int16_t)(driveBase * g_lineTrackCfg.centerDirectMidRatio),
                           g_lineTrackCfg.centerDirectMidMin);
    edgeDev = track_max_i16((int16_t)(driveBase * g_lineTrackCfg.edgeDirectRatio),
                            g_lineTrackCfg.edgeDirectMin);

    if (centerBits == LT_MASK_CENTER && !hasLeftInner && !hasRightInner)
    {
        *devCmd = 0;
        *stage = 0u;
        *devRatio = TRACK_CENTER_DEV_RATIO;
        track_reset_center_clamp();
        track_reset_recenter_latch();
        return 1u;
    }

    if (track_try_progressive_recenter_drive(bits,
                                             smallDev,
                                             midDev,
                                             edgeDev,
                                             devCmd,
                                             stage,
                                             devRatio))
    {
        track_reset_center_clamp();
        return 1u;
    }

    if (centerBits != 0u && !hasLeftInner && !hasRightInner)
    {
        dir = (centerBits == LT_BIT_S4) ? LT_DIR_LEFT : LT_DIR_RIGHT;

        if (g_lineTrack.centerHoldDir == dir)
        {
            if (g_lineTrack.centerHoldTicks < 255u)
                g_lineTrack.centerHoldTicks++;
        }
        else
        {
            g_lineTrack.centerHoldDir = dir;
            g_lineTrack.centerHoldTicks = 1u;
        }

        *devCmd = (g_lineTrack.centerHoldTicks < g_lineTrackCfg.centerSingleHoldTicks)
                ? 0
                : ((dir == LT_DIR_LEFT) ? (int16_t)(-smallDev) : smallDev);
        *stage = 0u;
        *devRatio = TRACK_CENTER_DEV_RATIO;
        track_reset_recenter_latch();
        return 1u;
    }

    track_reset_center_clamp();
    track_reset_recenter_latch();

    if (hasLeftOuter && !hasRightOuter && !hasCenter)
    {
        *devCmd = (int16_t)(-edgeDev);
        *stage = 2u;
        *devRatio = TRACK_EDGE_DEV_RATIO;
        return 1u;
    }

    if (hasRightOuter && !hasLeftOuter && !hasCenter)
    {
        *devCmd = edgeDev;
        *stage = 2u;
        *devRatio = TRACK_EDGE_DEV_RATIO;
        return 1u;
    }

    if (hasLeftInner && !hasRightInner && !hasOuter && (hasCenter || (bits == LT_BIT_S3)))
    {
        *devCmd = (int16_t)(-midDev);
        *stage = 1u;
        *devRatio = TRACK_MID_DEV_RATIO;
        return 1u;
    }

    if (hasRightInner && !hasLeftInner && !hasOuter && (hasCenter || (bits == LT_BIT_S6)))
    {
        *devCmd = midDev;
        *stage = 1u;
        *devRatio = TRACK_MID_DEV_RATIO;
        return 1u;
    }

    return 0u;
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

static void track_select_follow_profile(uint8_t absBearing,
                                        float *kp,
                                        float *kd,
                                        float *devRatio,
                                        uint8_t *stage)
{
    float localKp = g_lineTrack.kp;
    float localKd = g_lineTrack.kd;
    float localDevRatio = TRACK_DEV_MAX_RATIO;
    uint8_t localStage = 1u;

#if (TRACK_DYNAMIC_PID_ENABLE != 0)
    if (absBearing <= TRACK_GAIN_CENTER_MAX_DEV)
    {
        localKp *= TRACK_CENTER_KP_SCALE;
        localKd *= TRACK_CENTER_KD_SCALE;
        localDevRatio = TRACK_CENTER_DEV_RATIO;
        localStage = 0u;
    }
    else if (absBearing <= TRACK_GAIN_MID_MAX_DEV)
    {
        localKp *= TRACK_MID_KP_SCALE;
        localKd *= TRACK_MID_KD_SCALE;
        localDevRatio = TRACK_MID_DEV_RATIO;
        localStage = 1u;
    }
    else
    {
        localKp *= TRACK_EDGE_KP_SCALE;
        localKd *= TRACK_EDGE_KD_SCALE;
        localDevRatio = TRACK_EDGE_DEV_RATIO;
        localStage = 2u;
    }
#endif

    if (kp != 0) *kp = localKp;
    if (kd != 0) *kd = localKd;
    if (devRatio != 0) *devRatio = localDevRatio;
    if (stage != 0) *stage = localStage;
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

    track_reset_recenter_latch();
    track_apply_search_dir(dir);
    g_lineTrack.searchPhase = LT_SEARCH_PHASE_PIVOT;
    g_lineTrack.searchTicks = 0u;
}

static uint8_t track_search_exit_ready(uint8_t bits, uint8_t dir)
{
    (void)dir;

    if (bits == 0u)
        return 0u;
    if (track_is_crossing(bits))
        return 1u;
    if (bits & LT_MASK_CENTER)
        return 1u;

    return ((bits & (LT_MASK_LEFT_ZONE | LT_MASK_RIGHT_ZONE)) != 0u) ? 1u : 0u;
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

            if (g_lineTrack.crossing > 0u && g_lineTrack.crossCount >= g_lineTrack.crossing)
                g_lineTrack.autoFlag = LT_FLAG_STOP;
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
            g_lineTrack.linePos = (g_lineTrack.searchDir == LT_DIR_LEFT) ? -350 : 350;
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
                g_lineTrack.linePos = (dir == LT_DIR_LEFT) ? -350 : 350;
                g_lineTrack.bearingDev = (dir == LT_DIR_LEFT) ? -7 : 7;
            }
        }

        g_lineTrack.dbgTrackState = g_lineTrack.trackState;
        return;
    }

    linePos = track_calculate_line_pos(bits);
    linePos = track_apply_center_clamp(bits, linePos);
    bearingDev = track_map_bearing_dev(linePos);

    if (track_is_search_state(g_lineTrack.trackState))
    {
        if (g_lineTrack.searchTicks > TRACK_SEARCH_BLIND_TICKS
            && track_search_exit_ready(bits, g_lineTrack.searchDir))
        {
            g_lineTrack.recoverDir = g_lineTrack.searchDir;
            g_lineTrack.recoverTicks = g_lineTrackCfg.recoverTicks;
            g_lineTrack.trackState = crossActive ? LT_TRACK_CROSS : LT_TRACK_FOLLOW;
            g_lineTrack.searchDir = LT_DIR_NONE;
            g_lineTrack.searchPhase = LT_SEARCH_PHASE_ARC;
            g_lineTrack.searchTicks = 0u;
            g_lineTrack.cornerDone = 1u;
            g_lineTrack.lastBearingDev = 0;
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

static float track_line_pos_to_ctrl_error(float linePos)
{
    float absPos;
    float sign;
    float boundaryError;
    float error;

    absPos = track_absf(linePos);
    sign = (linePos >= 0.0f) ? 1.0f : -1.0f;
    boundaryError = (g_lineTrackCfg.centerDeadband / TRACK_CTRL_ERROR_SCALE) * TRACK_CTRL_CENTER_SLOPE_RATIO;

    if (absPos <= g_lineTrackCfg.centerDeadband)
    {
        error = (absPos / TRACK_CTRL_ERROR_SCALE) * TRACK_CTRL_CENTER_SLOPE_RATIO;
        return track_clampf(sign * error, -7.0f, 7.0f);
    }

    absPos -= g_lineTrackCfg.centerDeadband;
    error = boundaryError + (absPos / TRACK_CTRL_ERROR_SCALE);

    if ((g_lineTrack.sensorData & LT_MASK_CENTER) == 0u)
        error *= g_lineTrackCfg.offcenterBoost;

    return track_clampf(sign * error, -7.0f, 7.0f);
}

static int16_t track_dev_speed_pid(int16_t rawLinePos, float kp, float kd)
{
    float filteredPos;
    float ctrlError;
    float delta;
    float output;
    int16_t devCmd;
    int16_t deltaCmd;

    filteredPos = g_lineTrack.filteredLinePos
                + g_lineTrackCfg.posFilterAlpha * ((float)rawLinePos - g_lineTrack.filteredLinePos);
    ctrlError = track_line_pos_to_ctrl_error(filteredPos);

    delta = ctrlError - g_lineTrack.lastCtrlError;
    g_lineTrack.filteredDTerm = g_lineTrack.filteredDTerm
                             + g_lineTrackCfg.dFilterAlpha * (delta - g_lineTrack.filteredDTerm);

    output = kp * ctrlError + kd * g_lineTrack.filteredDTerm;
    devCmd = (output >= 0.0f) ? (int16_t)(output + 0.5f) : (int16_t)(output - 0.5f);

    deltaCmd = (int16_t)(devCmd - g_lineTrack.lastDevSpeedCmd);
    if (deltaCmd > TRACK_CTRL_DEV_STEP_LIMIT)
        devCmd = (int16_t)(g_lineTrack.lastDevSpeedCmd + TRACK_CTRL_DEV_STEP_LIMIT);
    else if (deltaCmd < -TRACK_CTRL_DEV_STEP_LIMIT)
        devCmd = (int16_t)(g_lineTrack.lastDevSpeedCmd - TRACK_CTRL_DEV_STEP_LIMIT);

    g_lineTrack.filteredLinePos = filteredPos;
    g_lineTrack.ctrlError = ctrlError;
    g_lineTrack.lastCtrlError = ctrlError;
    g_lineTrack.lastDevSpeedCmd = devCmd;
    g_lineTrack.lastBearingDev = g_lineTrack.bearingDev;

    return devCmd;
}

static void track_drive_follow(int16_t basePwm)
{
    int16_t driveBase;
    int16_t left;
    int16_t right;
    int16_t devMax;
    int16_t absBearing;
    int16_t directDevCmd;
    float activeKp;
    float activeKd;
    float activeDevRatio;
    uint8_t gainStage;

    driveBase = basePwm;
    absBearing = (g_lineTrack.bearingDev >= 0) ? g_lineTrack.bearingDev : (int16_t)(-g_lineTrack.bearingDev);
    track_select_follow_profile((uint8_t)absBearing, &activeKp, &activeKd, &activeDevRatio, &gainStage);
    g_lineTrack.activeKp = activeKp;
    g_lineTrack.activeKd = activeKd;
    g_lineTrack.activeDevRatio = activeDevRatio;
    g_lineTrack.gainStage = gainStage;

    if (absBearing >= TRACK_EDGE_BEARING_MIN && driveBase > TRACK_EDGE_BASE_PWM_MAX)
        driveBase = TRACK_EDGE_BASE_PWM_MAX;

    if (g_lineTrack.trackState == LT_TRACK_CROSS)
    {
        g_lineTrack.directMode = LT_DIRECT_NONE;
        track_reset_follow_control();
        g_lineTrack.devSpeed = 0;
        g_lineTrack.gainStage = 0u;
    }
    else if (track_try_center_direct_drive(g_lineTrack.sensorData,
                                           driveBase,
                                           &directDevCmd,
                                           &gainStage,
                                           &activeDevRatio))
    {
        g_lineTrack.directMode = (gainStage == 0u) ? LT_DIRECT_CENTER
                                : ((gainStage == 1u) ? LT_DIRECT_INNER : LT_DIRECT_EDGE);
        g_lineTrack.devSpeed = directDevCmd;
        g_lineTrack.activeKp = 0.0f;
        g_lineTrack.activeKd = 0.0f;
        g_lineTrack.activeDevRatio = activeDevRatio;
        g_lineTrack.gainStage = gainStage;
        track_sync_direct_control(g_lineTrack.devSpeed);
    }
    else
    {
        g_lineTrack.directMode = LT_DIRECT_NONE;
        g_lineTrack.devSpeed = track_dev_speed_pid(g_lineTrack.linePos, activeKp, activeKd);
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
    g_lineTrack.activeDevRatio = TRACK_EDGE_DEV_RATIO;
    g_lineTrack.directMode = LT_DIRECT_NONE;
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

    if ((bits & LT_MASK_CENTER) != 0u)
        flags |= LT_TLM_FLAG_CENTER;
    if (bits == 0u)
        flags |= LT_TLM_FLAG_LOST;
    if (g_lineTrack.directMode != LT_DIRECT_NONE)
        flags |= LT_TLM_FLAG_DIRECT;
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
    if (g_lineTrack.directMode == LT_DIRECT_EDGE
        || (bits & (LT_MASK_LEFT_OUTER | LT_MASK_RIGHT_OUTER)) != 0u)
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
        else if ((bits & (LT_BIT_S3 | LT_BIT_S6)) != 0u || g_lineTrack.gainStage == 1u)
        {
            flags |= LT_TLM_FLAG_SCURVE;
        }
    }

    if (g_lineTrack.trackState == LT_TRACK_CROSS)
    {
        state = LT_TLM_STATE_CROSS;
    }
    else if (g_lineTrack.trackState == LT_TRACK_SEARCH_LEFT)
    {
        state = LT_TLM_STATE_SEARCH_LEFT;
    }
    else if (g_lineTrack.trackState == LT_TRACK_SEARCH_RIGHT)
    {
        state = LT_TLM_STATE_SEARCH_RIGHT;
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
    g_lineTrack.dbgTelemFlags = flags;
}

void LineTrack_Init(void)
{
    track_load_default_runtime_config();
    g_lineTrack.kp = PID_TRACK_LINE_KP;
    g_lineTrack.kd = PID_TRACK_LINE_KD;
    g_lineTrack.activeKp = PID_TRACK_LINE_KP;
    g_lineTrack.activeKd = PID_TRACK_LINE_KD;
    g_lineTrack.activeDevRatio = TRACK_DEV_MAX_RATIO;
    g_lineTrack.gainStage = 1u;
    g_lineTrack.lastData = LT_MASK_CENTER;
    g_lineTrack.lastValidLinePos = 0;
    g_lineTrack.lastTurnDir = LT_DIR_NONE;
    g_lineTrack.searchPhase = LT_SEARCH_PHASE_ARC;
    g_lineTrack.centerHoldDir = LT_DIR_NONE;
    g_lineTrack.centerHoldTicks = 0u;
    g_lineTrack.recenterDir = LT_DIR_NONE;
    g_lineTrack.recenterDevCmd = 0;
    g_lineTrack.crossState = LT_CROSS_READY;
    g_lineTrack.directMode = LT_DIRECT_NONE;
    g_lineTrack.recoverDir = LT_DIR_NONE;
    g_lineTrack.recoverTicks = 0u;
    track_reset_follow_control();
    LineTrack_Stop();
}

void LineTrack_Start(uint8_t crossings)
{
    float kp = g_lineTrack.kp;
    float kd = g_lineTrack.kd;

    g_lineTrack.state = LT_STATE_STARTING;
    g_lineTrack.autoFlag = LT_FLAG_START;
    g_lineTrack.trackState = LT_TRACK_FOLLOW;
    g_lineTrack.sensorData = 0u;
    g_lineTrack.lastData = LT_MASK_CENTER;
    g_lineTrack.bearingDev = 0;
    g_lineTrack.linePos = 0;
    g_lineTrack.lastValidLinePos = 0;
    g_lineTrack.devSpeed = 0;
    g_lineTrack.filterTimes = 0u;
    g_lineTrack.crossing = crossings;
    g_lineTrack.crossCount = 0u;
    g_lineTrack.crossState = LT_CROSS_READY;
    g_lineTrack.overrunCount = 0u;
    g_lineTrack.lastTurnDir = LT_DIR_NONE;
    g_lineTrack.searchDir = LT_DIR_NONE;
    g_lineTrack.searchPhase = LT_SEARCH_PHASE_ARC;
    g_lineTrack.centerHoldDir = LT_DIR_NONE;
    g_lineTrack.centerHoldTicks = 0u;
    g_lineTrack.recenterDir = LT_DIR_NONE;
    g_lineTrack.recenterDevCmd = 0;
    g_lineTrack.cornerDone = 0u;
    g_lineTrack.directMode = LT_DIRECT_NONE;
    g_lineTrack.recoverDir = LT_DIR_NONE;
    g_lineTrack.recoverTicks = 0u;
    g_lineTrack.searchTicks = 0u;
    g_lineTrack.activeKp = kp;
    g_lineTrack.activeKd = kd;
    g_lineTrack.activeDevRatio = TRACK_DEV_MAX_RATIO;
    g_lineTrack.gainStage = 1u;
    g_lineTrack.dbgTrackState = LT_TRACK_FOLLOW;
    g_lineTrack.dbgTurnDir = LT_DIR_NONE;
    g_lineTrack.dbgCrossActive = 0u;
    g_lineTrack.dbgTelemState = LT_TLM_STATE_TRACK;
    g_lineTrack.dbgTelemFlags = 0u;
    g_lineTrack.kp = kp;
    g_lineTrack.kd = kd;
    track_reset_follow_control();
}

void LineTrack_Stop(void)
{
    g_lineTrack.state = LT_STATE_IDLE;
    g_lineTrack.autoFlag = LT_FLAG_STOP;
    g_lineTrack.trackState = LT_TRACK_FOLLOW;
    g_lineTrack.sensorData = 0u;
    g_lineTrack.bearingDev = 0;
    g_lineTrack.linePos = 0;
    g_lineTrack.lastValidLinePos = 0;
    g_lineTrack.devSpeed = 0;
    g_lineTrack.overrunCount = 0u;
    g_lineTrack.searchDir = LT_DIR_NONE;
    g_lineTrack.searchPhase = LT_SEARCH_PHASE_ARC;
    g_lineTrack.centerHoldDir = LT_DIR_NONE;
    g_lineTrack.centerHoldTicks = 0u;
    g_lineTrack.recenterDir = LT_DIR_NONE;
    g_lineTrack.recenterDevCmd = 0;
    g_lineTrack.cornerDone = 0u;
    g_lineTrack.directMode = LT_DIRECT_NONE;
    g_lineTrack.recoverDir = LT_DIR_NONE;
    g_lineTrack.recoverTicks = 0u;
    g_lineTrack.searchTicks = 0u;
    g_lineTrack.activeKp = g_lineTrack.kp;
    g_lineTrack.activeKd = g_lineTrack.kd;
    g_lineTrack.activeDevRatio = TRACK_DEV_MAX_RATIO;
    g_lineTrack.gainStage = 1u;
    g_lineTrack.dbgTrackState = LT_TRACK_FOLLOW;
    g_lineTrack.dbgTurnDir = LT_DIR_NONE;
    g_lineTrack.dbgCrossActive = 0u;
    g_lineTrack.dbgTelemState = LT_TLM_STATE_TRACK;
    g_lineTrack.dbgTelemFlags = 0u;
    track_reset_follow_control();
    track_motor_stop();
}

void LineTrack_Update(uint32_t tickMs, int16_t basePwm, float currentYaw)
{
    (void)tickMs;
    (void)currentYaw;

    if (g_lineTrack.state == LT_STATE_STARTING)
    {
        g_lineTrack.state = LT_STATE_RUNNING;
        MotorDriver_Enable();
        return;
    }

    if (g_lineTrack.state != LT_STATE_RUNNING)
        return;

    track_signal_update();

    if (g_lineTrack.autoFlag == LT_FLAG_STOP)
    {
        g_lineTrack.state = LT_STATE_IDLE;
        track_motor_stop();
        return;
    }

    if (g_lineTrack.trackState == LT_TRACK_SEARCH_LEFT
        || g_lineTrack.trackState == LT_TRACK_SEARCH_RIGHT)
    {
        track_drive_search();
    }
    else
    {
        track_drive_follow(basePwm);
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
    track_reset_center_clamp();
    track_reset_recenter_latch();
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
    else if (strcmp(key, "track.center_small_ratio") == 0)
    {
        applied = track_clamp_paramf(value, 0.0f, 1.00f);
        g_lineTrackCfg.centerDirectSmallRatio = applied;
    }
    else if (strcmp(key, "track.center_small_min") == 0)
    {
        applied = (float)track_round_to_i16(track_clamp_paramf(value, 0.0f, 240.0f));
        g_lineTrackCfg.centerDirectSmallMin = (int16_t)applied;
    }
    else if (strcmp(key, "track.center_mid_ratio") == 0)
    {
        applied = track_clamp_paramf(value, 0.0f, 1.20f);
        g_lineTrackCfg.centerDirectMidRatio = applied;
    }
    else if (strcmp(key, "track.center_mid_min") == 0)
    {
        applied = (float)track_round_to_i16(track_clamp_paramf(value, 0.0f, 320.0f));
        g_lineTrackCfg.centerDirectMidMin = (int16_t)applied;
    }
    else if (strcmp(key, "track.edge_ratio") == 0)
    {
        applied = track_clamp_paramf(value, 0.0f, 1.20f);
        g_lineTrackCfg.edgeDirectRatio = applied;
    }
    else if (strcmp(key, "track.edge_min") == 0)
    {
        applied = (float)track_round_to_i16(track_clamp_paramf(value, 0.0f, 360.0f));
        g_lineTrackCfg.edgeDirectMin = (int16_t)applied;
    }
    else if (strcmp(key, "track.recenter_decay") == 0)
    {
        applied = (float)track_round_to_i16(track_clamp_paramf(value, 1.0f, 120.0f));
        g_lineTrackCfg.recenterDecayStep = (int16_t)applied;
    }
    else if (strcmp(key, "track.static_bias") == 0)
    {
        applied = (float)track_round_to_i16(track_clamp_paramf(value, -120.0f, 120.0f));
        g_lineTrackCfg.staticSteerBias = (int16_t)applied;
    }
    else if (strcmp(key, "track.center_deadband") == 0)
    {
        applied = track_clamp_paramf(value, 0.0f, 200.0f);
        g_lineTrackCfg.centerDeadband = applied;
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
    else if (strcmp(key, "track.offcenter_boost") == 0)
    {
        applied = track_clamp_paramf(value, 0.80f, 1.80f);
        g_lineTrackCfg.offcenterBoost = applied;
    }
    else if (strcmp(key, "track.center_hold_ticks") == 0)
    {
        applied = (float)track_round_to_u8(track_clamp_paramf(value, 0.0f, 10.0f));
        g_lineTrackCfg.centerSingleHoldTicks = (uint8_t)applied;
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
    else if (strcmp(key, "track.center_small_ratio") == 0)
        *value = g_lineTrackCfg.centerDirectSmallRatio;
    else if (strcmp(key, "track.center_small_min") == 0)
        *value = (float)g_lineTrackCfg.centerDirectSmallMin;
    else if (strcmp(key, "track.center_mid_ratio") == 0)
        *value = g_lineTrackCfg.centerDirectMidRatio;
    else if (strcmp(key, "track.center_mid_min") == 0)
        *value = (float)g_lineTrackCfg.centerDirectMidMin;
    else if (strcmp(key, "track.edge_ratio") == 0)
        *value = g_lineTrackCfg.edgeDirectRatio;
    else if (strcmp(key, "track.edge_min") == 0)
        *value = (float)g_lineTrackCfg.edgeDirectMin;
    else if (strcmp(key, "track.recenter_decay") == 0)
        *value = (float)g_lineTrackCfg.recenterDecayStep;
    else if (strcmp(key, "track.static_bias") == 0)
        *value = (float)g_lineTrackCfg.staticSteerBias;
    else if (strcmp(key, "track.center_deadband") == 0)
        *value = g_lineTrackCfg.centerDeadband;
    else if (strcmp(key, "track.pos_lpf") == 0)
        *value = g_lineTrackCfg.posFilterAlpha;
    else if (strcmp(key, "track.d_lpf") == 0)
        *value = g_lineTrackCfg.dFilterAlpha;
    else if (strcmp(key, "track.offcenter_boost") == 0)
        *value = g_lineTrackCfg.offcenterBoost;
    else if (strcmp(key, "track.center_hold_ticks") == 0)
        *value = (float)g_lineTrackCfg.centerSingleHoldTicks;
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
             "track.lkp,track.lkd,track.center_small_ratio,track.center_small_min,"
             "track.center_mid_ratio,track.center_mid_min,track.edge_ratio,track.edge_min,"
             "track.recenter_decay,track.static_bias,"
             "track.center_deadband,track.pos_lpf,track.d_lpf,track.offcenter_boost,"
             "track.center_hold_ticks,track.recover_ticks,track.search_turn_fast,"
             "track.search_turn_slow,track.search_timeout");
}
