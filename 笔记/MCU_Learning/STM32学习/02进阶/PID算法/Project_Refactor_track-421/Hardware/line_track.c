#include "line_track.h"
#include "config.h"
#include "bsp_uart.h"
#include "motor_driver.h"
#include "sensor_fusion.h"
#include <stdio.h>
#include <string.h>

LineTrack_State_t g_lineTrack;
static LineTrack_RuntimeConfig_t s_trackCfg;

static const int16_t s_trackWeights[8] = { -430, -270, -150, -40, 40, 150, 270, 430 };

static void load_runtime_config_defaults(void)
{
    s_trackCfg.lineKp = PID_TRACK_LINE_KP;
    s_trackCfg.lineKd = PID_TRACK_LINE_KD;
    s_trackCfg.basePwmMin = TRACK_BASE_PWM_MIN;
    s_trackCfg.basePwmMax = TRACK_BASE_PWM_MAX;
    s_trackCfg.cornerBasePwmMax = TRACK_CORNER_BASE_PWM_MAX;
    s_trackCfg.devPwmMax = TRACK_DEV_PWM_MAX;
    s_trackCfg.edgeBasePwmMax = TRACK_EDGE_BASE_PWM_MAX;
    s_trackCfg.edgeDevPwmMax = TRACK_EDGE_DEV_PWM_MAX;
    s_trackCfg.sharpTurnDev = TRACK_SHARP_TURN_DEV;
    s_trackCfg.posCenterDeadband = TRACK_POS_CENTER_DEADBAND;
    s_trackCfg.posNearThreshold = TRACK_POS_NEAR_THRESHOLD;
    s_trackCfg.posMidThreshold = TRACK_POS_MID_THRESHOLD;
    s_trackCfg.posEdgeThreshold = TRACK_POS_EDGE_THRESHOLD;
    s_trackCfg.positionTrim = TRACK_POSITION_TRIM;
    s_trackCfg.crossMinCount = TRACK_CROSS_MIN_COUNT;
    s_trackCfg.crossConfirmTicks = TRACK_CROSS_CONFIRM_TICKS;
    s_trackCfg.widePatternCount = TRACK_WIDE_PATTERN_COUNT;
    s_trackCfg.crossFilter = TRACK_CROSS_FILTER;
    s_trackCfg.dtermStepClamp = TRACK_DTERM_STEP_CLAMP;
    s_trackCfg.dtermWideClamp = TRACK_DTERM_WIDE_CLAMP;
    s_trackCfg.centerBearingSlew = TRACK_CENTER_BEARING_SLEW;
    s_trackCfg.normalBearingSlew = TRACK_NORMAL_BEARING_SLEW;
    s_trackCfg.cornerStrongSideHits = TRACK_CORNER_STRONG_SIDE_HITS;
    s_trackCfg.cornerOppositeMaxHits = TRACK_CORNER_OPPOSITE_MAX_HITS;
    s_trackCfg.cornerConfirmTicks = TRACK_CORNER_CONFIRM_TICKS;
    s_trackCfg.cornerFastConfirmTicks = TRACK_CORNER_FAST_CONFIRM_TICKS;
    s_trackCfg.lossEnterTicks = TRACK_LOSS_ENTER_TICKS;
    s_trackCfg.lossForceCornerTicks = TRACK_LOSS_FORCE_CORNER_TICKS;
    s_trackCfg.lossForceRequireRef = TRACK_LOSS_FORCE_REQUIRE_REF;
    s_trackCfg.lossSearchBearing = TRACK_LOSS_SEARCH_BEARING;
    s_trackCfg.lossHoldBasePwmMax = TRACK_LOSS_HOLD_BASE_PWM_MAX;
    s_trackCfg.lossHoldDevPwmMax = TRACK_LOSS_HOLD_DEV_PWM_MAX;
    s_trackCfg.lossSearchBasePwmMax = TRACK_LOSS_SEARCH_BASE_PWM_MAX;
    s_trackCfg.lossSearchDevPwmMax = TRACK_LOSS_SEARCH_DEV_PWM_MAX;
    s_trackCfg.overrunLimitTicks = TRACK_OVERRUN_LIMIT_TICKS;
    s_trackCfg.turnPwm = TRACK_TURN_PWM;
    s_trackCfg.turnPwmMin = TRACK_TURN_PWM_MIN;
    s_trackCfg.cornerExitPosThreshold = TRACK_CORNER_EXIT_POS_THRESHOLD;
    s_trackCfg.cornerTimeoutMs = TRACK_CORNER_TIMEOUT_MS;
    s_trackCfg.cornerResumeSpeedMax = TRACK_CORNER_RESUME_SPEED_MAX;
    s_trackCfg.cornerRecoverTicks = TRACK_CORNER_RECOVER_TICKS;
    s_trackCfg.centerLockTicks = TRACK_CENTER_LOCK_TICKS;
    s_trackCfg.centerLockDevPwmMax = TRACK_CENTER_LOCK_DEV_PWM_MAX;
    s_trackCfg.recoverBasePwmMax = TRACK_RECOVER_BASE_PWM_MAX;
    s_trackCfg.recoverDevPwmMax = TRACK_RECOVER_DEV_PWM_MAX;
    s_trackCfg.cornerFlipYawDeg = TRACK_CORNER_FLIP_YAW_DEG;
}

static int16_t abs_i16(int16_t v)
{
    return (v < 0) ? (int16_t)(-v) : v;
}

static int8_t clamp_i8(int16_t v, int8_t lo, int8_t hi)
{
    if (v < lo)
        return lo;
    if (v > hi)
        return hi;
    return (int8_t)v;
}

static int16_t clamp_i16(int32_t v, int16_t lo, int16_t hi)
{
    if (v < lo)
        return lo;
    if (v > hi)
        return hi;
    return (int16_t)v;
}

static float wrap_deg(float deg)
{
    while (deg > 180.0f)
        deg -= 360.0f;
    while (deg < -180.0f)
        deg += 360.0f;
    return deg;
}

static uint8_t bit_count(uint8_t bits)
{
    uint8_t count = 0u;
    uint8_t i;

    for (i = 0u; i < 8u; i++)
    {
        if (bits & (uint8_t)(1u << i))
            count++;
    }

    return count;
}

static uint8_t side_hit_count(uint8_t bits, uint8_t mask)
{
    return bit_count((uint8_t)(bits & mask));
}

static void emit_logic_event(uint32_t tickMs,
                             const char *logic,
                             uint8_t dir,
                             const char *reason,
                             uint8_t bits,
                             int16_t pos)
{
    char dirChar = '-';

    if (dir == LT_DIR_LEFT)
        dirChar = 'L';
    else if (dir == LT_DIR_RIGHT)
        dirChar = 'R';

    BspUart_SendLogicEvent(tickMs,
                           logic,
                           dirChar,
                           reason,
                           bits,
                           pos,
                           g_lineTrack.lastDirectionalBits,
                           g_lineTrack.lastDirectionalScore,
                           g_lineTrack.bearingDev);
}

static uint8_t read_sensor_bits(void)
{
    LineSensor_Data_t line;

    LineSensor_Read(&line);
    return line.bits;
}

static uint8_t has_directional_evidence(uint8_t bits)
{
    return (((bits & LT_MASK_LEFT_ALL) != 0u) || ((bits & LT_MASK_RIGHT_ALL) != 0u)) ? 1u : 0u;
}

static uint8_t is_edge_grip_pattern(uint8_t bits)
{
    if ((bits & LT_MASK_CENTER) != 0u)
        return 0u;

    if ((bits & LT_MASK_LEFT_ALL) != 0u && (bits & LT_MASK_RIGHT_ALL) == 0u)
        return 1u;

    if ((bits & LT_MASK_RIGHT_ALL) != 0u && (bits & LT_MASK_LEFT_ALL) == 0u)
        return 1u;

    return 0u;
}

static int16_t weighted_position(uint8_t bits)
{
    int32_t sum = 0;
    uint8_t count = 0u;
    uint8_t i;

    for (i = 0u; i < 8u; i++)
    {
        if (bits & (uint8_t)(1u << i))
        {
            sum += s_trackWeights[i];
            count++;
        }
    }

    if (count == 0u)
        return 0;

    return (int16_t)(sum / (int32_t)count);
}

static uint8_t infer_corner_dir_from_bits(uint8_t bits)
{
    uint8_t leftHits = side_hit_count(bits, LT_MASK_LEFT_ALL);
    uint8_t rightHits = side_hit_count(bits, LT_MASK_RIGHT_ALL);

    if (leftHits >= s_trackCfg.cornerStrongSideHits
        && rightHits <= s_trackCfg.cornerOppositeMaxHits)
        return LT_DIR_LEFT;

    if (rightHits >= s_trackCfg.cornerStrongSideHits
        && leftHits <= s_trackCfg.cornerOppositeMaxHits)
        return LT_DIR_RIGHT;

    return 0u;
}

static uint16_t directional_evidence_score(uint8_t bits, int16_t pos)
{
    uint8_t dir = infer_corner_dir_from_bits(bits);
    uint8_t sideHits;
    uint16_t score = (uint16_t)abs_i16(pos);

    if (dir == 0u)
        return 0u;

    if (dir == LT_DIR_LEFT)
    {
        sideHits = side_hit_count(bits, LT_MASK_LEFT_ALL);
        if ((bits & LT_MASK_LEFT_FAR) != 0u)
            score = (uint16_t)(score + 120u);
    }
    else
    {
        sideHits = side_hit_count(bits, LT_MASK_RIGHT_ALL);
        if ((bits & LT_MASK_RIGHT_FAR) != 0u)
            score = (uint16_t)(score + 120u);
    }

    if (sideHits >= 2u)
        score = (uint16_t)(score + 80u);

    if ((bits & LT_MASK_CENTER) != LT_MASK_CENTER)
        score = (uint16_t)(score + 40u);

    return score;
}

static void clear_directional_snapshot(void)
{
    g_lineTrack.lastDirectionalBits = 0u;
    g_lineTrack.lastDirectionalScore = 0u;
}

static uint8_t has_strong_corner_snapshot(void)
{
    return (infer_corner_dir_from_bits(g_lineTrack.lastDirectionalBits) != 0u) ? 1u : 0u;
}

static void update_directional_snapshot(uint8_t bits, int16_t pos)
{
    uint16_t score;

    if ((bits & LT_MASK_CENTER) == LT_MASK_CENTER
        && abs_i16(pos) <= s_trackCfg.posCenterDeadband)
    {
        clear_directional_snapshot();
        return;
    }

    if (!has_directional_evidence(bits))
        return;

    score = directional_evidence_score(bits, pos);
    if (score == 0u)
        return;

    if (score >= g_lineTrack.lastDirectionalScore)
    {
        g_lineTrack.lastDirectionalBits = bits;
        g_lineTrack.lastDirectionalScore = score;
    }
}

static int16_t apply_position_trim(int16_t pos)
{
    return clamp_i16((int32_t)pos + s_trackCfg.positionTrim, -350, 350);
}

static uint8_t lerp_u8_by_pos(int16_t value,
                              int16_t inLo,
                              int16_t inHi,
                              uint8_t outLo,
                              uint8_t outHi)
{
    int32_t numerator;
    int32_t denom;

    if (value <= inLo)
        return outLo;

    if (value >= inHi)
        return outHi;

    if (inHi <= inLo)
        return outHi;

    numerator = (int32_t)(value - inLo) * (int32_t)(outHi - outLo);
    denom = (int32_t)(inHi - inLo);

    return (uint8_t)(outLo + (uint8_t)((numerator + (denom / 2)) / denom));
}

static int16_t blend_center_zone_position(uint8_t bits, int16_t pos)
{
    int32_t blended = pos;
    uint8_t centerMask = (uint8_t)(bits & LT_MASK_CENTER);
    uint8_t nearSideMask = (uint8_t)(bits & (LT_MASK_LEFT | LT_MASK_RIGHT));
    uint8_t farSideMask = (uint8_t)(bits & (LT_MASK_LEFT_FAR | LT_MASK_RIGHT_FAR));

    if (centerMask == LT_MASK_CENTER)
    {
        if (nearSideMask == 0u && farSideMask == 0u)
            blended = (blended * 35) / 100;
        else if (farSideMask == 0u)
            blended = (blended * 50) / 100;
        else
            blended = (blended * 65) / 100;
    }
    else if (centerMask != 0u)
    {
        if (farSideMask == 0u)
            blended = (blended * 65) / 100;
        else
            blended = (blended * 80) / 100;
    }

    return clamp_i16(blended, -350, 350);
}

static int8_t position_to_bearing(uint8_t bits, int16_t pos)
{
    int16_t effectivePos = blend_center_zone_position(bits, pos);
    int16_t absPos = abs_i16(effectivePos);
    int8_t sign = (effectivePos >= 0) ? 1 : -1;
    uint8_t mag = 0u;
    uint8_t centerMask = (uint8_t)(bits & LT_MASK_CENTER);

    if ((bits & 0x01u) != 0u && (bits & LT_MASK_RIGHT_ALL) == 0u)
        return -7;

    if ((bits & 0x80u) != 0u && (bits & LT_MASK_LEFT_ALL) == 0u)
        return 7;

    if (bits == LT_MASK_CENTER)
        return 0;

    if (centerMask == LT_MASK_CENTER
        && absPos <= (int16_t)(s_trackCfg.posNearThreshold / 2))
        return 0;

    if (absPos <= s_trackCfg.posCenterDeadband)
        mag = 0u;
    else if (absPos <= s_trackCfg.posNearThreshold)
        mag = lerp_u8_by_pos(absPos, s_trackCfg.posCenterDeadband, s_trackCfg.posNearThreshold, 0u, 2u);
    else if (absPos <= s_trackCfg.posMidThreshold)
        mag = lerp_u8_by_pos(absPos, s_trackCfg.posNearThreshold, s_trackCfg.posMidThreshold, 2u, 4u);
    else if (absPos <= s_trackCfg.posEdgeThreshold)
        mag = lerp_u8_by_pos(absPos, s_trackCfg.posMidThreshold, s_trackCfg.posEdgeThreshold, 4u, 6u);
    else
        mag = 7u;

    if (absPos > s_trackCfg.posCenterDeadband && mag == 0u)
        mag = 1u;

    if (centerMask == LT_MASK_CENTER && mag > 3u)
        mag = 3u;
    else if (centerMask != 0u && mag > 5u)
        mag = 5u;

    if (centerMask == 0u
        && is_edge_grip_pattern(bits)
        && mag < 4u)
    {
        mag = 4u;
    }

    return (int8_t)(sign * (int8_t)mag);
}

static uint8_t is_cross_pattern(uint8_t bits, int16_t pos)
{
    uint8_t count = bit_count(bits);

    if ((bits & LT_MASK_CENTER) != LT_MASK_CENTER)
        return 0u;

    if (count < s_trackCfg.crossMinCount)
        return 0u;

    if (abs_i16(pos) > s_trackCfg.posNearThreshold)
        return 0u;

    if (((bits & LT_MASK_LEFT_FAR) == 0u || (bits & LT_MASK_RIGHT_FAR) == 0u)
        && count < (uint8_t)(s_trackCfg.crossMinCount + 1u))
        return 0u;

    return 1u;
}

static uint8_t is_wide_center_pattern(uint8_t bits)
{
    if ((bits & LT_MASK_CENTER) == 0u)
        return 0u;

    if (bit_count(bits) < s_trackCfg.widePatternCount)
        return 0u;

    if ((bits & LT_MASK_LEFT_ALL) == 0u)
        return 0u;

    if ((bits & LT_MASK_RIGHT_ALL) == 0u)
        return 0u;

    return 1u;
}

static void update_last_trend(uint8_t bits, int16_t pos, int8_t bearing)
{
    if ((bits & LT_MASK_LEFT_ALL) != 0u && (bits & LT_MASK_RIGHT_ALL) == 0u)
    {
        g_lineTrack.lastTrendDir = LT_DIR_LEFT;
        return;
    }

    if ((bits & LT_MASK_RIGHT_ALL) != 0u && (bits & LT_MASK_LEFT_ALL) == 0u)
    {
        g_lineTrack.lastTrendDir = LT_DIR_RIGHT;
        return;
    }

    if (bearing < 0 || pos < -s_trackCfg.posCenterDeadband)
    {
        g_lineTrack.lastTrendDir = LT_DIR_LEFT;
        return;
    }

    if (bearing > 0 || pos > s_trackCfg.posCenterDeadband)
        g_lineTrack.lastTrendDir = LT_DIR_RIGHT;
}

static int8_t smooth_bearing_dev(int8_t rawBearing, uint8_t bits)
{
    int8_t step = (int8_t)s_trackCfg.normalBearingSlew;
    int8_t prevBearing = g_lineTrack.bearingDev;

    if ((bits & LT_MASK_CENTER) != 0u && abs_i16(rawBearing) <= 2)
        step = (int8_t)s_trackCfg.centerBearingSlew;

    if (rawBearing > (int8_t)(prevBearing + step))
        return (int8_t)(prevBearing + step);

    if (rawBearing < (int8_t)(prevBearing - step))
        return (int8_t)(prevBearing - step);

    return rawBearing;
}

static int8_t get_loss_hold_bearing(uint8_t dir)
{
    int8_t holdMag = (s_trackCfg.lossSearchBearing > 2u)
        ? (int8_t)(s_trackCfg.lossSearchBearing - 2u)
        : 2;

    if (dir == LT_DIR_LEFT)
        return (int8_t)(-holdMag);

    return holdMag;
}

static int16_t get_loss_hold_position(uint8_t dir)
{
    int16_t holdPos = s_trackCfg.posMidThreshold;

    if (holdPos < s_trackCfg.posNearThreshold)
        holdPos = s_trackCfg.posNearThreshold;

    if (dir == LT_DIR_LEFT)
        return (int16_t)(-holdPos);

    return holdPos;
}

static uint8_t fallback_corner_dir(void);

static void start_loss_soft_hold(uint32_t tickMs)
{
    int16_t holdPos;

    if (g_lineTrack.lossSoftHoldActive)
        return;

    g_lineTrack.lossSoftHoldActive = 1u;
    g_lineTrack.lossSearchDir = fallback_corner_dir();
    g_lineTrack.traceLogicState = LT_DBG_LOSS;
    holdPos = get_loss_hold_position(g_lineTrack.lossSearchDir);
    emit_logic_event(tickMs, "LOS", g_lineTrack.lossSearchDir, "soft_enter", g_lineTrack.sensorBits, holdPos);
}

static void update_loss_soft_hold(void)
{
    int8_t holdBearing = get_loss_hold_bearing(g_lineTrack.lossSearchDir);

    g_lineTrack.traceLogicState = LT_DBG_LOSS;
    g_lineTrack.weightedPos = get_loss_hold_position(g_lineTrack.lossSearchDir);
    g_lineTrack.bearingDev = smooth_bearing_dev(holdBearing, 0u);
}

static void stop_loss_soft_hold(uint32_t tickMs, uint8_t bits, int16_t pos, const char *reason)
{
    if (!g_lineTrack.lossSoftHoldActive)
        return;

    g_lineTrack.lossSoftHoldActive = 0u;
    emit_logic_event(tickMs, "TRK", g_lineTrack.lossSearchDir, reason, bits, pos);
}

static uint8_t fallback_corner_dir(void)
{
    uint8_t dir = infer_corner_dir_from_bits(g_lineTrack.lastDirectionalBits);

    if (dir != 0u)
        return dir;

    dir = infer_corner_dir_from_bits(g_lineTrack.lastData);
    if (dir != 0u)
        return dir;

    if (g_lineTrack.lastTrendDir == LT_DIR_LEFT || g_lineTrack.lastTrendDir == LT_DIR_RIGHT)
        return g_lineTrack.lastTrendDir;

    if (g_lineTrack.bearingDev < 0)
        return LT_DIR_LEFT;

    if (g_lineTrack.bearingDev > 0)
        return LT_DIR_RIGHT;

    if (g_lineTrack.weightedPos < 0)
        return LT_DIR_LEFT;

    return LT_DIR_RIGHT;
}

static void start_loss_search(uint32_t tickMs)
{
    if (g_lineTrack.lossSearchActive)
        return;

    g_lineTrack.lossSoftHoldActive = 0u;
    g_lineTrack.lossSearchActive = 1u;
    g_lineTrack.lossSearchDir = fallback_corner_dir();
    g_lineTrack.lossHoldReported = 0u;
    g_lineTrack.traceLogicState = LT_DBG_LOSS;
    emit_logic_event(tickMs, "LOS", g_lineTrack.lossSearchDir, "enter", g_lineTrack.sensorBits, g_lineTrack.weightedPos);
}

static void stop_loss_search(uint32_t tickMs, uint8_t bits, int16_t pos, const char *reason)
{
    if (!g_lineTrack.lossSearchActive)
        return;

    g_lineTrack.lossSearchActive = 0u;
    g_lineTrack.lossSoftHoldActive = 0u;
    g_lineTrack.lossHoldReported = 0u;
    g_lineTrack.traceLogicState = LT_DBG_TRACK;
    emit_logic_event(tickMs, "TRK", g_lineTrack.lossSearchDir, reason, bits, pos);
    g_lineTrack.lossSearchDir = 0u;
}

static uint8_t prefers_fast_corner_entry(void)
{
    if (has_strong_corner_snapshot())
        return 1u;

    return 0u;
}

static void track_motor_stop(void)
{
    MotorDriver_Stop();
    MotorDriver_Disable();
    g_lineTrack.lastTrackOutL = 0;
    g_lineTrack.lastTrackOutR = 0;
}

static void track_motor_forward(int16_t left, int16_t right)
{
    MotorDriver_Enable();
    MotorDriver_SetTurnPWM(left, right);
    g_lineTrack.lastTrackOutL = left;
    g_lineTrack.lastTrackOutR = right;
}

static void track_turn_in_place(uint8_t dir, int16_t turnPwm)
{
    if (turnPwm < s_trackCfg.turnPwmMin)
        turnPwm = s_trackCfg.turnPwmMin;

    if (turnPwm > s_trackCfg.turnPwm)
        turnPwm = s_trackCfg.turnPwm;

    MotorDriver_Enable();

    if (dir == LT_DIR_LEFT)
    {
        MotorDriver_SetTurnPWM(-turnPwm, turnPwm);
        g_lineTrack.devSpeed = (int16_t)(-turnPwm);
    }
    else
    {
        MotorDriver_SetTurnPWM(turnPwm, -turnPwm);
        g_lineTrack.devSpeed = turnPwm;
    }
}

static void reset_cross_filter_if_needed(uint8_t crossPattern)
{
    if (!crossPattern
        && g_lineTrack.crossState == 2u
        && g_lineTrack.filterTimes >= s_trackCfg.crossFilter)
    {
        g_lineTrack.crossState = 1u;
        g_lineTrack.filterTimes = 0u;
    }
}

static void update_crossing_state(uint8_t crossPattern)
{
    if (!crossPattern)
    {
        reset_cross_filter_if_needed(0u);
        return;
    }

    if (g_lineTrack.crossState == 1u)
    {
        if (g_lineTrack.crossCount < 255u)
            g_lineTrack.crossCount++;

        g_lineTrack.crossState = 2u;
        g_lineTrack.filterTimes = 0u;

        if (g_lineTrack.crossCount >= g_lineTrack.crossing)
            g_lineTrack.autoFlag = LT_FLAG_STOP;
    }
}

static void start_corner_search(uint32_t tickMs, float currentYaw)
{
    stop_loss_search(tickMs, g_lineTrack.sensorBits, g_lineTrack.weightedPos, "loss_to_corner");
    g_lineTrack.cornerTurning = 1u;
    g_lineTrack.cornerDir = fallback_corner_dir();
    g_lineTrack.cornerStartTick = tickMs;
    g_lineTrack.cornerStartYaw = currentYaw;
    g_lineTrack.cornerFlipUsed = 0u;
    g_lineTrack.centerLockTicks = 0u;
    g_lineTrack.dbgTrackState = LT_DBG_CORNER;
    g_lineTrack.dbgCornerDir = g_lineTrack.cornerDir;
    g_lineTrack.dbgCornerYawDelta = 0.0f;
    g_lineTrack.dbgCornerBits = g_lineTrack.sensorBits;
    g_lineTrack.traceLogicState = LT_DBG_CORNER;
    emit_logic_event(tickMs, "COR", g_lineTrack.cornerDir, "enter", g_lineTrack.sensorBits, g_lineTrack.weightedPos);
}

static uint8_t corner_exit_ready(uint8_t bits, int16_t pos)
{
    if (bits == 0u)
        return 0u;

    if ((bits & LT_MASK_CENTER) != LT_MASK_CENTER)
        return 0u;

    if (bit_count(bits) < 2u)
        return 0u;

    if (abs_i16(pos) > s_trackCfg.cornerExitPosThreshold)
        return 0u;

    return 1u;
}

static uint8_t opposite_side_detected(uint8_t bits, uint8_t dir)
{
    if (bits == 0u)
        return 0u;

    if (dir == LT_DIR_LEFT)
    {
        if ((bits & LT_MASK_RIGHT_ALL) != 0u && (bits & LT_MASK_LEFT_ALL) == 0u)
            return 1u;
    }
    else if (dir == LT_DIR_RIGHT)
    {
        if ((bits & LT_MASK_LEFT_ALL) != 0u && (bits & LT_MASK_RIGHT_ALL) == 0u)
            return 1u;
    }

    return 0u;
}

static uint8_t corner_sensor_progress(uint8_t bits, uint8_t dir)
{
    if (dir == LT_DIR_LEFT)
    {
        if ((bits & LT_MASK_LEFT) != 0u)
            return 3u;
        if ((bits & 0x02u) != 0u)
            return 2u;
        if ((bits & 0x01u) != 0u)
            return 1u;
    }
    else if (dir == LT_DIR_RIGHT)
    {
        if ((bits & LT_MASK_RIGHT) != 0u)
            return 3u;
        if ((bits & 0x40u) != 0u)
            return 2u;
        if ((bits & 0x80u) != 0u)
            return 1u;
    }

    return 0u;
}

static int16_t corner_turn_pwm_from_bits(uint8_t bits, uint8_t dir)
{
    uint8_t progress = corner_sensor_progress(bits, dir);

    if (progress == 0u)
        return s_trackCfg.turnPwm;

    if (progress == 1u)
        return clamp_i16((int32_t)s_trackCfg.turnPwm - 40, s_trackCfg.turnPwmMin, s_trackCfg.turnPwm);

    if (progress == 2u)
        return clamp_i16((int32_t)s_trackCfg.turnPwm - 58, s_trackCfg.turnPwmMin, s_trackCfg.turnPwm);

    return s_trackCfg.turnPwmMin;
}

static uint8_t should_hold_center_lock(uint8_t bits, int16_t pos)
{
    if (g_lineTrack.centerLockTicks == 0u)
        return 0u;

    if ((bits & LT_MASK_CENTER) != LT_MASK_CENTER)
        return 0u;

    if (abs_i16(pos) > s_trackCfg.cornerExitPosThreshold)
        return 0u;

    return 1u;
}

static void signal_handler(uint32_t tickMs, float currentYaw)
{
    uint8_t bits = read_sensor_bits();
    uint8_t crossPattern;
    uint8_t wideCenterPattern;
    uint8_t cornerConfirmTicks = s_trackCfg.cornerConfirmTicks;
    uint8_t hasCornerSnapshot = has_strong_corner_snapshot();
    int16_t rawPos = 0;

    g_lineTrack.sensorBits = bits;

    if (g_lineTrack.filterTimes < 255u)
        g_lineTrack.filterTimes++;

    if (g_lineTrack.cornerTurning)
        return;

    if (bits != 0u)
    {
        g_lineTrack.overrunCount = 0u;
        g_lineTrack.lastData = bits;
        rawPos = weighted_position(bits);
    }

    crossPattern = is_cross_pattern(bits, rawPos);
    wideCenterPattern = is_wide_center_pattern(bits);

    if (crossPattern)
    {
        if (g_lineTrack.crossDetectTicks < 255u)
            g_lineTrack.crossDetectTicks++;
    }
    else
    {
        g_lineTrack.crossDetectTicks = 0u;
    }

    update_crossing_state((g_lineTrack.crossDetectTicks >= s_trackCfg.crossConfirmTicks) ? 1u : 0u);

    if (g_lineTrack.crossDetectTicks >= s_trackCfg.crossConfirmTicks)
    {
        g_lineTrack.weightedPos = 0;
        g_lineTrack.bearingDev = 0;
        g_lineTrack.lastBearingDev = 0;
        g_lineTrack.dbgTrackState = LT_DBG_CROSS;
        if (g_lineTrack.traceLogicState != LT_DBG_CROSS)
        {
            g_lineTrack.traceLogicState = LT_DBG_CROSS;
            emit_logic_event(tickMs, "CRS", 0u, "enter", bits, 0);
        }
        return;
    }

    if (bits == 0u)
    {
        uint8_t canForceCorner = hasCornerSnapshot || (s_trackCfg.lossForceRequireRef == 0u);

        if (g_lineTrack.overrunCount < 255u)
            g_lineTrack.overrunCount++;

        if (hasCornerSnapshot && prefers_fast_corner_entry())
            cornerConfirmTicks = s_trackCfg.cornerFastConfirmTicks;

        if (hasCornerSnapshot)
        {
            if (!g_lineTrack.cornerTurning
                && g_lineTrack.overrunCount >= cornerConfirmTicks)
            {
                start_corner_search(tickMs, currentYaw);
            }
        }
        else
        {
            int8_t searchBearing;

            if (g_lineTrack.overrunCount < s_trackCfg.lossEnterTicks)
            {
                start_loss_soft_hold(tickMs);
                g_lineTrack.dbgTrackState = LT_DBG_LOSS;
                update_loss_soft_hold();
                update_last_trend(0u, g_lineTrack.weightedPos, g_lineTrack.bearingDev);
            }
            else
            {
                stop_loss_soft_hold(tickMs, 0u, g_lineTrack.weightedPos, "soft_to_loss");
                start_loss_search(tickMs);
                g_lineTrack.dbgTrackState = LT_DBG_LOSS;

                if (g_lineTrack.lossSearchDir == LT_DIR_LEFT)
                {
                    g_lineTrack.weightedPos = (int16_t)(-s_trackCfg.posEdgeThreshold);
                    searchBearing = (int8_t)(-s_trackCfg.lossSearchBearing);
                }
                else
                {
                    g_lineTrack.weightedPos = s_trackCfg.posEdgeThreshold;
                    searchBearing = (int8_t)s_trackCfg.lossSearchBearing;
                }

                g_lineTrack.bearingDev = smooth_bearing_dev(searchBearing, 0u);
                update_last_trend(0u, g_lineTrack.weightedPos, g_lineTrack.bearingDev);

                if (!g_lineTrack.cornerTurning
                    && g_lineTrack.overrunCount >= s_trackCfg.lossForceCornerTicks)
                {
                    if (canForceCorner)
                    {
                        start_corner_search(tickMs, currentYaw);
                    }
                    else if (!g_lineTrack.lossHoldReported)
                    {
                        emit_logic_event(tickMs, "LOS", g_lineTrack.lossSearchDir, "hold", g_lineTrack.sensorBits, g_lineTrack.weightedPos);
                        g_lineTrack.lossHoldReported = 1u;
                    }
                }
            }
        }

        if (!g_lineTrack.cornerTurning
            && g_lineTrack.overrunCount >= s_trackCfg.overrunLimitTicks)
            g_lineTrack.autoFlag = LT_FLAG_STOP;

        return;
    }

    g_lineTrack.weightedPos = apply_position_trim(rawPos);

    if (g_lineTrack.lossSoftHoldActive)
    {
        stop_loss_soft_hold(tickMs, bits, g_lineTrack.weightedPos, "soft_exit");
        if ((bits & LT_MASK_CENTER) != 0u
            && abs_i16(g_lineTrack.weightedPos) <= s_trackCfg.posNearThreshold)
        {
            g_lineTrack.centerLockTicks = s_trackCfg.centerLockTicks;
        }
    }

    if (g_lineTrack.lossSearchActive)
    {
        stop_loss_search(tickMs, bits, g_lineTrack.weightedPos, "loss_exit");
        if ((bits & LT_MASK_CENTER) != 0u
            && abs_i16(g_lineTrack.weightedPos) <= s_trackCfg.posNearThreshold)
        {
            g_lineTrack.centerLockTicks = s_trackCfg.centerLockTicks;
        }
    }

    update_directional_snapshot(bits, g_lineTrack.weightedPos);

    if (!g_lineTrack.cornerTurning && g_lineTrack.traceLogicState == LT_DBG_CROSS)
    {
        g_lineTrack.traceLogicState = LT_DBG_TRACK;
        emit_logic_event(tickMs, "TRK", 0u, "cross_exit", bits, g_lineTrack.weightedPos);
    }

    if (wideCenterPattern)
    {
        g_lineTrack.bearingDev = 0;
        g_lineTrack.lastBearingDev = 0;
        return;
    }

    if (should_hold_center_lock(bits, g_lineTrack.weightedPos))
    {
        g_lineTrack.weightedPos = 0;
        g_lineTrack.bearingDev = 0;
        g_lineTrack.lastBearingDev = 0;
        return;
    }

    g_lineTrack.bearingDev = smooth_bearing_dev(position_to_bearing(bits, g_lineTrack.weightedPos), bits);
    update_last_trend(bits, g_lineTrack.weightedPos, g_lineTrack.bearingDev);
}

static int16_t dev_speed_pid(int8_t empower, int8_t target)
{
    int8_t dev = (int8_t)(empower - target);
    int8_t delta = (int8_t)(dev - g_lineTrack.lastBearingDev);
    float pwm;

    delta = clamp_i8(delta, (int8_t)(-s_trackCfg.dtermStepClamp), (int8_t)s_trackCfg.dtermStepClamp);

    if ((g_lineTrack.sensorBits & LT_MASK_CENTER) != 0u
        && bit_count(g_lineTrack.sensorBits) >= s_trackCfg.widePatternCount)
    {
        delta = clamp_i8(delta, (int8_t)(-s_trackCfg.dtermWideClamp), (int8_t)s_trackCfg.dtermWideClamp);
    }

    pwm = g_lineTrack.kp * (float)dev
        + g_lineTrack.kd * (float)delta;

    g_lineTrack.lastBearingDev = dev;

    if (pwm >= 0.0f)
        return (int16_t)(pwm + 0.5f);

    return (int16_t)(pwm - 0.5f);
}

static void compute_and_drive(int16_t basePwm)
{
    int16_t driveBase = basePwm;
    int16_t left;
    int16_t right;
    int16_t devLimit = s_trackCfg.devPwmMax;
    int16_t absBearing = (g_lineTrack.bearingDev >= 0)
        ? g_lineTrack.bearingDev
        : (int16_t)(-g_lineTrack.bearingDev);
    uint8_t edgeGrip = is_edge_grip_pattern(g_lineTrack.sensorBits);

    if (driveBase < s_trackCfg.basePwmMin)
        driveBase = s_trackCfg.basePwmMin;

    if (driveBase > s_trackCfg.basePwmMax)
        driveBase = s_trackCfg.basePwmMax;

    if (absBearing >= s_trackCfg.sharpTurnDev && driveBase > s_trackCfg.cornerBasePwmMax)
        driveBase = s_trackCfg.cornerBasePwmMax;

    if (edgeGrip)
    {
        if (driveBase > s_trackCfg.edgeBasePwmMax)
            driveBase = s_trackCfg.edgeBasePwmMax;
        devLimit = s_trackCfg.edgeDevPwmMax;
    }

    if (g_lineTrack.lossSoftHoldActive)
    {
        if (driveBase > s_trackCfg.lossHoldBasePwmMax)
            driveBase = s_trackCfg.lossHoldBasePwmMax;
        devLimit = s_trackCfg.lossHoldDevPwmMax;
    }

    if (g_lineTrack.lossSearchActive)
    {
        if (driveBase > s_trackCfg.lossSearchBasePwmMax)
            driveBase = s_trackCfg.lossSearchBasePwmMax;
        devLimit = s_trackCfg.lossSearchDevPwmMax;
    }

    if (g_lineTrack.cornerRecoverTicks != 0u)
    {
        if (driveBase > s_trackCfg.recoverBasePwmMax)
            driveBase = s_trackCfg.recoverBasePwmMax;
        devLimit = s_trackCfg.recoverDevPwmMax;
    }

    if (g_lineTrack.centerLockTicks != 0u
        && devLimit > s_trackCfg.centerLockDevPwmMax)
    {
        devLimit = s_trackCfg.centerLockDevPwmMax;
    }

    g_lineTrack.devSpeed = dev_speed_pid(g_lineTrack.bearingDev, 0);

    if (g_lineTrack.devSpeed > devLimit)
        g_lineTrack.devSpeed = devLimit;

    if (g_lineTrack.devSpeed < -devLimit)
        g_lineTrack.devSpeed = -devLimit;

    left = (int16_t)(driveBase + g_lineTrack.devSpeed);
    right = (int16_t)(driveBase - g_lineTrack.devSpeed);

    if (left > TRACK_PWM_MAX)
        left = TRACK_PWM_MAX;
    if (left < TRACK_PWM_MIN)
        left = TRACK_PWM_MIN;

    if (right > TRACK_PWM_MAX)
        right = TRACK_PWM_MAX;
    if (right < TRACK_PWM_MIN)
        right = TRACK_PWM_MIN;

    track_motor_forward(left, right);

    if (g_lineTrack.cornerRecoverTicks != 0u)
        g_lineTrack.cornerRecoverTicks--;

    if (g_lineTrack.centerLockTicks != 0u)
        g_lineTrack.centerLockTicks--;
}

static void handle_corner_search(uint32_t tickMs, int16_t basePwm, float currentYaw)
{
    uint8_t bits = read_sensor_bits();
    int16_t lockPos = 0;
    float yawDelta = wrap_deg(currentYaw - g_lineTrack.cornerStartYaw);

    if (bits != 0u)
        lockPos = apply_position_trim(weighted_position(bits));

    g_lineTrack.dbgTrackState = LT_DBG_CORNER;
    g_lineTrack.dbgCornerDir = g_lineTrack.cornerDir;
    g_lineTrack.dbgCornerYawDelta = yawDelta;
    g_lineTrack.dbgCornerBits = bits;

    if (corner_exit_ready(bits, lockPos))
    {
        g_lineTrack.cornerTurning = 0u;
        g_lineTrack.lossSearchActive = 0u;
        g_lineTrack.lossSoftHoldActive = 0u;
        g_lineTrack.lossHoldReported = 0u;
        g_lineTrack.lossSearchDir = 0u;
        g_lineTrack.overrunCount = 0u;
        g_lineTrack.sensorBits = bits;
        g_lineTrack.lastData = bits;
        g_lineTrack.weightedPos = 0;
        g_lineTrack.bearingDev = 0;
        update_last_trend(bits, g_lineTrack.weightedPos, g_lineTrack.bearingDev);
        g_lineTrack.lastBearingDev = 0;
        g_lineTrack.cornerDone = 1u;
        g_lineTrack.cornerRecoverTicks = s_trackCfg.cornerRecoverTicks;
        g_lineTrack.centerLockTicks = s_trackCfg.centerLockTicks;
        g_lineTrack.traceLogicState = LT_DBG_TRACK;
        emit_logic_event(tickMs, "TRK", g_lineTrack.cornerDir, "corner_exit", bits, 0);
        compute_and_drive(basePwm);
        return;
    }

    if (!g_lineTrack.cornerFlipUsed
        && opposite_side_detected(bits, g_lineTrack.cornerDir))
    {
        g_lineTrack.cornerDir = (g_lineTrack.cornerDir == LT_DIR_LEFT) ? LT_DIR_RIGHT : LT_DIR_LEFT;
        g_lineTrack.cornerStartYaw = currentYaw;
        g_lineTrack.cornerStartTick = tickMs;
        g_lineTrack.cornerFlipUsed = 1u;
        g_lineTrack.dbgCornerDir = g_lineTrack.cornerDir;
        emit_logic_event(tickMs, "COR", g_lineTrack.cornerDir, "flip", bits, lockPos);
    }

    if ((tickMs - g_lineTrack.cornerStartTick) >= s_trackCfg.cornerTimeoutMs)
    {
        g_lineTrack.cornerStartTick = tickMs;
        g_lineTrack.cornerStartYaw = currentYaw;
        g_lineTrack.cornerFlipUsed = 0u;
        emit_logic_event(tickMs, "COR", g_lineTrack.cornerDir, "rearm", bits, lockPos);
    }

    track_turn_in_place(g_lineTrack.cornerDir, corner_turn_pwm_from_bits(bits, g_lineTrack.cornerDir));
}

void LineTrack_Init(void)
{
    load_runtime_config_defaults();
    g_lineTrack.kp = s_trackCfg.lineKp;
    g_lineTrack.kd = s_trackCfg.lineKd;
    LineTrack_Stop();
}

void LineTrack_Start(uint8_t crossings)
{
    g_lineTrack.state = LT_STATE_STARTING;
    g_lineTrack.autoFlag = LT_FLAG_START;
    g_lineTrack.bearingDev = 0;
    g_lineTrack.lastBearingDev = 0;
    g_lineTrack.sensorBits = 0u;
    g_lineTrack.lastData = LT_MASK_CENTER;
    clear_directional_snapshot();
    g_lineTrack.overrunCount = 0u;
    g_lineTrack.filterTimes = 0u;
    g_lineTrack.crossing = crossings;
    g_lineTrack.crossCount = 0u;
    g_lineTrack.crossState = 1u;
    g_lineTrack.crossDetectTicks = 0u;
    g_lineTrack.cornerTurning = 0u;
    g_lineTrack.cornerDone = 0u;
    g_lineTrack.cornerDir = 0u;
    g_lineTrack.lastTrendDir = 0u;
    g_lineTrack.traceLogicState = LT_DBG_TRACK;
    g_lineTrack.lossSearchActive = 0u;
    g_lineTrack.lossSoftHoldActive = 0u;
    g_lineTrack.lossHoldReported = 0u;
    g_lineTrack.lossSearchDir = 0u;
    g_lineTrack.cornerRecoverTicks = 0u;
    g_lineTrack.centerLockTicks = 0u;
    g_lineTrack.cornerFlipUsed = 0u;
    g_lineTrack.cornerStartTick = 0u;
    g_lineTrack.cornerStartYaw = 0.0f;
    g_lineTrack.weightedPos = 0;
    g_lineTrack.devSpeed = 0;
    g_lineTrack.lastTrackOutL = 0;
    g_lineTrack.lastTrackOutR = 0;
    g_lineTrack.dbgTrackState = LT_DBG_TRACK;
    g_lineTrack.dbgCornerDir = 0u;
    g_lineTrack.dbgCornerYawDelta = 0.0f;
    g_lineTrack.dbgCornerBits = 0u;
}

void LineTrack_Stop(void)
{
    g_lineTrack.state = LT_STATE_IDLE;
    g_lineTrack.autoFlag = LT_FLAG_STOP;
    g_lineTrack.cornerTurning = 0u;
    g_lineTrack.devSpeed = 0;
    g_lineTrack.lastTrendDir = 0u;
    clear_directional_snapshot();
    g_lineTrack.traceLogicState = LT_DBG_TRACK;
    g_lineTrack.crossDetectTicks = 0u;
    g_lineTrack.lossSearchActive = 0u;
    g_lineTrack.lossSoftHoldActive = 0u;
    g_lineTrack.lossHoldReported = 0u;
    g_lineTrack.lossSearchDir = 0u;
    g_lineTrack.cornerRecoverTicks = 0u;
    g_lineTrack.centerLockTicks = 0u;
    g_lineTrack.cornerFlipUsed = 0u;
    g_lineTrack.cornerStartYaw = 0.0f;
    g_lineTrack.dbgTrackState = LT_DBG_TRACK;
    g_lineTrack.dbgCornerDir = 0u;
    g_lineTrack.dbgCornerYawDelta = 0.0f;
    g_lineTrack.dbgCornerBits = 0u;
    track_motor_stop();
}

void LineTrack_Update(uint32_t tickMs, int16_t basePwm, float currentYaw)
{
    (void)currentYaw;

    switch (g_lineTrack.state)
    {
    case LT_STATE_STARTING:
        g_lineTrack.state = LT_STATE_RUNNING;
        MotorDriver_Enable();
        break;

    case LT_STATE_RUNNING:
        g_lineTrack.dbgTrackState = LT_DBG_TRACK;
        g_lineTrack.dbgCornerDir = 0u;
        g_lineTrack.dbgCornerYawDelta = 0.0f;
        g_lineTrack.dbgCornerBits = 0u;

        signal_handler(tickMs, currentYaw);

        if (g_lineTrack.autoFlag == LT_FLAG_STOP)
        {
            track_motor_stop();
            g_lineTrack.state = LT_STATE_IDLE;
            break;
        }

        if (g_lineTrack.cornerTurning)
            handle_corner_search(tickMs, basePwm, currentYaw);
        else
            compute_and_drive(basePwm);
        break;

    default:
        break;
    }
}

uint8_t LineTrack_IsRunning(void)
{
    return (g_lineTrack.state == LT_STATE_RUNNING) ? 1u : 0u;
}

void LineTrack_SetPID(float kp, float kd)
{
    g_lineTrack.kp = kp;
    g_lineTrack.kd = kd;
    s_trackCfg.lineKp = kp;
    s_trackCfg.lineKd = kd;
}

const LineTrack_RuntimeConfig_t *LineTrack_GetRuntimeConfig(void)
{
    return &s_trackCfg;
}

void LineTrack_ResetRuntimeConfig(void)
{
    load_runtime_config_defaults();
    g_lineTrack.kp = s_trackCfg.lineKp;
    g_lineTrack.kd = s_trackCfg.lineKd;
}

uint8_t LineTrack_SetRuntimeParam(const char *name, float value)
{
    if (name == NULL)
        return 0u;

    if (strcmp(name, "LKP") == 0)
        s_trackCfg.lineKp = value;
    else if (strcmp(name, "LKD") == 0)
        s_trackCfg.lineKd = value;
    else if (strcmp(name, "BASE_MIN") == 0)
        s_trackCfg.basePwmMin = clamp_i16((int32_t)value, 0, TRACK_PWM_MAX);
    else if (strcmp(name, "BASE_MAX") == 0)
        s_trackCfg.basePwmMax = clamp_i16((int32_t)value, 0, TRACK_PWM_MAX);
    else if (strcmp(name, "CORNER_BASE") == 0)
        s_trackCfg.cornerBasePwmMax = clamp_i16((int32_t)value, 0, TRACK_PWM_MAX);
    else if (strcmp(name, "DEV_MAX") == 0)
        s_trackCfg.devPwmMax = clamp_i16((int32_t)value, 0, TRACK_PWM_MAX);
    else if (strcmp(name, "EDGE_BASE") == 0)
        s_trackCfg.edgeBasePwmMax = clamp_i16((int32_t)value, 0, TRACK_PWM_MAX);
    else if (strcmp(name, "EDGE_DEV") == 0)
        s_trackCfg.edgeDevPwmMax = clamp_i16((int32_t)value, 0, TRACK_PWM_MAX);
    else if (strcmp(name, "SHARP_DEV") == 0)
        s_trackCfg.sharpTurnDev = (uint8_t)clamp_i16((int32_t)value, 1, 7);
    else if (strcmp(name, "CENTER_DB") == 0)
        s_trackCfg.posCenterDeadband = clamp_i16((int32_t)value, 0, 200);
    else if (strcmp(name, "POS_NEAR") == 0)
        s_trackCfg.posNearThreshold = clamp_i16((int32_t)value, 0, 350);
    else if (strcmp(name, "POS_MID") == 0)
        s_trackCfg.posMidThreshold = clamp_i16((int32_t)value, 0, 350);
    else if (strcmp(name, "POS_EDGE") == 0)
        s_trackCfg.posEdgeThreshold = clamp_i16((int32_t)value, 0, 350);
    else if (strcmp(name, "TRIM") == 0)
        s_trackCfg.positionTrim = clamp_i16((int32_t)value, -200, 200);
    else if (strcmp(name, "CROSS_MIN") == 0)
        s_trackCfg.crossMinCount = (uint8_t)clamp_i16((int32_t)value, 2, 8);
    else if (strcmp(name, "CROSS_CONFIRM") == 0)
        s_trackCfg.crossConfirmTicks = (uint8_t)clamp_i16((int32_t)value, 1, 20);
    else if (strcmp(name, "WIDE_COUNT") == 0)
        s_trackCfg.widePatternCount = (uint8_t)clamp_i16((int32_t)value, 2, 8);
    else if (strcmp(name, "CROSS_FILTER") == 0)
        s_trackCfg.crossFilter = (uint16_t)clamp_i16((int32_t)value, 1, 255);
    else if (strcmp(name, "DTERM_STEP") == 0)
        s_trackCfg.dtermStepClamp = (uint8_t)clamp_i16((int32_t)value, 0, 10);
    else if (strcmp(name, "DTERM_WIDE") == 0)
        s_trackCfg.dtermWideClamp = (uint8_t)clamp_i16((int32_t)value, 0, 10);
    else if (strcmp(name, "SLEW_CENTER") == 0)
        s_trackCfg.centerBearingSlew = (uint8_t)clamp_i16((int32_t)value, 0, 7);
    else if (strcmp(name, "SLEW_NORMAL") == 0)
        s_trackCfg.normalBearingSlew = (uint8_t)clamp_i16((int32_t)value, 1, 7);
    else if (strcmp(name, "CORNER_HITS") == 0)
        s_trackCfg.cornerStrongSideHits = (uint8_t)clamp_i16((int32_t)value, 1, 3);
    else if (strcmp(name, "CORNER_OPP") == 0)
        s_trackCfg.cornerOppositeMaxHits = (uint8_t)clamp_i16((int32_t)value, 0, 3);
    else if (strcmp(name, "CORNER_CONFIRM") == 0)
        s_trackCfg.cornerConfirmTicks = (uint8_t)clamp_i16((int32_t)value, 1, 40);
    else if (strcmp(name, "CORNER_FAST") == 0)
        s_trackCfg.cornerFastConfirmTicks = (uint8_t)clamp_i16((int32_t)value, 1, 10);
    else if (strcmp(name, "LOSS_ENTER") == 0)
        s_trackCfg.lossEnterTicks = (uint8_t)clamp_i16((int32_t)value, 1, 40);
    else if (strcmp(name, "LOSS_FORCE") == 0)
        s_trackCfg.lossForceCornerTicks = (uint8_t)clamp_i16((int32_t)value, 1, 80);
    else if (strcmp(name, "LOSS_REQ_REF") == 0)
        s_trackCfg.lossForceRequireRef = (uint8_t)clamp_i16((int32_t)value, 0, 1);
    else if (strcmp(name, "LOSS_BEAR") == 0)
        s_trackCfg.lossSearchBearing = (uint8_t)clamp_i16((int32_t)value, 1, 7);
    else if (strcmp(name, "LOSS_HBASE") == 0)
        s_trackCfg.lossHoldBasePwmMax = clamp_i16((int32_t)value, 0, TRACK_PWM_MAX);
    else if (strcmp(name, "LOSS_HDEV") == 0)
        s_trackCfg.lossHoldDevPwmMax = clamp_i16((int32_t)value, 0, TRACK_PWM_MAX);
    else if (strcmp(name, "LOSS_BASE") == 0)
        s_trackCfg.lossSearchBasePwmMax = clamp_i16((int32_t)value, 0, TRACK_PWM_MAX);
    else if (strcmp(name, "LOSS_DEV") == 0)
        s_trackCfg.lossSearchDevPwmMax = clamp_i16((int32_t)value, 0, TRACK_PWM_MAX);
    else if (strcmp(name, "OVERRUN") == 0)
        s_trackCfg.overrunLimitTicks = (uint8_t)clamp_i16((int32_t)value, 1, 200);
    else if (strcmp(name, "TURN_PWM") == 0)
        s_trackCfg.turnPwm = clamp_i16((int32_t)value, 0, TRACK_PWM_MAX);
    else if (strcmp(name, "TURN_MIN") == 0)
        s_trackCfg.turnPwmMin = clamp_i16((int32_t)value, 0, TRACK_PWM_MAX);
    else if (strcmp(name, "EXIT_POS") == 0)
        s_trackCfg.cornerExitPosThreshold = clamp_i16((int32_t)value, 0, 200);
    else if (strcmp(name, "CORNER_TO") == 0)
        s_trackCfg.cornerTimeoutMs = (uint16_t)clamp_i16((int32_t)value, 10, 5000);
    else if (strcmp(name, "RESUME_SPD") == 0)
        s_trackCfg.cornerResumeSpeedMax = value;
    else if (strcmp(name, "RECOVER_TICKS") == 0)
        s_trackCfg.cornerRecoverTicks = (uint8_t)clamp_i16((int32_t)value, 0, 50);
    else if (strcmp(name, "CENTER_LOCK") == 0)
        s_trackCfg.centerLockTicks = (uint8_t)clamp_i16((int32_t)value, 0, 50);
    else if (strcmp(name, "CENTER_DEV") == 0)
        s_trackCfg.centerLockDevPwmMax = clamp_i16((int32_t)value, 0, TRACK_PWM_MAX);
    else if (strcmp(name, "RECOVER_BASE") == 0)
        s_trackCfg.recoverBasePwmMax = clamp_i16((int32_t)value, 0, TRACK_PWM_MAX);
    else if (strcmp(name, "RECOVER_DEV") == 0)
        s_trackCfg.recoverDevPwmMax = clamp_i16((int32_t)value, 0, TRACK_PWM_MAX);
    else if (strcmp(name, "FLIP_YAW") == 0)
        s_trackCfg.cornerFlipYawDeg = value;
    else
        return 0u;

    if (s_trackCfg.basePwmMin > s_trackCfg.basePwmMax)
        s_trackCfg.basePwmMin = s_trackCfg.basePwmMax;
    if (s_trackCfg.cornerBasePwmMax > s_trackCfg.basePwmMax)
        s_trackCfg.cornerBasePwmMax = s_trackCfg.basePwmMax;
    if (s_trackCfg.edgeBasePwmMax > s_trackCfg.basePwmMax)
        s_trackCfg.edgeBasePwmMax = s_trackCfg.basePwmMax;
    if (s_trackCfg.lossHoldBasePwmMax > s_trackCfg.basePwmMax)
        s_trackCfg.lossHoldBasePwmMax = s_trackCfg.basePwmMax;
    if (s_trackCfg.lossSearchBasePwmMax > s_trackCfg.basePwmMax)
        s_trackCfg.lossSearchBasePwmMax = s_trackCfg.basePwmMax;
    if (s_trackCfg.recoverBasePwmMax > s_trackCfg.basePwmMax)
        s_trackCfg.recoverBasePwmMax = s_trackCfg.basePwmMax;
    if (s_trackCfg.turnPwmMin > s_trackCfg.turnPwm)
        s_trackCfg.turnPwmMin = s_trackCfg.turnPwm;
    if (s_trackCfg.posNearThreshold > s_trackCfg.posMidThreshold)
        s_trackCfg.posNearThreshold = s_trackCfg.posMidThreshold;
    if (s_trackCfg.posMidThreshold > s_trackCfg.posEdgeThreshold)
        s_trackCfg.posMidThreshold = s_trackCfg.posEdgeThreshold;

    g_lineTrack.kp = s_trackCfg.lineKp;
    g_lineTrack.kd = s_trackCfg.lineKd;
    return 1u;
}

void LineTrack_DumpRuntimeConfig(void)
{
    char buf[220];

    sprintf(buf, "TCFG:PID,LKP=%.2f,LKD=%.2f,TRIM=%d,SHARP_DEV=%u\r\n",
            (double)s_trackCfg.lineKp, (double)s_trackCfg.lineKd,
            (int)s_trackCfg.positionTrim, (unsigned)s_trackCfg.sharpTurnDev);
    BspUart_SendString(buf);

    sprintf(buf, "TCFG:PWM,BASE_MIN=%d,BASE_MAX=%d,CORNER_BASE=%d,DEV_MAX=%d,EDGE_BASE=%d,EDGE_DEV=%d\r\n",
            (int)s_trackCfg.basePwmMin, (int)s_trackCfg.basePwmMax,
            (int)s_trackCfg.cornerBasePwmMax, (int)s_trackCfg.devPwmMax,
            (int)s_trackCfg.edgeBasePwmMax, (int)s_trackCfg.edgeDevPwmMax);
    BspUart_SendString(buf);

    sprintf(buf, "TCFG:LOSS,LOSS_ENTER=%u,LOSS_FORCE=%u,LOSS_REQ_REF=%u,LOSS_BEAR=%u,LOSS_HBASE=%d,LOSS_HDEV=%d,LOSS_BASE=%d,LOSS_DEV=%d,OVERRUN=%u\r\n",
            (unsigned)s_trackCfg.lossEnterTicks,
            (unsigned)s_trackCfg.lossForceCornerTicks, (unsigned)s_trackCfg.lossForceRequireRef,
            (unsigned)s_trackCfg.lossSearchBearing,
            (int)s_trackCfg.lossHoldBasePwmMax, (int)s_trackCfg.lossHoldDevPwmMax,
            (int)s_trackCfg.lossSearchBasePwmMax, (int)s_trackCfg.lossSearchDevPwmMax,
            (unsigned)s_trackCfg.overrunLimitTicks);
    BspUart_SendString(buf);

    sprintf(buf, "TCFG:CORNER,TURN_PWM=%d,TURN_MIN=%d,EXIT_POS=%d,CORNER_CONFIRM=%u,CORNER_FAST=%u,CORNER_TO=%u\r\n",
            (int)s_trackCfg.turnPwm, (int)s_trackCfg.turnPwmMin,
            (int)s_trackCfg.cornerExitPosThreshold,
            (unsigned)s_trackCfg.cornerConfirmTicks, (unsigned)s_trackCfg.cornerFastConfirmTicks,
            (unsigned)s_trackCfg.cornerTimeoutMs);
    BspUart_SendString(buf);

    sprintf(buf, "TCFG:RECOVER,RECOVER_TICKS=%u,CENTER_LOCK=%u,CENTER_DEV=%d,RECOVER_BASE=%d,RECOVER_DEV=%d\r\n",
            (unsigned)s_trackCfg.cornerRecoverTicks, (unsigned)s_trackCfg.centerLockTicks,
            (int)s_trackCfg.centerLockDevPwmMax, (int)s_trackCfg.recoverBasePwmMax,
            (int)s_trackCfg.recoverDevPwmMax);
    BspUart_SendString(buf);

    sprintf(buf, "TCFG:POS,CENTER_DB=%d,POS_NEAR=%d,POS_MID=%d,POS_EDGE=%d,SLEW_CENTER=%u,SLEW_NORMAL=%u\r\n",
            (int)s_trackCfg.posCenterDeadband, (int)s_trackCfg.posNearThreshold,
            (int)s_trackCfg.posMidThreshold, (int)s_trackCfg.posEdgeThreshold,
            (unsigned)s_trackCfg.centerBearingSlew, (unsigned)s_trackCfg.normalBearingSlew);
    BspUart_SendString(buf);
}
