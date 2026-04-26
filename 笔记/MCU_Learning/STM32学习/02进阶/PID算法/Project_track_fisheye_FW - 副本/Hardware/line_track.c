#include "line_track.h"
#include "motor_driver.h"
#include "sensor_fusion.h"
#include <string.h>
#include <stdio.h>

typedef struct
{
    uint16_t bits;
    uint8_t  activeCount;
    uint8_t  leftEdgeActive;
    uint8_t  rightEdgeActive;
    int16_t  weightedPos;
    int8_t   bearingDev;
} TrackSample_t;

LineTrack_State_t g_lineTrack;
LineTrack_RuntimeConfig_t g_lineTrackCfg;

static uint8_t s_targetCrossings = 0u;

static const int16_t s_sensorPositions[LINE_SENSOR_COUNT] = {
    210, 150, 90, 30, -30, -90, -150, -210
};

static float track_absf(float value)
{
    return (value < 0.0f) ? -value : value;
}

static float track_clampf(float value, float minValue, float maxValue)
{
    if (value < minValue) {
        return minValue;
    }
    if (value > maxValue) {
        return maxValue;
    }
    return value;
}

static int16_t track_clamp_i16(int16_t value, int16_t minValue, int16_t maxValue)
{
    if (value < minValue) {
        return minValue;
    }
    if (value > maxValue) {
        return maxValue;
    }
    return value;
}

static int16_t track_slew_i16(int16_t current, int16_t target, int16_t step)
{
    if (step <= 0) {
        return target;
    }

    if (target > (int16_t)(current + step)) {
        return (int16_t)(current + step);
    }

    if (target < (int16_t)(current - step)) {
        return (int16_t)(current - step);
    }

    return target;
}

static int16_t track_map_follow_diff_to_drive(int16_t semanticDiff)
{
#if TRACK_FOLLOW_DIFF_SIGN < 0
    return (int16_t)(-semanticDiff);
#else
    return semanticDiff;
#endif
}

static uint8_t track_abs_u8(int8_t value)
{
    return (value < 0) ? (uint8_t)(-value) : (uint8_t)value;
}

static uint8_t track_count_active_bits(uint16_t bits)
{
    uint8_t count = 0u;
    uint8_t i;

    for (i = 0u; i < LINE_SENSOR_COUNT; ++i) {
        if (bits & ((uint16_t)1u << i)) {
            count++;
        }
    }

    return count;
}

static uint8_t track_is_full_black_bits(uint16_t bits)
{
    uint16_t fullMask = (uint16_t)((1u << LINE_SENSOR_COUNT) - 1u);
    return (bits == fullMask) ? 1u : 0u;
}

static void track_arm_run_start_if_needed(uint32_t tickMs)
{
    if (g_lineTrack.runStartTickMs == 0u) {
        g_lineTrack.runStartTickMs = tickMs;
    }
}

static uint8_t track_should_stop_on_full_black(uint32_t tickMs, uint16_t rawBits)
{
    if (TRACK_FULL_BLACK_STOP_DELAY_MS == 0u) {
        return 0u;
    }

    if (!track_is_full_black_bits(rawBits)) {
        return 0u;
    }

    track_arm_run_start_if_needed(tickMs);
    return ((tickMs - g_lineTrack.runStartTickMs) >= TRACK_FULL_BLACK_STOP_DELAY_MS) ? 1u : 0u;
}

static uint8_t track_is_recover_active(void)
{
    return (g_lineTrack.recoverHoldTicks > 0u) ? 1u : 0u;
}

static uint8_t track_is_search_state(uint8_t state)
{
    return (state == LT_TRACK_SEARCH_LEFT || state == LT_TRACK_SEARCH_RIGHT) ? 1u : 0u;
}

static uint16_t track_reverse_sensor_bits(uint16_t bits)
{
    uint16_t reversed = 0u;
    uint8_t i;

    for (i = 0u; i < LINE_SENSOR_COUNT; ++i) {
        if (bits & ((uint16_t)1u << i)) {
            reversed |= (uint16_t)1u << (LINE_SENSOR_COUNT - 1u - i);
        }
    }

    return reversed;
}

static uint16_t track_normalize_sensor_bits(uint16_t bits)
{
#if TRACK_SENSOR_REVERSED
    return track_reverse_sensor_bits(bits);
#else
    return bits;
#endif
}

static uint8_t track_guess_turn_dir(uint16_t bits, int16_t weightedPos)
{
    if (bits & 0xC0u) {
        return LT_DIR_LEFT;
    }

    if (bits & 0x03u) {
        return LT_DIR_RIGHT;
    }

    if (weightedPos < 0) {
        return LT_DIR_LEFT;
    }

    if (weightedPos > 0) {
        return LT_DIR_RIGHT;
    }

    return LT_DIR_NONE;
}

static int8_t track_quantize_bearing(int16_t weightedPos)
{
    if (weightedPos <= -TRACK_LINE_POS_LARGE_MAX) return -7;
    if (weightedPos <= -TRACK_LINE_POS_MEDIUM_MAX) return -4;
    if (weightedPos <= -TRACK_LINE_POS_SMALL_MAX) return -2;
    if (weightedPos <= -TRACK_LINE_POS_CENTER_MAX) return -1;
    if (weightedPos >= TRACK_LINE_POS_LARGE_MAX) return 7;
    if (weightedPos >= TRACK_LINE_POS_MEDIUM_MAX) return 4;
    if (weightedPos >= TRACK_LINE_POS_SMALL_MAX) return 2;
    if (weightedPos >= TRACK_LINE_POS_CENTER_MAX) return 1;
    return 0;
}

static float track_compute_control_error(float linePos)
{
    return track_clampf(linePos / TRACK_CONTROL_POS_UNIT, -7.0f, 7.0f);
}

static float track_select_steer_gain(float absError)
{
    if (absError >= 6.0f) {
        return TRACK_STEER_GAIN_LARGE;
    }

    if (absError >= 4.0f) {
        return TRACK_STEER_GAIN_MEDIUM;
    }

    if (absError >= 2.0f) {
        return TRACK_STEER_GAIN_SMALL;
    }

    return 1.0f;
}

static uint8_t track_is_single_edge_sample(const TrackSample_t *sample)
{
    if (!sample) {
        return 0u;
    }

    return (sample->leftEdgeActive != sample->rightEdgeActive) ? 1u : 0u;
}

static uint8_t track_should_boost_turn_entry(const TrackSample_t *sample, float absError)
{
    if (!track_is_single_edge_sample(sample)) {
        return 0u;
    }

    return (absError >= 3.0f) ? 1u : 0u;
}

static uint8_t track_should_use_pivot_recover(const TrackSample_t *sample, float absError)
{
    if (!track_is_single_edge_sample(sample)) {
        return 0u;
    }

    if (absError < TRACK_PIVOT_TRIGGER_ERROR) {
        return 0u;
    }

    if (sample->activeCount > 3u) {
        return 0u;
    }

    return 1u;
}

static void track_apply_drive_output(int16_t basePwm,
                                     int16_t semanticDiff,
                                     const TrackSample_t *sample,
                                     float absError)
{
    int16_t driveDiff = track_map_follow_diff_to_drive(semanticDiff);

    if (track_should_use_pivot_recover(sample, absError)) {
        int16_t recoverBase = (int16_t)(basePwm * TRACK_PIVOT_RECOVER_BASE);
        int16_t recoverDiff = (int16_t)(driveDiff * TRACK_PIVOT_RECOVER_DIFF);
        int16_t left = (int16_t)(recoverBase + recoverDiff);
        int16_t right = (int16_t)(recoverBase - recoverDiff);

        MotorDriver_SetTurnPWM(left, right);
        return;
    }

    MotorDriver_SetCoreDiff(basePwm, driveDiff);
}

static void track_load_default_runtime_config(void)
{
    uint8_t i;

    for (i = 0u; i < LINE_SENSOR_COUNT; ++i) {
        g_lineTrackCfg.sensorScale[i] = 1.0f;
    }

    g_lineTrackCfg.devRatio = TRACK_FOLLOW_DEV_RATIO;
    g_lineTrackCfg.deadband = TRACK_FOLLOW_DEADBAND;
    g_lineTrackCfg.posFilterAlpha = TRACK_FOLLOW_POS_LPF_ALPHA;
    g_lineTrackCfg.dFilterAlpha = TRACK_FOLLOW_D_LPF_ALPHA;
    g_lineTrackCfg.staticSteerBias = TRACK_STATIC_STEER_BIAS;
    g_lineTrackCfg.diffSlewStep = TRACK_DIFF_SLEW_STEP;
    g_lineTrackCfg.recoverTicks = TRACK_RECOVER_TICKS;
    g_lineTrackCfg.searchArcPwmFast = TRACK_SEARCH_ARC_PWM_FAST;
    g_lineTrackCfg.searchArcPwmSlow = TRACK_SEARCH_ARC_PWM_SLOW;
    g_lineTrackCfg.searchTurnPwmFast = TRACK_SEARCH_TURN_PWM_FAST;
    g_lineTrackCfg.searchTurnPwmSlow = TRACK_SEARCH_TURN_PWM_SLOW;
}

static void track_reset_follow_filters(void)
{
    g_lineTrack.filteredTrackError = 0.0f;
    g_lineTrack.lastFilteredTrackError = 0.0f;
    g_lineTrack.filteredDTerm = 0.0f;
    g_lineTrack.smoothedLinePos = 0.0f;
    g_lineTrack.devSpeed = 0;
    g_lineTrack.lastDevSpeedCmd = 0;
}

static void track_reset_runtime_state(void)
{
    g_lineTrack.state = LT_STATE_IDLE;
    g_lineTrack.trackState = LT_TRACK_FOLLOW;
    g_lineTrack.sensorData = 0u;
    g_lineTrack.lastData = 0u;
    g_lineTrack.bearingDev = 0;
    g_lineTrack.linePos = 0;
    g_lineTrack.lastValidLinePos = 0;
    g_lineTrack.runStartTickMs = 0u;
    g_lineTrack.crossReleaseTicks = 0u;
    g_lineTrack.crossCount = 0u;
    g_lineTrack.crossState = LT_CROSS_READY;
    g_lineTrack.overrunCount = 0u;
    g_lineTrack.lastTurnDir = LT_DIR_NONE;
    g_lineTrack.cornerLatchDir = LT_DIR_NONE;
    g_lineTrack.searchDir = LT_DIR_NONE;
    g_lineTrack.searchPhase = LT_SEARCH_PHASE_ARC;
    g_lineTrack.searchSeenTicks = 0u;
    g_lineTrack.cornerDone = 0u;
    g_lineTrack.cornerLatchTicks = 0u;
    g_lineTrack.recoverHoldTicks = 0u;
    g_lineTrack.recoverDir = LT_DIR_NONE;
    g_lineTrack.recoverTicks = 0u;
    g_lineTrack.searchTicks = 0u;
    g_lineTrack.activeKp = g_lineTrack.kp;
    g_lineTrack.activeKd = g_lineTrack.kd;
    g_lineTrack.gainStage = 0u;
    g_lineTrack.dbgTrackState = LT_TRACK_FOLLOW;
    g_lineTrack.dbgTurnDir = LT_DIR_NONE;
    g_lineTrack.dbgCrossActive = 0u;
    g_lineTrack.dbgTelemState = LT_TLM_STATE_TRACK;
    g_lineTrack.dbgScoreEnabled = 0u;
    g_lineTrack.dbgActiveCount = 0u;
    g_lineTrack.dbgSearchLost = 0u;
    g_lineTrack.dbgSearchReacquired = 0u;
    g_lineTrack.dbgCornerCandidateDir = LT_DIR_NONE;
    g_lineTrack.dbgResolvedSearchDir = LT_DIR_NONE;
    g_lineTrack.dbgResolvedSource = 0u;
    g_lineTrack.dbgTelemFlags = 0u;
    track_reset_follow_filters();
}

static int16_t track_compute_weighted_position(uint16_t bits, uint8_t activeCount)
{
    float sum = 0.0f;
    uint8_t i;

    if (activeCount == 0u) {
        return 0;
    }

    for (i = 0u; i < LINE_SENSOR_COUNT; ++i) {
        if (bits & ((uint16_t)1u << i)) {
            sum += (float)s_sensorPositions[i] * g_lineTrackCfg.sensorScale[i];
        }
    }

    return (int16_t)(sum / (float)activeCount);
}

static void track_refresh_sample(TrackSample_t *sample)
{
    if (!sample) {
        return;
    }

    sample->activeCount = track_count_active_bits(sample->bits);
    sample->leftEdgeActive = (sample->bits & 0xC0u) ? 1u : 0u;
    sample->rightEdgeActive = (sample->bits & 0x03u) ? 1u : 0u;
    sample->weightedPos = track_compute_weighted_position(sample->bits, sample->activeCount);
    sample->bearingDev = track_quantize_bearing(sample->weightedPos);
}

static void track_apply_two_led_turn_bias(TrackSample_t *sample, uint8_t turnDir)
{
    uint16_t selectedBit = 0u;
    uint8_t i;

    if (!sample || sample->activeCount != 2u) {
        return;
    }

    if (turnDir == LT_DIR_RIGHT) {
        for (i = LINE_SENSOR_COUNT; i > 0u; --i) {
            uint8_t idx = (uint8_t)(i - 1u);
            if (sample->bits & ((uint16_t)1u << idx)) {
                selectedBit = (uint16_t)1u << idx;
                break;
            }
        }
    } else if (turnDir == LT_DIR_LEFT) {
        for (i = 0u; i < LINE_SENSOR_COUNT; ++i) {
            if (sample->bits & ((uint16_t)1u << i)) {
                selectedBit = (uint16_t)1u << i;
                break;
            }
        }
    }

    if (selectedBit == 0u) {
        return;
    }

    sample->bits = selectedBit;
    track_refresh_sample(sample);
}

static uint8_t track_resolve_recent_turn_dir(void)
{
    if (g_lineTrack.cornerLatchDir != LT_DIR_NONE &&
        g_lineTrack.cornerLatchTicks <= TRACK_CORNER_LATCH_TICKS) {
        return g_lineTrack.cornerLatchDir;
    }

    return g_lineTrack.lastTurnDir;
}

static uint8_t track_resolve_turn_bias_dir(const TrackSample_t *sample)
{
    if (!sample) {
        return LT_DIR_NONE;
    }

    if (track_is_search_state(g_lineTrack.trackState)) {
        return g_lineTrack.searchDir;
    }

    if (track_is_recover_active() && g_lineTrack.recoverDir != LT_DIR_NONE) {
        return g_lineTrack.recoverDir;
    }

    if (sample->activeCount == 2u) {
        return track_resolve_recent_turn_dir();
    }

    if (sample->activeCount >= TRACK_CROSS_MIN_ACTIVE) {
        return track_resolve_recent_turn_dir();
    }

    return LT_DIR_NONE;
}

static void track_apply_wide_turn_bias(TrackSample_t *sample, uint8_t turnDir)
{
    uint16_t mask = 0u;

    if (!sample ||
        sample->activeCount < TRACK_CROSS_MIN_ACTIVE ||
        !sample->leftEdgeActive ||
        !sample->rightEdgeActive) {
        return;
    }

    if (turnDir == LT_DIR_RIGHT) {
        mask = 0x0Fu;
    } else if (turnDir == LT_DIR_LEFT) {
        mask = 0xF0u;
    } else {
        return;
    }

    sample->bits &= mask;
    track_refresh_sample(sample);
}

static void track_apply_turn_bias(TrackSample_t *sample)
{
    uint8_t turnDir = track_resolve_turn_bias_dir(sample);

    if (turnDir == LT_DIR_NONE || !sample) {
        return;
    }

    track_apply_two_led_turn_bias(sample, turnDir);
    track_apply_wide_turn_bias(sample, turnDir);
}

static TrackSample_t track_sample_from_sensor(const LineSensor_Data_t *line)
{
    TrackSample_t sample;

    sample.bits = track_normalize_sensor_bits(line ? line->bits : 0u);
    sample.activeCount = line ? line->count : 0u;
    sample.leftEdgeActive = 0u;
    sample.rightEdgeActive = 0u;
    sample.weightedPos = 0;
    sample.bearingDev = 0;
    track_refresh_sample(&sample);
    return sample;
}

static void track_set_follow_telem(const TrackSample_t *sample, uint8_t crossActive)
{
    uint8_t absBearing = track_abs_u8(sample->bearingDev);
    uint16_t flags = 0u;

    if (sample->bearingDev == 0) {
        flags |= LT_TLM_FLAG_CENTER | LT_TLM_FLAG_STRAIGHT;
        g_lineTrack.dbgTelemState = LT_TLM_STATE_STRAIGHT;
        g_lineTrack.gainStage = 0u;
    } else if (absBearing >= 7u) {
        flags |= LT_TLM_FLAG_EDGE;
        g_lineTrack.dbgTelemState = LT_TLM_STATE_EDGE;
        g_lineTrack.gainStage = 3u;
    } else if (absBearing >= 4u) {
        g_lineTrack.dbgTelemState = LT_TLM_STATE_SCURVE;
        g_lineTrack.gainStage = 2u;
    } else {
        g_lineTrack.dbgTelemState = LT_TLM_STATE_TRACK;
        g_lineTrack.gainStage = 1u;
    }

    if (g_lineTrack.recoverHoldTicks > 0u) {
        flags |= LT_TLM_FLAG_TRIM;
    }

    if (crossActive) {
        flags |= LT_TLM_FLAG_CROSS;
        g_lineTrack.dbgTelemState = LT_TLM_STATE_CROSS;
        g_lineTrack.dbgTrackState = LT_TRACK_CROSS;
    } else {
        g_lineTrack.dbgTrackState = LT_TRACK_FOLLOW;
    }

    g_lineTrack.dbgTelemFlags = flags;
}

static void track_update_cross_state(uint8_t crossActive)
{
    if (crossActive) {
        g_lineTrack.dbgCrossActive = 1u;
        g_lineTrack.crossReleaseTicks = 0u;
        if (g_lineTrack.crossState != LT_CROSS_SEEN) {
            if (g_lineTrack.crossCount < 255u) {
                g_lineTrack.crossCount++;
            }
            g_lineTrack.crossState = LT_CROSS_SEEN;
        }
        return;
    }

    g_lineTrack.dbgCrossActive = 0u;

    if (g_lineTrack.crossState == LT_CROSS_SEEN) {
        if (g_lineTrack.crossReleaseTicks < 255u) {
            g_lineTrack.crossReleaseTicks++;
        }
        if (g_lineTrack.crossReleaseTicks >= TRACK_CROSS_RELEASE_TICKS) {
            g_lineTrack.crossReleaseTicks = 0u;
            g_lineTrack.crossState = LT_CROSS_READY;
        }
    } else {
        g_lineTrack.crossState = LT_CROSS_READY;
    }
}

static uint8_t track_choose_search_dir(void)
{
    uint8_t dir = LT_DIR_NONE;
    uint8_t source = 0u;

    if (g_lineTrack.cornerLatchDir != LT_DIR_NONE &&
        g_lineTrack.cornerLatchTicks <= TRACK_CORNER_LATCH_TICKS) {
        dir = g_lineTrack.cornerLatchDir;
        source = 1u;
    } else if (g_lineTrack.lastData & 0x03u) {
        dir = LT_DIR_LEFT;
        source = 2u;
    } else if (g_lineTrack.lastData & 0xC0u) {
        dir = LT_DIR_RIGHT;
        source = 2u;
    } else if (g_lineTrack.lastTurnDir != LT_DIR_NONE) {
        dir = g_lineTrack.lastTurnDir;
        source = 3u;
    } else if (g_lineTrack.lastValidLinePos < 0) {
        dir = LT_DIR_LEFT;
        source = 4u;
    } else if (g_lineTrack.lastValidLinePos > 0) {
        dir = LT_DIR_RIGHT;
        source = 4u;
    } else {
        dir = LT_DIR_LEFT;
        source = 5u;
    }

    g_lineTrack.dbgResolvedSearchDir = dir;
    g_lineTrack.dbgResolvedSource = source;
    return dir;
}

static void track_enter_search(uint8_t dir)
{
    if (dir != LT_DIR_RIGHT) {
        dir = LT_DIR_LEFT;
    }

    g_lineTrack.trackState = (dir == LT_DIR_LEFT) ? LT_TRACK_SEARCH_LEFT : LT_TRACK_SEARCH_RIGHT;
    g_lineTrack.searchDir = dir;
    g_lineTrack.searchPhase = LT_SEARCH_PHASE_PIVOT;
    g_lineTrack.searchTicks = 0u;
    g_lineTrack.searchSeenTicks = 0u;
    g_lineTrack.cornerLatchDir = dir;
    g_lineTrack.cornerLatchTicks = 0u;
    g_lineTrack.recoverDir = LT_DIR_NONE;
    g_lineTrack.recoverTicks = 0u;
    g_lineTrack.recoverHoldTicks = 0u;
    g_lineTrack.dbgTrackState = g_lineTrack.trackState;
    g_lineTrack.dbgTurnDir = dir;
    g_lineTrack.dbgSearchLost = 1u;
    g_lineTrack.dbgSearchReacquired = 0u;
    g_lineTrack.dbgTelemState = (dir == LT_DIR_LEFT) ? LT_TLM_STATE_SEARCH_LEFT : LT_TLM_STATE_SEARCH_RIGHT;
    g_lineTrack.dbgTelemFlags = LT_TLM_FLAG_SEARCH | LT_TLM_FLAG_LOST | LT_TLM_FLAG_PIVOT;
    g_lineTrack.dbgScoreEnabled = 0u;
    g_lineTrack.devSpeed = 0;
    g_lineTrack.lastDevSpeedCmd = 0;
}

static void track_drive_search(void)
{
    int16_t left;
    int16_t right;

    /* Motor driver applies MOTOR_*_DIR_SIGN before touching the H-bridge.
     * Keep searchDir semantics aligned with the physical chassis rotation. */
    if (g_lineTrack.searchDir == LT_DIR_RIGHT) {
        left = (int16_t)(-((int16_t)g_lineTrackCfg.searchTurnPwmSlow));
        right = (int16_t)g_lineTrackCfg.searchTurnPwmFast;
    } else {
        left = (int16_t)g_lineTrackCfg.searchTurnPwmFast;
        right = (int16_t)(-((int16_t)g_lineTrackCfg.searchTurnPwmSlow));
    }

    MotorDriver_SetTurnPWM(left, right);
    g_lineTrack.searchTicks++;
    if (g_lineTrack.cornerLatchTicks < 255u) {
        g_lineTrack.cornerLatchTicks++;
    }
}

static uint8_t track_search_reacquire_matches(const TrackSample_t *sample)
{
    if (!sample || sample->activeCount == 0u) {
        return 0u;
    }

    if (sample->activeCount == 2u) {
        if (g_lineTrack.searchDir == LT_DIR_RIGHT) {
            if (sample->leftEdgeActive && !sample->rightEdgeActive) {
                return 1u;
            }

            return (sample->weightedPos < 0) ? 1u : 0u;
        }

        if (g_lineTrack.searchDir == LT_DIR_LEFT) {
            if (sample->rightEdgeActive && !sample->leftEdgeActive) {
                return 1u;
            }

            return (sample->weightedPos > 0) ? 1u : 0u;
        }
    }

    return 1u;
}

static uint8_t track_try_exit_search(const TrackSample_t *sample)
{
    if (!track_search_reacquire_matches(sample)) {
        g_lineTrack.searchSeenTicks = 0u;
        return 0u;
    }

    if (g_lineTrack.searchSeenTicks < 255u) {
        g_lineTrack.searchSeenTicks++;
    }

    if (g_lineTrack.searchSeenTicks < TRACK_SEARCH_REACQUIRE_TICKS) {
        return 0u;
    }

    g_lineTrack.trackState = LT_TRACK_FOLLOW;
    g_lineTrack.searchDir = LT_DIR_NONE;
    g_lineTrack.searchPhase = LT_SEARCH_PHASE_ARC;
    g_lineTrack.searchTicks = 0u;
    g_lineTrack.searchSeenTicks = 0u;
    g_lineTrack.recoverDir = g_lineTrack.dbgTurnDir;
    g_lineTrack.recoverTicks = g_lineTrackCfg.recoverTicks;
    g_lineTrack.recoverHoldTicks = g_lineTrackCfg.recoverTicks;
    g_lineTrack.dbgSearchLost = 0u;
    g_lineTrack.dbgSearchReacquired = 1u;
    g_lineTrack.dbgTrackState = LT_TRACK_FOLLOW;
    g_lineTrack.dbgTurnDir = g_lineTrack.recoverDir;
    track_reset_follow_filters();
    return 1u;
}

static int16_t track_scale_base_pwm(int16_t basePwm,
                                    float absError,
                                    uint8_t crossActive,
                                    uint8_t turnEntryBoost)
{
    float scale = 1.0f;
    uint8_t recoverActive = track_is_recover_active();

    if (absError >= 6.0f) {
        scale = TRACK_BASE_SCALE_LARGE;
    } else if (absError >= 4.0f) {
        scale = TRACK_BASE_SCALE_MEDIUM;
    } else if (absError >= 2.0f) {
        scale = TRACK_BASE_SCALE_SMALL;
    }

    if (crossActive) {
        scale *= TRACK_CROSS_BASE_SCALE;
    }

    if (recoverActive) {
        scale *= TRACK_RECOVER_BASE_SCALE;
    }

    if (turnEntryBoost && scale < 0.92f) {
        scale = 0.92f;
    }

    if (recoverActive && scale < TRACK_RECOVER_MIN_BASE_SCALE) {
        scale = TRACK_RECOVER_MIN_BASE_SCALE;
    }

    basePwm = (int16_t)((float)basePwm * scale);
    if (basePwm < MOTOR_DEADZONE) {
        basePwm = MOTOR_DEADZONE;
    }
    if (basePwm > TRACK_PWM_MAX) {
        basePwm = TRACK_PWM_MAX;
    }

    return basePwm;
}

static void track_apply_follow_control(const TrackSample_t *sample, int16_t basePwm, uint8_t crossActive)
{
    float error;
    float absError;
    float steerGain;
    float filteredError;
    float dRaw;
    float output;
    int16_t diffLimit;
    int16_t diff;
    int16_t diffCmd;
    int16_t diffSlewStep;
    uint8_t recoverActive;
    uint8_t turnEntryBoost;
    uint8_t turnDir;

    g_lineTrack.smoothedLinePos +=
        g_lineTrackCfg.posFilterAlpha * ((float)sample->weightedPos - g_lineTrack.smoothedLinePos);

    error = track_compute_control_error(g_lineTrack.smoothedLinePos);
    if (track_absf(error) <= g_lineTrackCfg.deadband) {
        error = 0.0f;
    }
    absError = track_absf(error);
    steerGain = track_select_steer_gain(absError);
    recoverActive = track_is_recover_active();
    turnEntryBoost = track_should_boost_turn_entry(sample, absError);
    if (turnEntryBoost) {
        steerGain *= TRACK_TURN_ENTRY_GAIN_BOOST;
    }
    if (recoverActive) {
        steerGain *= TRACK_RECOVER_GAIN_BOOST;
    }

    filteredError = g_lineTrack.filteredTrackError +
                    g_lineTrackCfg.posFilterAlpha * (error - g_lineTrack.filteredTrackError);
    dRaw = filteredError - g_lineTrack.lastFilteredTrackError;
    g_lineTrack.filteredDTerm +=
        g_lineTrackCfg.dFilterAlpha * (dRaw - g_lineTrack.filteredDTerm);

    g_lineTrack.lastFilteredTrackError = filteredError;
    g_lineTrack.filteredTrackError = filteredError;
    g_lineTrack.activeKp = g_lineTrack.kp * steerGain;
    g_lineTrack.activeKd = g_lineTrack.kd * steerGain;

    output = (g_lineTrack.activeKp * filteredError) +
             (g_lineTrack.activeKd * g_lineTrack.filteredDTerm);
    output += (float)g_lineTrackCfg.staticSteerBias;

    basePwm = track_scale_base_pwm(basePwm, absError, crossActive, turnEntryBoost);
    diffLimit = (int16_t)((float)basePwm * g_lineTrackCfg.devRatio);
    if (turnEntryBoost) {
        diffLimit = (int16_t)((float)diffLimit * TRACK_TURN_ENTRY_DIFF_BOOST);
    }
    if (recoverActive) {
        diffLimit = (int16_t)((float)diffLimit * TRACK_RECOVER_DIFF_BOOST);
    }
    if (diffLimit < 60) {
        diffLimit = 60;
    }
    if (diffLimit > TRACK_DIFF_MAX) {
        diffLimit = TRACK_DIFF_MAX;
    }

    diff = (output >= 0.0f) ? (int16_t)(output + 0.5f) : (int16_t)(output - 0.5f);
    diff = track_clamp_i16(diff, (int16_t)(-diffLimit), diffLimit);
    diffSlewStep = (int16_t)g_lineTrackCfg.diffSlewStep;
    if (recoverActive) {
        diffSlewStep = (int16_t)((float)diffSlewStep * TRACK_RECOVER_SLEW_BOOST);
    }
    diffCmd = track_slew_i16(g_lineTrack.lastDevSpeedCmd, diff, diffSlewStep);

    g_lineTrack.linePos = sample->weightedPos;
    g_lineTrack.bearingDev = sample->bearingDev;
    g_lineTrack.devSpeed = diffCmd;
    g_lineTrack.lastDevSpeedCmd = diffCmd;
    g_lineTrack.lastValidLinePos = sample->weightedPos;
    g_lineTrack.sensorData = sample->bits;
    g_lineTrack.lastData = sample->bits;
    g_lineTrack.dbgActiveCount = sample->activeCount;
    g_lineTrack.dbgCornerCandidateDir =
        (sample->leftEdgeActive && !sample->rightEdgeActive) ? LT_DIR_LEFT :
        (sample->rightEdgeActive && !sample->leftEdgeActive) ? LT_DIR_RIGHT :
        LT_DIR_NONE;

    turnDir = track_guess_turn_dir(sample->bits, sample->weightedPos);
    if (turnDir != LT_DIR_NONE) {
        g_lineTrack.lastTurnDir = turnDir;
        g_lineTrack.dbgTurnDir = turnDir;
    }

    if (sample->leftEdgeActive != sample->rightEdgeActive) {
        g_lineTrack.cornerLatchDir = turnDir;
        g_lineTrack.cornerLatchTicks = 0u;
    } else if (g_lineTrack.cornerLatchTicks < 255u) {
        g_lineTrack.cornerLatchTicks++;
    }

    if (g_lineTrack.recoverHoldTicks > 0u) {
        g_lineTrack.recoverHoldTicks--;
        g_lineTrack.recoverTicks = g_lineTrack.recoverHoldTicks;
    } else {
        g_lineTrack.recoverTicks = 0u;
        g_lineTrack.recoverDir = LT_DIR_NONE;
    }

    g_lineTrack.dbgScoreEnabled = 1u;
    track_set_follow_telem(sample, crossActive);
    track_apply_drive_output(basePwm, diffCmd, sample, absError);
}

void LineTrack_Init(void)
{
    memset(&g_lineTrack, 0, sizeof(g_lineTrack));
    track_load_default_runtime_config();
    g_lineTrack.kp = PID_TRACK_LINE_KP;
    g_lineTrack.kd = PID_TRACK_LINE_KD;
    g_lineTrack.activeKp = g_lineTrack.kp;
    g_lineTrack.activeKd = g_lineTrack.kd;
    track_reset_runtime_state();
}

void LineTrack_Start(uint8_t crossings)
{
    s_targetCrossings = crossings;
    track_reset_runtime_state();
    g_lineTrack.state = LT_STATE_RUNNING;
}

void LineTrack_Stop(void)
{
    track_reset_runtime_state();
    MotorDriver_Stop();
}

void LineTrack_Update(uint32_t tickMs, int16_t basePwm, float currentYawRate)
{
    LineSensor_Data_t line;
    TrackSample_t rawSample;
    TrackSample_t sample;
    uint8_t crossActive;
    (void)currentYawRate;

    if (g_lineTrack.state != LT_STATE_RUNNING) {
        return;
    }

    track_arm_run_start_if_needed(tickMs);

    LineSensor_Read(&line);
    rawSample = track_sample_from_sensor(&line);
    sample = rawSample;
    track_apply_turn_bias(&sample);
    crossActive = (sample.activeCount >= TRACK_CROSS_MIN_ACTIVE) ? 1u : 0u;

    g_lineTrack.sensorData = sample.bits;
    g_lineTrack.linePos = sample.weightedPos;
    g_lineTrack.bearingDev = sample.bearingDev;
    g_lineTrack.dbgActiveCount = sample.activeCount;
    g_lineTrack.dbgCornerCandidateDir =
        (sample.leftEdgeActive && !sample.rightEdgeActive) ? LT_DIR_LEFT :
        (sample.rightEdgeActive && !sample.leftEdgeActive) ? LT_DIR_RIGHT :
        LT_DIR_NONE;

    track_update_cross_state(crossActive);

    if (s_targetCrossings > 0u && g_lineTrack.crossCount >= s_targetCrossings) {
        g_lineTrack.state = LT_STATE_IDLE;
        MotorDriver_Stop();
        return;
    }

    if (sample.activeCount == 0u) {
        g_lineTrack.dbgSearchLost = 1u;
        g_lineTrack.dbgSearchReacquired = 0u;
        g_lineTrack.dbgScoreEnabled = 0u;
        g_lineTrack.dbgTelemFlags = LT_TLM_FLAG_LOST;

        if (g_lineTrack.overrunCount < 255u) {
            g_lineTrack.overrunCount++;
        }

        if (!track_is_search_state(g_lineTrack.trackState) &&
            g_lineTrack.overrunCount >= TRACK_LOST_CONFIRM_TICKS) {
            track_enter_search(track_choose_search_dir());
        }

        if (track_is_search_state(g_lineTrack.trackState)) {
            track_drive_search();
        } else {
            MotorDriver_SetCoreDiff(basePwm, 0);
        }
        return;
    }

    g_lineTrack.overrunCount = 0u;
    g_lineTrack.dbgSearchLost = 0u;

    if (track_is_search_state(g_lineTrack.trackState)) {
        if (!track_try_exit_search(&sample)) {
            track_drive_search();
            return;
        }
    }

    track_apply_follow_control(&sample, basePwm, crossActive);
}

uint8_t LineTrack_IsRunning(void)
{
    return (g_lineTrack.state == LT_STATE_RUNNING) ? 1u : 0u;
}

uint8_t LineTrack_PollFullBlackMarker(uint32_t tickMs)
{
    LineSensor_Data_t line;

    if (g_lineTrack.state != LT_STATE_RUNNING) {
        return 0u;
    }

    LineSensor_Read(&line);
    if (!track_should_stop_on_full_black(tickMs, track_normalize_sensor_bits(line.bits))) {
        return 0u;
    }

    g_lineTrack.runStartTickMs = tickMs;
    return 1u;
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
}

static uint8_t track_parse_sensor_scale_index(const char *key, uint8_t *index)
{
    const char *suffix;
    uint8_t value = 0u;

    if (!key || !index) {
        return 0u;
    }

    suffix = strstr(key, "track.sensor_scale");
    if (suffix != key) {
        return 0u;
    }

    suffix += 18;
    if (*suffix == '\0') {
        return 0u;
    }

    while (*suffix >= '0' && *suffix <= '9') {
        value = (uint8_t)(value * 10u + (uint8_t)(*suffix - '0'));
        suffix++;
    }

    if (*suffix != '\0' || value == 0u || value > LINE_SENSOR_COUNT) {
        return 0u;
    }

    *index = (uint8_t)(value - 1u);
    return 1u;
}

uint8_t LineTrack_ParamSet(const char *key, float value, float *appliedValue)
{
    uint8_t index;
    float applied = value;

    if (!key) {
        return 0u;
    }

    if (track_parse_sensor_scale_index(key, &index)) {
        applied = track_clampf(value, 0.20f, 3.00f);
        g_lineTrackCfg.sensorScale[index] = applied;
    } else if (strcmp(key, "track.dev_ratio") == 0) {
        applied = track_clampf(value, 0.50f, 4.00f);
        g_lineTrackCfg.devRatio = applied;
    } else if (strcmp(key, "track.deadband") == 0) {
        applied = track_clampf(value, 0.0f, 3.0f);
        g_lineTrackCfg.deadband = applied;
    } else if (strcmp(key, "track.pos_lpf") == 0) {
        applied = track_clampf(value, 0.0f, 1.0f);
        g_lineTrackCfg.posFilterAlpha = applied;
    } else if (strcmp(key, "track.d_lpf") == 0) {
        applied = track_clampf(value, 0.0f, 1.0f);
        g_lineTrackCfg.dFilterAlpha = applied;
    } else if (strcmp(key, "track.static_bias") == 0) {
        applied = track_clampf(value, -200.0f, 200.0f);
        g_lineTrackCfg.staticSteerBias = (int16_t)applied;
    } else if (strcmp(key, "track.diff_slew") == 0) {
        applied = track_clampf(value, 0.0f, (float)TRACK_DIFF_MAX);
        g_lineTrackCfg.diffSlewStep = (uint16_t)(applied + 0.5f);
        applied = (float)g_lineTrackCfg.diffSlewStep;
    } else if (strcmp(key, "track.recover_ticks") == 0) {
        applied = track_clampf(value, 0.0f, 80.0f);
        g_lineTrackCfg.recoverTicks = (uint8_t)(applied + 0.5f);
        applied = (float)g_lineTrackCfg.recoverTicks;
    } else if (strcmp(key, "track.search_turn_fast") == 0) {
        applied = track_clampf(value, 0.0f, (float)TRACK_PWM_MAX);
        g_lineTrackCfg.searchTurnPwmFast = (uint16_t)(applied + 0.5f);
        applied = (float)g_lineTrackCfg.searchTurnPwmFast;
    } else if (strcmp(key, "track.search_turn_slow") == 0) {
        applied = track_clampf(value, 0.0f, (float)TRACK_PWM_MAX);
        g_lineTrackCfg.searchTurnPwmSlow = (uint16_t)(applied + 0.5f);
        applied = (float)g_lineTrackCfg.searchTurnPwmSlow;
    } else {
        return 0u;
    }

    if (appliedValue) {
        *appliedValue = applied;
    }
    return 1u;
}

uint8_t LineTrack_ParamGet(const char *key, float *value)
{
    uint8_t index;

    if (!key || !value) {
        return 0u;
    }

    if (track_parse_sensor_scale_index(key, &index)) {
        *value = g_lineTrackCfg.sensorScale[index];
    } else if (strcmp(key, "track.dev_ratio") == 0) {
        *value = g_lineTrackCfg.devRatio;
    } else if (strcmp(key, "track.deadband") == 0) {
        *value = g_lineTrackCfg.deadband;
    } else if (strcmp(key, "track.pos_lpf") == 0) {
        *value = g_lineTrackCfg.posFilterAlpha;
    } else if (strcmp(key, "track.d_lpf") == 0) {
        *value = g_lineTrackCfg.dFilterAlpha;
    } else if (strcmp(key, "track.static_bias") == 0) {
        *value = (float)g_lineTrackCfg.staticSteerBias;
    } else if (strcmp(key, "track.diff_slew") == 0) {
        *value = (float)g_lineTrackCfg.diffSlewStep;
    } else if (strcmp(key, "track.recover_ticks") == 0) {
        *value = (float)g_lineTrackCfg.recoverTicks;
    } else if (strcmp(key, "track.search_turn_fast") == 0) {
        *value = (float)g_lineTrackCfg.searchTurnPwmFast;
    } else if (strcmp(key, "track.search_turn_slow") == 0) {
        *value = (float)g_lineTrackCfg.searchTurnPwmSlow;
    } else {
        return 0u;
    }

    return 1u;
}

void LineTrack_ParamList(char *out, uint16_t outSize)
{
    uint16_t used = 0u;
    uint8_t i;

    if (!out || outSize == 0u) {
        return;
    }

    used += (uint16_t)snprintf(out + used, outSize - used,
                               "track.dev_ratio,track.deadband,track.pos_lpf,"
                               "track.d_lpf,track.static_bias,track.diff_slew,track.recover_ticks,"
                               "track.search_turn_fast,track.search_turn_slow");

    for (i = 0u; i < LINE_SENSOR_COUNT && used < outSize; ++i) {
        used += (uint16_t)snprintf(out + used, outSize - used,
                                   ",track.sensor_scale%u",
                                   (unsigned)(i + 1u));
    }

    if (used >= outSize) {
        out[outSize - 1u] = '\0';
    }
}
