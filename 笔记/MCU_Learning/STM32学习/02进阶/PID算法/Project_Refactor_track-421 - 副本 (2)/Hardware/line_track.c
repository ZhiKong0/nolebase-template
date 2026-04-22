#include "line_track.h"
#include "motor_driver.h"
#include "sensor_fusion.h"
#include <string.h>

#define LT_BIT_S1           0x01u
#define LT_BIT_S2           0x02u
#define LT_BIT_S3           0x04u
#define LT_BIT_S4           0x08u
#define LT_BIT_S5           0x10u
#define LT_BIT_S6           0x20u
#define LT_BIT_S7           0x40u
#define LT_BIT_S8           0x80u

#define LT_MASK_CENTER      (LT_BIT_S4 | LT_BIT_S5)
#define LT_MASK_LEFT_ZONE   (LT_BIT_S1 | LT_BIT_S2 | LT_BIT_S3)
#define LT_MASK_RIGHT_ZONE  (LT_BIT_S6 | LT_BIT_S7 | LT_BIT_S8)
#define LT_MASK_LEFT_REACQUIRE  (LT_BIT_S3 | LT_BIT_S4 | LT_BIT_S5)
#define LT_MASK_RIGHT_REACQUIRE (LT_BIT_S4 | LT_BIT_S5 | LT_BIT_S6)

LineTrack_State_t g_lineTrack;

typedef struct
{
    uint8_t dynamicPidEnabled;
    float kpStraight;
    float kpCurve;
    float kdStraight;
    float kdCurve;
    float deadbandStraight;
    float deadbandCurve;
    float loadLow;
    float loadHigh;
    float centerAnchorStraight;
    float centerAnchorCurve;
    float steerTrim;
    float curveBrakeGain;
    float curveSpeedMinRatio;
} LineTrack_TuneConfig_t;

static LineTrack_TuneConfig_t g_lineTrackTune;

static const float s_sensorPositionWeight[8] = { -7.0f, -5.0f, -3.0f, -TRACK_CENTER_SINGLE_POSITION,
                                                  TRACK_CENTER_SINGLE_POSITION, 3.0f, 5.0f, 7.0f };

static int16_t abs_i16(int16_t value)
{
    return (value < 0) ? (int16_t)(-value) : value;
}

static float abs_f32(float value)
{
    return (value < 0.0f) ? -value : value;
}

static float clamp_f32(float value, float minValue, float maxValue)
{
    if (value < minValue)
        return minValue;
    if (value > maxValue)
        return maxValue;
    return value;
}

static float lerp_f32(float a, float b, float ratio)
{
    return a + (b - a) * ratio;
}

static int16_t float_to_i16(float value)
{
    return (value >= 0.0f) ? (int16_t)(value + 0.5f) : (int16_t)(value - 0.5f);
}

static float clamp_min_f32(float value, float minValue)
{
    return (value < minValue) ? minValue : value;
}

static void normalize_tune_config(void)
{
    g_lineTrackTune.kpStraight = clamp_f32(g_lineTrackTune.kpStraight, 0.0f, 60.0f);
    g_lineTrackTune.kpCurve = clamp_f32(g_lineTrackTune.kpCurve, 0.0f, 60.0f);
    g_lineTrackTune.kdStraight = clamp_f32(g_lineTrackTune.kdStraight, 0.0f, 40.0f);
    g_lineTrackTune.kdCurve = clamp_f32(g_lineTrackTune.kdCurve, 0.0f, 40.0f);
    g_lineTrackTune.deadbandStraight = clamp_f32(g_lineTrackTune.deadbandStraight, 0.0f, 3.0f);
    g_lineTrackTune.deadbandCurve = clamp_f32(g_lineTrackTune.deadbandCurve, 0.0f, 3.0f);
    g_lineTrackTune.loadLow = clamp_f32(g_lineTrackTune.loadLow, 0.0f, 9.8f);
    g_lineTrackTune.loadHigh = clamp_f32(g_lineTrackTune.loadHigh, 0.2f, 10.0f);
    g_lineTrackTune.centerAnchorStraight = clamp_f32(g_lineTrackTune.centerAnchorStraight, 0.0f, 1.0f);
    g_lineTrackTune.centerAnchorCurve = clamp_f32(g_lineTrackTune.centerAnchorCurve, 0.0f, 1.0f);
    g_lineTrackTune.steerTrim = clamp_f32(g_lineTrackTune.steerTrim, -80.0f, 80.0f);
    if (g_lineTrackTune.loadHigh <= (g_lineTrackTune.loadLow + 0.2f))
    {
        g_lineTrackTune.loadHigh = g_lineTrackTune.loadLow + 0.2f;
        if (g_lineTrackTune.loadHigh > 10.0f)
        {
            g_lineTrackTune.loadHigh = 10.0f;
            g_lineTrackTune.loadLow = 9.8f;
        }
    }
    g_lineTrackTune.curveBrakeGain = clamp_f32(g_lineTrackTune.curveBrakeGain, 0.0f, 0.4f);
    g_lineTrackTune.curveSpeedMinRatio = clamp_f32(g_lineTrackTune.curveSpeedMinRatio, 0.15f, 1.0f);
    g_lineTrackTune.dynamicPidEnabled = g_lineTrackTune.dynamicPidEnabled ? 1u : 0u;
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

static uint8_t bit_count(uint8_t bits)
{
    uint8_t count = 0u;

    while (bits != 0u)
    {
        if (bits & 0x01u)
            count++;
        bits >>= 1;
    }

    return count;
}

static uint8_t read_sensor_bits(void)
{
    LineSensor_Data_t sensor;

    LineSensor_Read(&sensor);
    return sensor.bits;
}

static float sensor_position_average(uint8_t bits)
{
    float sum = 0.0f;
    uint8_t count = 0u;
    uint8_t i;

    if (bits == LT_MASK_CENTER)
        return 0.0f;

    for (i = 0u; i < LINE_SENSOR_COUNT; i++)
    {
        if (bits & (uint8_t)(1u << i))
        {
            sum += s_sensorPositionWeight[i];
            count++;
        }
    }

    if (count == 0u)
        return 0.0f;

    return sum / (float)count;
}

static uint8_t is_crossing_pattern(uint8_t bits)
{
    if (bit_count(bits) < TRACK_CROSS_MIN_ACTIVE)
        return 0u;

    if ((bits & LT_MASK_CENTER) == 0u)
        return 0u;

    if ((bits & LT_MASK_LEFT_ZONE) == 0u)
        return 0u;

    if ((bits & LT_MASK_RIGHT_ZONE) == 0u)
        return 0u;

    return 1u;
}

static int8_t quantize_bearing(uint8_t bits, float position, uint8_t crossingHit)
{
    float absPosition;
    int8_t magnitude = 0;

    if (bits == 0u)
        return g_lineTrack.lastBearingDev;

    if (crossingHit)
        return 0;

    if (bits == LT_MASK_CENTER)
        return 0;

    absPosition = abs_f32(position);
    if (absPosition >= 6.0f)
        magnitude = 7;
    else if (absPosition >= 4.0f)
        magnitude = 4;
    else if (absPosition >= 2.0f)
        magnitude = 2;
    else if (absPosition >= 0.55f)
        magnitude = 1;

    if (magnitude == 0)
        return 0;

    return (position < 0.0f) ? (int8_t)(-magnitude) : magnitude;
}

static uint8_t is_center_pair_only(uint8_t bits)
{
    if ((bits & LT_MASK_CENTER) != LT_MASK_CENTER)
        return 0u;

    return ((bits & (LT_MASK_LEFT_ZONE | LT_MASK_RIGHT_ZONE)) == 0u) ? 1u : 0u;
}

static float center_zone_target(uint8_t bits, float position)
{
    uint8_t centerBits = bits & LT_MASK_CENTER;
    uint8_t leftBits = bits & LT_MASK_LEFT_ZONE;
    uint8_t rightBits = bits & LT_MASK_RIGHT_ZONE;

    if (centerBits == LT_MASK_CENTER)
    {
        if (leftBits != 0u && rightBits == 0u)
            return -TRACK_CENTER_BLEND_POSITION;
        if (rightBits != 0u && leftBits == 0u)
            return TRACK_CENTER_BLEND_POSITION;
        return 0.0f;
    }

    if (centerBits == LT_BIT_S4 && rightBits == 0u)
        return -TRACK_CENTER_SINGLE_POSITION;

    if (centerBits == LT_BIT_S5 && leftBits == 0u)
        return TRACK_CENTER_SINGLE_POSITION;

    return position;
}

static uint8_t dir_from_sensor_bits(uint8_t bits)
{
    if ((bits & LT_MASK_LEFT_ZONE) != 0u && (bits & LT_MASK_RIGHT_ZONE) == 0u)
        return LT_DIR_LEFT;

    if ((bits & LT_MASK_RIGHT_ZONE) != 0u && (bits & LT_MASK_LEFT_ZONE) == 0u)
        return LT_DIR_RIGHT;

    return LT_DIR_NONE;
}

static uint8_t fallback_dir(void)
{
    uint8_t dir = dir_from_sensor_bits(g_lineTrack.lastData);

    if (dir != LT_DIR_NONE)
        return dir;

    if (g_lineTrack.lastBearingDev < 0)
        return LT_DIR_LEFT;

    if (g_lineTrack.lastBearingDev > 0)
        return LT_DIR_RIGHT;

    return LT_DIR_RIGHT;
}

static void update_cross_state(uint8_t crossingHit)
{
    if (crossingHit)
    {
        if (g_lineTrack.crossState == 1u)
        {
            if (g_lineTrack.filterTimes < 255u)
                g_lineTrack.filterTimes++;

            if (g_lineTrack.filterTimes >= TRACK_CROSS_CONFIRM_TICKS)
            {
                g_lineTrack.crossCount++;
                g_lineTrack.crossState = 2u;
                g_lineTrack.filterTimes = 0u;

                if (g_lineTrack.crossing != 0u
                    && g_lineTrack.crossCount >= g_lineTrack.crossing)
                {
                    g_lineTrack.autoFlag = LT_FLAG_STOP;
                }
            }
        }
        else
        {
            g_lineTrack.filterTimes = 0u;
        }
    }
    else if (g_lineTrack.crossState == 2u)
    {
        if (g_lineTrack.filterTimes < 255u)
            g_lineTrack.filterTimes++;

        if (g_lineTrack.filterTimes >= TRACK_CROSS_REARM_TICKS)
        {
            g_lineTrack.crossState = 1u;
            g_lineTrack.filterTimes = 0u;
        }
    }
    else
    {
        g_lineTrack.filterTimes = 0u;
    }
}

static void update_last_data(uint8_t bits, int8_t bearingDev, uint8_t crossingHit)
{
    if (bits == 0u || crossingHit)
        return;

    if ((bits & LT_MASK_CENTER) != 0u || abs_i16((int16_t)bearingDev) >= 2)
        g_lineTrack.lastData = bits;
}

static void track_motor_stop(void)
{
    g_lineTrack.trackBasePwm = 0;
    g_lineTrack.trackPwmLeft = 0;
    g_lineTrack.trackPwmRight = 0;
    MotorDriver_Stop();
    MotorDriver_Disable();
}

static int16_t track_apply_forward_deadzone(int16_t pwm)
{
    if (pwm <= 0)
        return 0;

    if (pwm < MOTOR_DEADZONE)
        return MOTOR_DEADZONE;

    return pwm;
}

static void track_motor_forward(int16_t left, int16_t right)
{
    if (left > TRACK_PWM_MAX)
        left = TRACK_PWM_MAX;
    if (left < TRACK_PWM_MIN)
        left = TRACK_PWM_MIN;

    if (right > TRACK_PWM_MAX)
        right = TRACK_PWM_MAX;
    if (right < TRACK_PWM_MIN)
        right = TRACK_PWM_MIN;

    left = track_apply_forward_deadzone(left);
    right = track_apply_forward_deadzone(right);

    left = clamp_toward_i16(g_lineTrack.trackPwmLeft, left, TRACK_PWM_SLEW_STEP);
    right = clamp_toward_i16(g_lineTrack.trackPwmRight, right, TRACK_PWM_SLEW_STEP);

    g_lineTrack.trackPwmLeft = left;
    g_lineTrack.trackPwmRight = right;

    MotorDriver_Enable();
    MotorDriver_SetTurnPWM(left, right);
}

static void track_turn_left(void)
{
    g_lineTrack.trackBasePwm = 0;
    MotorDriver_Enable();
    MotorDriver_SetTurnPWM((int16_t)(-TRACK_TURN_PWM_SLOW), TRACK_TURN_PWM_FAST);
}

static void track_turn_right(void)
{
    g_lineTrack.trackBasePwm = 0;
    MotorDriver_Enable();
    MotorDriver_SetTurnPWM(TRACK_TURN_PWM_FAST, (int16_t)(-TRACK_TURN_PWM_SLOW));
}

static uint8_t search_accept_mask(uint8_t dir)
{
    if (dir == LT_DIR_LEFT)
        return LT_MASK_LEFT_REACQUIRE;
    if (dir == LT_DIR_RIGHT)
        return LT_MASK_RIGHT_REACQUIRE;
    return LT_MASK_CENTER;
}

static uint8_t search_exit_ready(uint8_t bits, uint8_t dir)
{
    if (bits == 0u)
        return 0u;

    if ((bits & LT_MASK_CENTER) != 0u)
        return 1u;

    if (dir == LT_DIR_LEFT)
    {
        if ((bits & LT_MASK_LEFT_REACQUIRE) == 0u)
            return 0u;
        if ((bits & LT_MASK_RIGHT_ZONE) != 0u)
            return 0u;
        return 1u;
    }

    if (dir == LT_DIR_RIGHT)
    {
        if ((bits & LT_MASK_RIGHT_REACQUIRE) == 0u)
            return 0u;
        if ((bits & LT_MASK_LEFT_ZONE) != 0u)
            return 0u;
        return 1u;
    }

    return 0u;
}

static void enter_search_state(uint32_t tickMs)
{
    g_lineTrack.searchActive = 1u;
    g_lineTrack.searchDir = fallback_dir();
    g_lineTrack.searchAcceptCount = 0u;
    g_lineTrack.searchTickCount = 0u;
    g_lineTrack.searchStartTick = tickMs;
    g_lineTrack.cornerDone = 0u;
    g_lineTrack.overrunCount = TRACK_LOST_CONFIRM_TICKS;
}

static void exit_search_state(void)
{
    g_lineTrack.searchActive = 0u;
    g_lineTrack.searchAcceptCount = 0u;
    g_lineTrack.searchTickCount = 0u;
    g_lineTrack.overrunCount = 0u;
    g_lineTrack.cornerDone = 1u;
    g_lineTrack.trackBasePwm = 0;
    g_lineTrack.trackPwmLeft = 0;
    g_lineTrack.trackPwmRight = 0;
    g_lineTrack.pidLastLinePos = g_lineTrack.filteredLinePos;
    g_lineTrack.filteredLineRate = 0.0f;
}

static void update_dynamic_track_profile(void)
{
    float alpha = 0.0f;

    if (g_lineTrackTune.dynamicPidEnabled)
    {
        float span = g_lineTrackTune.loadHigh - g_lineTrackTune.loadLow;

        if (span <= 0.001f)
        {
            alpha = (g_lineTrack.filteredCurveLoad >= g_lineTrackTune.loadHigh) ? 1.0f : 0.0f;
        }
        else
        {
            alpha = (g_lineTrack.filteredCurveLoad - g_lineTrackTune.loadLow) / span;
            alpha = clamp_f32(alpha, 0.0f, 1.0f);
        }
    }

    g_lineTrack.scheduleAlpha = alpha;
    g_lineTrack.kp = lerp_f32(g_lineTrackTune.kpStraight, g_lineTrackTune.kpCurve, alpha);
    g_lineTrack.kd = lerp_f32(g_lineTrackTune.kdStraight, g_lineTrackTune.kdCurve, alpha);
    g_lineTrack.activeSteerDeadband = lerp_f32(g_lineTrackTune.deadbandStraight,
                                               g_lineTrackTune.deadbandCurve,
                                               alpha);
    g_lineTrack.activeCenterAnchor = lerp_f32(g_lineTrackTune.centerAnchorStraight,
                                              g_lineTrackTune.centerAnchorCurve,
                                              alpha);
    g_lineTrack.activeSteerTrim = g_lineTrackTune.steerTrim;
}

static void update_curve_speed_target(void)
{
    float speedScale = 1.0f - g_lineTrackTune.curveBrakeGain * g_lineTrack.filteredCurveLoad;

    speedScale = clamp_f32(speedScale, g_lineTrackTune.curveSpeedMinRatio, 1.0f);
    g_lineTrack.curveSpeedScale = speedScale;
    g_lineTrack.curveSpeedTarget = clamp_min_f32(g_lineTrack.cruiseSpeedTarget * speedScale,
                                                 SPEED_ENTRY);
}

static void update_center_hold(uint8_t bits,
                               uint8_t crossingHit,
                               float filteredPos,
                               float filteredRate)
{
    if (crossingHit || bits == 0u)
    {
        g_lineTrack.centerHold = 0u;
        g_lineTrack.centerStableCount = 0u;
        return;
    }

    if (g_lineTrack.centerHold)
    {
        if (!is_center_pair_only(bits)
            || abs_f32(filteredPos) >= TRACK_CENTER_LOCK_EXIT)
        {
            g_lineTrack.centerHold = 0u;
            g_lineTrack.centerStableCount = 0u;
        }
        else if (abs_f32(filteredRate) >= TRACK_CENTER_LOCK_RATE_EXIT)
        {
            g_lineTrack.centerHold = 0u;
            g_lineTrack.centerStableCount = 0u;
        }
        return;
    }

    if (is_center_pair_only(bits)
        && abs_f32(filteredPos) <= TRACK_CENTER_LOCK_ENTER)
    {
        if (abs_f32(filteredRate) <= TRACK_CENTER_LOCK_RATE_ENTER)
        {
            if (g_lineTrack.centerStableCount < 255u)
                g_lineTrack.centerStableCount++;
        }
        else
        {
            g_lineTrack.centerStableCount = 0u;
        }
    }
    else
    {
        g_lineTrack.centerStableCount = 0u;
    }

    if (g_lineTrack.centerStableCount >= TRACK_CENTER_LOCK_TICKS)
        g_lineTrack.centerHold = 1u;
}

static float track_curve_load(uint8_t bits)
{
    float load = abs_f32(g_lineTrack.previewLinePos)
               + TRACK_CURVE_RATE_WEIGHT * abs_f32(g_lineTrack.filteredLineRate);

    if ((bits & LT_MASK_CENTER) == 0u
        && ((bits & LT_BIT_S1) != 0u || (bits & LT_BIT_S8) != 0u))
    {
        load += TRACK_EDGE_LOAD_BONUS;
    }

    return clamp_f32(load, 0.0f, 10.0f);
}

static float apply_center_anchor(uint8_t bits, float position)
{
    uint8_t centerBits = bits & LT_MASK_CENTER;
    float anchor = g_lineTrack.activeCenterAnchor;
    float target;

    if (centerBits == 0u)
        return position;

    target = center_zone_target(bits, position);
    if (is_center_pair_only(bits))
        anchor = 1.0f;
    else if (centerBits == LT_MASK_CENTER)
        anchor = clamp_f32(anchor + 0.22f, 0.0f, 1.0f);
    else
        anchor = clamp_f32(anchor + 0.12f, 0.0f, 1.0f);

    position = lerp_f32(position, target, anchor);
    if (centerBits == LT_MASK_CENTER)
        position = clamp_f32(position, -TRACK_CENTER_SINGLE_POSITION, TRACK_CENTER_SINGLE_POSITION);
    else
        position = clamp_f32(position, -TRACK_CENTER_SINGLE_POSITION, TRACK_CENTER_SINGLE_POSITION);

    return clamp_f32(position, -7.0f, 7.0f);
}

static float track_control_position(uint8_t bits, uint8_t crossingHit)
{
    float controlPos = g_lineTrack.previewLinePos;
    float steerDeadband = g_lineTrack.activeSteerDeadband;

    if (crossingHit)
        return 0.0f;

    if (g_lineTrack.centerHold)
        return 0.0f;

    if (is_center_pair_only(bits)
        && abs_f32(controlPos) <= steerDeadband)
    {
        return 0.0f;
    }

    if (abs_f32(controlPos) <= steerDeadband)
        return 0.0f;

    return controlPos;
}

static int16_t Dev_speed_PID(float linePos, float lineRate)
{
    float output = g_lineTrack.kp * linePos + g_lineTrack.kd * lineRate;

    output = clamp_f32(output, (float)(-TRACK_PWM_MAX), (float)TRACK_PWM_MAX);
    return float_to_i16(output);
}

static void Signal_Handler(uint32_t tickMs)
{
    uint8_t bits = read_sensor_bits();
    uint8_t crossingHit = is_crossing_pattern(bits);
    float rawPos = g_lineTrack.rawLinePos;
    float posDelta = 0.0f;
    int8_t bearingDev;

    g_lineTrack.sensorBits = bits;
    if (bits != 0u)
    {
        float previewPos;

        rawPos = crossingHit ? 0.0f : sensor_position_average(bits);
        rawPos = clamp_f32(rawPos, -7.0f, 7.0f);
        g_lineTrack.rawLinePos = rawPos;
        g_lineTrack.filteredLinePos += TRACK_LINE_POS_LPF
                                     * (rawPos - g_lineTrack.filteredLinePos);
        g_lineTrack.filteredLinePos = clamp_f32(g_lineTrack.filteredLinePos, -7.0f, 7.0f);

        posDelta = g_lineTrack.filteredLinePos - g_lineTrack.pidLastLinePos;
        g_lineTrack.filteredLineRate += TRACK_LINE_RATE_LPF
                                      * (posDelta - g_lineTrack.filteredLineRate);
        g_lineTrack.filteredLineRate = clamp_f32(g_lineTrack.filteredLineRate, -7.0f, 7.0f);
        g_lineTrack.pidLastLinePos = g_lineTrack.filteredLinePos;

        previewPos = g_lineTrack.filteredLinePos
                   + TRACK_LOOKAHEAD_GAIN * g_lineTrack.filteredLineRate;
        previewPos = clamp_f32(previewPos, -7.0f, 7.0f);
        g_lineTrack.previewLinePos = previewPos;
        update_center_hold(bits,
                           crossingHit,
                           g_lineTrack.filteredLinePos,
                           g_lineTrack.filteredLineRate);
        g_lineTrack.filteredCurveLoad += TRACK_CURVE_LOAD_LPF
                                       * (track_curve_load(bits) - g_lineTrack.filteredCurveLoad);
        g_lineTrack.filteredCurveLoad = clamp_f32(g_lineTrack.filteredCurveLoad, 0.0f, 10.0f);
        update_dynamic_track_profile();
        g_lineTrack.previewLinePos = apply_center_anchor(bits, previewPos);
        update_curve_speed_target();
    }
    else
    {
        g_lineTrack.centerHold = 0u;
        g_lineTrack.centerStableCount = 0u;
    }

    bearingDev = quantize_bearing(bits, g_lineTrack.previewLinePos, crossingHit);
    g_lineTrack.weightedPos = float_to_i16(g_lineTrack.previewLinePos * 100.0f);
    g_lineTrack.bearingDev = bearingDev;

    update_cross_state(crossingHit);
    update_last_data(bits, bearingDev, crossingHit);

    if (bits != 0u)
    {
        g_lineTrack.overrunCount = 0u;
        g_lineTrack.lastBearingDev = bearingDev;

        if (g_lineTrack.searchActive)
        {
            if (search_exit_ready(bits, g_lineTrack.searchDir))
            {
                if (g_lineTrack.searchAcceptCount < 255u)
                    g_lineTrack.searchAcceptCount++;

                if (g_lineTrack.searchAcceptCount >= TRACK_TURN_ACCEPT_TICKS)
                    exit_search_state();
            }
            else
            {
                g_lineTrack.searchAcceptCount = 0u;
            }
        }

        return;
    }

    g_lineTrack.searchAcceptCount = 0u;
    if (g_lineTrack.searchActive)
    {
        /* 进入找线后由 searchTickCount 专门负责超时，
           不再继续走“全灭累计自动停机”这条慢链，避免转到一半被误停。 */
        return;
    }

    if (g_lineTrack.overrunCount < 0xFFFFu)
        g_lineTrack.overrunCount++;

    if (!g_lineTrack.searchActive
        && g_lineTrack.overrunCount >= TRACK_LOST_CONFIRM_TICKS)
    {
        enter_search_state(tickMs);
    }

    if (g_lineTrack.overrunCount >= TRACK_OVERRUN_LIMIT_TICKS)
        g_lineTrack.autoFlag = LT_FLAG_STOP;
}

static void Track_Handler(int16_t basePwm)
{
    uint8_t crossingHit;
    float controlPos;
    int16_t devLimit;
    int16_t baseAbsPwm;
    int16_t leftPwm;
    int16_t rightPwm;

    if (g_lineTrack.autoFlag == LT_FLAG_STOP)
        return;

    if (g_lineTrack.searchActive)
    {
        g_lineTrack.pidBypassActive = 1u;
        if (g_lineTrack.searchTickCount < 0xFFFFu)
            g_lineTrack.searchTickCount++;

        if (g_lineTrack.searchTickCount >= TRACK_TURN_TIMEOUT_TICKS)
        {
            g_lineTrack.autoFlag = LT_FLAG_STOP;
            return;
        }

        if (g_lineTrack.searchDir == LT_DIR_LEFT)
            track_turn_left();
        else
            track_turn_right();

        return;
    }

    g_lineTrack.pidBypassActive = 0u;
    crossingHit = is_crossing_pattern(g_lineTrack.sensorBits);
    baseAbsPwm = abs_i16(basePwm);
    g_lineTrack.trackBasePwm = baseAbsPwm;
    controlPos = track_control_position(g_lineTrack.sensorBits, crossingHit);
    g_lineTrack.devSpeed = Dev_speed_PID(controlPos, g_lineTrack.filteredLineRate);
    g_lineTrack.devSpeed += float_to_i16(g_lineTrack.activeSteerTrim);
    devLimit = float_to_i16((float)baseAbsPwm * TRACK_DEV_MAX_RATIO);

    if (g_lineTrack.devSpeed > devLimit)
        g_lineTrack.devSpeed = devLimit;
    if (g_lineTrack.devSpeed < -devLimit)
        g_lineTrack.devSpeed = (int16_t)(-devLimit);

    leftPwm = (int16_t)(g_lineTrack.trackBasePwm + g_lineTrack.devSpeed);
    rightPwm = (int16_t)(g_lineTrack.trackBasePwm - g_lineTrack.devSpeed);
    track_motor_forward(leftPwm, rightPwm);
}

static void update_debug_fields(void)
{
    if (g_lineTrack.searchActive)
    {
        g_lineTrack.dbgTrackState = 1u;
        g_lineTrack.dbgCornerDir = g_lineTrack.searchDir;
        g_lineTrack.dbgCornerYawDelta = 0.0f;
        g_lineTrack.dbgCornerBits = g_lineTrack.sensorBits;
        g_lineTrack.dbgCornerAcceptMask = search_accept_mask(g_lineTrack.searchDir);
        g_lineTrack.dbgCornerYawReady = 1u;
        g_lineTrack.dbgCornerAcceptHit = search_exit_ready(g_lineTrack.sensorBits,
                                                           g_lineTrack.searchDir);
    }
    else
    {
        g_lineTrack.dbgTrackState = 0u;
        g_lineTrack.dbgCornerDir = LT_DIR_NONE;
        g_lineTrack.dbgCornerYawDelta = 0.0f;
        g_lineTrack.dbgCornerBits = g_lineTrack.sensorBits;
        g_lineTrack.dbgCornerAcceptMask = 0u;
        g_lineTrack.dbgCornerYawReady = 0u;
        g_lineTrack.dbgCornerAcceptHit = 0u;
    }
}

static void car_auto_track(uint32_t tickMs,
                           int16_t basePwm)
{
    Signal_Handler(tickMs);
    update_debug_fields();
    Track_Handler(basePwm);
}

void LineTrack_Init(void)
{
    memset(&g_lineTrack, 0, sizeof(g_lineTrack));
    LineTrack_LoadTuneDefaults();
    g_lineTrack.cruiseSpeedTarget = PID_TRACK_SPEED_TARGET;
    g_lineTrack.curveSpeedScale = 1.0f;
    g_lineTrack.curveSpeedTarget = PID_TRACK_SPEED_TARGET;
    g_lineTrack.startupSkipSecondTurnEnabled = 0u;
    g_lineTrack.crossState = 1u;
    g_lineTrack.lastData = LT_MASK_CENTER;
    LineTrack_Stop();
}

void LineTrack_Start(uint8_t crossings)
{
    float cruiseSpeedTarget = g_lineTrack.cruiseSpeedTarget;
    uint8_t startupCompat = g_lineTrack.startupSkipSecondTurnEnabled;

    memset(&g_lineTrack, 0, sizeof(g_lineTrack));

    g_lineTrack.state = LT_STATE_STARTING;
    g_lineTrack.autoFlag = LT_FLAG_START;
    g_lineTrack.lastData = LT_MASK_CENTER;
    g_lineTrack.crossing = crossings;
    g_lineTrack.crossState = 1u;
    g_lineTrack.cruiseSpeedTarget = clamp_min_f32(cruiseSpeedTarget, SPEED_ENTRY);
    g_lineTrack.curveSpeedScale = 1.0f;
    g_lineTrack.curveSpeedTarget = g_lineTrack.cruiseSpeedTarget;
    update_dynamic_track_profile();
    g_lineTrack.startupSkipSecondTurnEnabled = startupCompat;
}

void LineTrack_Stop(void)
{
    g_lineTrack.state = LT_STATE_IDLE;
    g_lineTrack.autoFlag = LT_FLAG_STOP;
    g_lineTrack.searchActive = 0u;
    g_lineTrack.pidBypassActive = 0u;
    g_lineTrack.searchAcceptCount = 0u;
    g_lineTrack.searchTickCount = 0u;
    g_lineTrack.overrunCount = 0u;
    g_lineTrack.devSpeed = 0;
    g_lineTrack.cornerDone = 0u;
    g_lineTrack.filteredCurveLoad = 0.0f;
    g_lineTrack.scheduleAlpha = 0.0f;
    g_lineTrack.curveSpeedScale = 1.0f;
    g_lineTrack.curveSpeedTarget = clamp_min_f32(g_lineTrack.cruiseSpeedTarget, SPEED_ENTRY);
    update_dynamic_track_profile();
    g_lineTrack.acuteState = LT_ACUTE_IDLE;
    g_lineTrack.acuteStartYaw = 0.0f;
    g_lineTrack.acuteRearmTick = 0u;
    g_lineTrack.dbgTrackState = 0u;
    g_lineTrack.dbgCornerDir = LT_DIR_NONE;
    g_lineTrack.dbgCornerYawDelta = 0.0f;
    g_lineTrack.dbgCornerAcceptMask = 0u;
    g_lineTrack.dbgCornerYawReady = 0u;
    g_lineTrack.dbgCornerAcceptHit = 0u;
    track_motor_stop();
}

void LineTrack_Update(uint32_t tickMs, int16_t basePwm)
{
    switch (g_lineTrack.state)
    {
    case LT_STATE_STARTING:
        g_lineTrack.state = LT_STATE_RUNNING;
        g_lineTrack.autoFlag = LT_FLAG_START;
        MotorDriver_Enable();
        break;

    case LT_STATE_RUNNING:
        car_auto_track(tickMs, basePwm);
        if (g_lineTrack.autoFlag == LT_FLAG_STOP)
        {
            track_motor_stop();
            g_lineTrack.state = LT_STATE_IDLE;
        }
        break;

    default:
        break;
    }
}

uint8_t LineTrack_IsRunning(void)
{
    return (g_lineTrack.state == LT_STATE_RUNNING) ? 1u : 0u;
}

void LineTrack_SetCruiseSpeed(float speed)
{
    g_lineTrack.cruiseSpeedTarget = clamp_min_f32(speed, SPEED_ENTRY);

    if (g_lineTrack.state == LT_STATE_IDLE)
    {
        g_lineTrack.curveSpeedScale = 1.0f;
        g_lineTrack.curveSpeedTarget = g_lineTrack.cruiseSpeedTarget;
    }
    else
    {
        update_curve_speed_target();
    }
}

float LineTrack_GetCruiseSpeedTarget(void)
{
    return clamp_min_f32(g_lineTrack.cruiseSpeedTarget, SPEED_ENTRY);
}

float LineTrack_GetCurveSpeedTarget(void)
{
    return clamp_min_f32(g_lineTrack.curveSpeedTarget, SPEED_ENTRY);
}

void LineTrack_LoadTuneDefaults(void)
{
    g_lineTrackTune.dynamicPidEnabled = TRACK_DYNAMIC_PID_ENABLE_DEFAULT;
    g_lineTrackTune.kpStraight = TRACK_DYNAMIC_KP_STRAIGHT_DEFAULT;
    g_lineTrackTune.kpCurve = TRACK_DYNAMIC_KP_CURVE_DEFAULT;
    g_lineTrackTune.kdStraight = TRACK_DYNAMIC_KD_STRAIGHT_DEFAULT;
    g_lineTrackTune.kdCurve = TRACK_DYNAMIC_KD_CURVE_DEFAULT;
    g_lineTrackTune.deadbandStraight = TRACK_DYNAMIC_DEADBAND_STRAIGHT_DEFAULT;
    g_lineTrackTune.deadbandCurve = TRACK_DYNAMIC_DEADBAND_CURVE_DEFAULT;
    g_lineTrackTune.loadLow = TRACK_DYNAMIC_LOAD_LOW_DEFAULT;
    g_lineTrackTune.loadHigh = TRACK_DYNAMIC_LOAD_HIGH_DEFAULT;
    g_lineTrackTune.centerAnchorStraight = TRACK_DYNAMIC_CENTER_ANCHOR_STRAIGHT_DEFAULT;
    g_lineTrackTune.centerAnchorCurve = TRACK_DYNAMIC_CENTER_ANCHOR_CURVE_DEFAULT;
    g_lineTrackTune.steerTrim = TRACK_DYNAMIC_STEER_TRIM_DEFAULT;
    g_lineTrackTune.curveBrakeGain = TRACK_CURVE_BRAKE_GAIN;
    g_lineTrackTune.curveSpeedMinRatio = TRACK_CURVE_SPEED_MIN_RATIO;
    normalize_tune_config();
    update_dynamic_track_profile();
    update_curve_speed_target();
}

void LineTrack_SetDynamicPidEnable(uint8_t enable)
{
    g_lineTrackTune.dynamicPidEnabled = enable ? 1u : 0u;
    normalize_tune_config();
    update_dynamic_track_profile();
    update_curve_speed_target();
}

uint8_t LineTrack_GetDynamicPidEnable(void)
{
    return g_lineTrackTune.dynamicPidEnabled;
}

uint8_t LineTrack_SetTuneParam(LineTrack_TuneParam_t param, float value)
{
    switch (param)
    {
    case LT_TUNE_KP_STRAIGHT:
        g_lineTrackTune.kpStraight = value;
        break;
    case LT_TUNE_KP_CURVE:
        g_lineTrackTune.kpCurve = value;
        break;
    case LT_TUNE_KD_STRAIGHT:
        g_lineTrackTune.kdStraight = value;
        break;
    case LT_TUNE_KD_CURVE:
        g_lineTrackTune.kdCurve = value;
        break;
    case LT_TUNE_DEADBAND_STRAIGHT:
        g_lineTrackTune.deadbandStraight = value;
        break;
    case LT_TUNE_DEADBAND_CURVE:
        g_lineTrackTune.deadbandCurve = value;
        break;
    case LT_TUNE_LOAD_LOW:
        g_lineTrackTune.loadLow = value;
        break;
    case LT_TUNE_LOAD_HIGH:
        g_lineTrackTune.loadHigh = value;
        break;
    case LT_TUNE_CENTER_ANCHOR_STRAIGHT:
        g_lineTrackTune.centerAnchorStraight = value;
        break;
    case LT_TUNE_CENTER_ANCHOR_CURVE:
        g_lineTrackTune.centerAnchorCurve = value;
        break;
    case LT_TUNE_STEER_TRIM:
        g_lineTrackTune.steerTrim = value;
        break;
    case LT_TUNE_CURVE_BRAKE_GAIN:
        g_lineTrackTune.curveBrakeGain = value;
        break;
    case LT_TUNE_CURVE_SPEED_MIN_RATIO:
        g_lineTrackTune.curveSpeedMinRatio = value;
        break;
    default:
        return 0u;
    }

    normalize_tune_config();
    update_dynamic_track_profile();
    update_curve_speed_target();
    return 1u;
}

float LineTrack_GetTuneParam(LineTrack_TuneParam_t param)
{
    switch (param)
    {
    case LT_TUNE_KP_STRAIGHT: return g_lineTrackTune.kpStraight;
    case LT_TUNE_KP_CURVE: return g_lineTrackTune.kpCurve;
    case LT_TUNE_KD_STRAIGHT: return g_lineTrackTune.kdStraight;
    case LT_TUNE_KD_CURVE: return g_lineTrackTune.kdCurve;
    case LT_TUNE_DEADBAND_STRAIGHT: return g_lineTrackTune.deadbandStraight;
    case LT_TUNE_DEADBAND_CURVE: return g_lineTrackTune.deadbandCurve;
    case LT_TUNE_LOAD_LOW: return g_lineTrackTune.loadLow;
    case LT_TUNE_LOAD_HIGH: return g_lineTrackTune.loadHigh;
    case LT_TUNE_CENTER_ANCHOR_STRAIGHT: return g_lineTrackTune.centerAnchorStraight;
    case LT_TUNE_CENTER_ANCHOR_CURVE: return g_lineTrackTune.centerAnchorCurve;
    case LT_TUNE_STEER_TRIM: return g_lineTrackTune.steerTrim;
    case LT_TUNE_CURVE_BRAKE_GAIN: return g_lineTrackTune.curveBrakeGain;
    case LT_TUNE_CURVE_SPEED_MIN_RATIO: return g_lineTrackTune.curveSpeedMinRatio;
    default: return 0.0f;
    }
}

float LineTrack_GetScheduleAlpha(void)
{
    return g_lineTrack.scheduleAlpha;
}

float LineTrack_GetActiveSteerDeadband(void)
{
    return g_lineTrack.activeSteerDeadband;
}

void LineTrack_SetPID(float kp, float kd)
{
    g_lineTrackTune.kpStraight = kp;
    g_lineTrackTune.kpCurve = kp;
    g_lineTrackTune.kdStraight = kd;
    g_lineTrackTune.kdCurve = kd;
    g_lineTrackTune.dynamicPidEnabled = 0u;
    normalize_tune_config();
    update_dynamic_track_profile();
}

void LineTrack_SetStartupSkipSecondTurnEnabled(uint8_t enable)
{
    g_lineTrack.startupSkipSecondTurnEnabled = enable ? 1u : 0u;
}
