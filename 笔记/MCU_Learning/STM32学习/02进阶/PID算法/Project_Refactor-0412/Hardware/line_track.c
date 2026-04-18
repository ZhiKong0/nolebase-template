/**
 * @file  line_track.c
 * @brief 8-channel line tracking state machine
 *
 *  8-sensor layout:  S1  S2  S3  S4  S5  S6  S7  S8  (2 middle: S4,S5)
 *
 *  Key adaptation: "center" = S4 and/or S5 active.
 */

#include "line_track.h"
#include "sensor_fusion.h"
#include "motor_driver.h"
#include "Delay.h"
#include "config.h"

/* ========== Global State ========== */
LineTrack_State_t g_lineTrack;

/* ========== Helpers ========== */

static uint8_t bit_count(uint8_t b)
{
    uint8_t c = 0, i;
    for (i = 0; i < 8; i++) {
        if (b & (1u << i)) c++;
    }
    return c;
}

/* Weighted position from 8-sensor bits.
 * Returns value in approx [-700, +700].  Positive = line is to the LEFT.
 * Each sensor weight: S1=-350 S2=-250 S3=-150 S4=-50 S5=+50 S6=+150 S7=+250 S8=+350
 */
static const int16_t s_weights[8] = { -350, -250, -150, -50, 50, 150, 250, 350 };

static int16_t weighted_position(uint8_t bits)
{
    int32_t sum = 0;
    uint8_t count = 0, i;
    for (i = 0; i < 8; i++) {
        if (bits & (1u << i)) {
            sum += s_weights[i];
            count++;
        }
    }
    if (count == 0) return 0;
    return (int16_t)(sum / (int32_t)count);
}

/* Map weighted position to bearing_dev [-7, +7].
 * Positive = car偏左 = need steer right (same convention as 5-sensor).
 *
 * In 5-sensor code:
 *   bearing_dev > 0 → PID outputs positive devSpeed
 *   → Motor_L = base + dev (faster), Motor_R = base - dev (slower)
 *   → car steers RIGHT → corrects LEFT deviation.
 *
 * weighted_position: S1=-350(far left) ... S8=+350(far right).
 *   pos > 0 → line is on RIGHT side → car is LEFT of line → "偏左".
 *   pos < 0 → line is on LEFT side  → car is RIGHT of line → "偏右".
 *
 * 5-sensor reference:
 *   case 0x0c (M+R0, line right): bearing_dev = +1 → "偏左"
 *   case 0x18 (L0+M, line left):  bearing_dev = -1 → "偏右"
 *
 * bearing_dev = pos / 50 (same sign, no negation).
 *   pos > 0 (line RIGHT) → bearing_dev > 0 → "偏左" → steer RIGHT → correct.
 *   pos < 0 (line LEFT)  → bearing_dev < 0 → "偏右" → steer LEFT  → correct.
 */
static int8_t position_to_bearing(int16_t pos)
{
    int16_t dev = (int16_t)((int32_t)pos / 50);  /* /50 gives range [-7, +7] */
    if (dev > 7) dev = 7;
    if (dev < -7) dev = -7;
    return (int8_t)dev;
}

/* ========== Sensor Reading ========== */

static uint8_t read_sensor_bits(void)
{
    LineSensor_Data_t d;
    LineSensor_Read(&d);
    return d.bits;
}

/* ========== Motor Helpers (use existing driver) ========== */

static void track_motor_stop(void)
{
    MotorDriver_Stop();
    MotorDriver_Disable();
}

static void track_motor_forward(int16_t left, int16_t right)
{
    /* 不用 SetDiffPWM! 那个函数会对每个轮子独立加死区，会吞掉差速。
       用 SetTurnPWM 直接设置 PWM，死区已在 basePwm(速度环输出) 中体现。 */
    MotorDriver_Enable();
    MotorDriver_SetTurnPWM(left, right);
}

static void acute_enter_recover(uint32_t tickMs)
{
    g_lineTrack.acuteState = LT_ACUTE_RECOVER;
    g_lineTrack.acuteRecoverStartTick = tickMs;
    g_lineTrack.acuteRecoverStableCount = 0;
    g_lineTrack.acuteRearmTick = tickMs + TRACK_ACUTE_REARM_MS;
    g_lineTrack.cornerDone = 1;
    g_lineTrack.overrunCount = 0;
    g_lineTrack.bearingDev = 0;
    g_lineTrack.weightedPos = 0;
    g_lineTrack.lastPos = 0;
    g_lineTrack.filteredDPos = 0.0f;
}

static void acute_drive_recovery(int16_t basePwm)
{
    int16_t base = basePwm;
    int16_t diff = TRACK_ACUTE_RECOVER_DIFF_PWM;
    int16_t left, right;

    if (base < TRACK_ACUTE_RECOVER_BASE_PWM)
        base = TRACK_ACUTE_RECOVER_BASE_PWM;
    if (base > TRACK_PWM_MAX)
        base = TRACK_PWM_MAX;

    if (diff > (base - MOTOR_DEADZONE))
        diff = (int16_t)(base - MOTOR_DEADZONE);
    if (diff < 0)
        diff = 0;

    if (g_lineTrack.acuteSide == LT_DIR_LEFT)
    {
        left = (int16_t)(base - diff);
        right = (int16_t)(base + diff);
    }
    else
    {
        left = (int16_t)(base + diff);
        right = (int16_t)(base - diff);
    }

    if (left > TRACK_PWM_MAX) left = TRACK_PWM_MAX;
    if (left < MOTOR_DEADZONE) left = MOTOR_DEADZONE;
    if (right > TRACK_PWM_MAX) right = TRACK_PWM_MAX;
    if (right < MOTOR_DEADZONE) right = MOTOR_DEADZONE;

    track_motor_forward(left, right);
}

static void acute_enter_corner_search(uint32_t tickMs, float currentYaw)
{
    g_lineTrack.acuteState = LT_ACUTE_IDLE;
    g_lineTrack.cornerTurning = 1;
    g_lineTrack.cornerDir = g_lineTrack.acuteSide;
    g_lineTrack.rightAngleAssist = 0;
    g_lineTrack.rightAngleAcceptSeen = 0;
    g_lineTrack.cornerStartTick = tickMs;
    g_lineTrack.cornerStartYaw = currentYaw;
    g_lineTrack.overrunCount = TRACK_CORNER_CONFIRM;
    g_lineTrack.lastPos = g_lineTrack.weightedPos;
    g_lineTrack.filteredDPos = 0.0f;
}

static uint8_t acute_recover_line_ready(uint8_t bits, int16_t pos)
{
    int16_t absPos = (pos >= 0) ? pos : (int16_t)(-pos);

    if ((bits & LT_MASK_ACUTE_ZONE) == 0)
        return 0;

    /* 方向感知:
       左锐角恢复时不应再看到最右侧强触发;
       右锐角恢复时不应再看到最左侧强触发。 */
    if (g_lineTrack.acuteSide == LT_DIR_LEFT)
    {
        if (bits & LT_MASK_FAR_RIGHT)
            return 0;
    }
    else if (g_lineTrack.acuteSide == LT_DIR_RIGHT)
    {
        if (bits & LT_MASK_FAR_LEFT)
            return 0;
    }

    /* 居中约束:
       若当前没有S4/S5，则要求位置偏差已经明显回到中心附近。 */
    if ((bits & LT_MASK_MID) == 0 && absPos > TRACK_ACUTE_RECOVER_EXIT_POS_MAX)
        return 0;

    return 1;
}

static uint8_t acute_trigger_allowed(uint8_t bits, uint8_t side)
{
    /* 连续同侧三灯(S1/S2/S3 或 S6/S7/S8)更像普通直角,
       不要误进锐角状态机。 */
    if (side == LT_DIR_LEFT)
    {
        if ((bits & (LT_MASK_FAR_LEFT | LT_MASK_LEFT)) == (LT_MASK_FAR_LEFT | LT_MASK_LEFT))
            return 0;
    }
    else if (side == LT_DIR_RIGHT)
    {
        if ((bits & (LT_MASK_FAR_RIGHT | LT_MASK_RIGHT)) == (LT_MASK_FAR_RIGHT | LT_MASK_RIGHT))
            return 0;
    }

    return 1;
}

static uint8_t corner_current_frame_consistent(uint8_t bits, uint8_t dir, uint8_t strict)
{
    if (bits == 0x00)
        return 0;

    if (dir == LT_DIR_RIGHT)
    {
        if (strict)
        {
            if (bits & (LT_MASK_FAR_LEFT | LT_MASK_LEFT))
                return 0;
        }
        else
        {
            if (bits & LT_MASK_FAR_LEFT)
                return 0;
        }
    }
    else if (dir == LT_DIR_LEFT)
    {
        if (strict)
        {
            if (bits & (LT_MASK_FAR_RIGHT | LT_MASK_RIGHT))
                return 0;
        }
        else
        {
            if (bits & LT_MASK_FAR_RIGHT)
                return 0;
        }
    }

    return 1;
}

static uint8_t right_angle_trigger_match(uint8_t bits, uint8_t side)
{
    if (side == LT_DIR_LEFT)
        return (((bits & (LT_MASK_FAR_LEFT | LT_MASK_LEFT)) == (LT_MASK_FAR_LEFT | LT_MASK_LEFT)) ? 1u : 0u);
    if (side == LT_DIR_RIGHT)
        return (((bits & (LT_MASK_FAR_RIGHT | LT_MASK_RIGHT)) == (LT_MASK_FAR_RIGHT | LT_MASK_RIGHT)) ? 1u : 0u);
    return 0u;
}

static uint8_t crossing_keep_straight_match(uint8_t bits, uint8_t count)
{
    if (count < TRACK_CROSS_BYPASS_MIN_COUNT)
        return 0u;
    if ((bits & (LT_MASK_FAR_LEFT | LT_MASK_LEFT)) == 0u)
        return 0u;
    if ((bits & (LT_MASK_FAR_RIGHT | LT_MASK_RIGHT)) == 0u)
        return 0u;
    if ((bits & LT_MASK_MID) == 0u)
        return 0u;
    return 1u;
}

static void enter_cross_bypass(uint32_t tickMs, uint8_t bits)
{
    uint8_t centerBits = (uint8_t)(bits & LT_MASK_ACUTE_ZONE);

    g_lineTrack.cornerTurning = 0;
    g_lineTrack.rightAngleAssist = 0;
    g_lineTrack.rightAngleAcceptSeen = 0;
    g_lineTrack.crossBypassActive = 1;
    g_lineTrack.crossBypassUntilTick = tickMs + TRACK_CROSS_BYPASS_HOLD_MS;
    g_lineTrack.overrunCount = 0;

    if (centerBits != 0u)
    {
        g_lineTrack.weightedPos = weighted_position(centerBits);
        g_lineTrack.lastPos = g_lineTrack.weightedPos;
        g_lineTrack.filteredDPos = 0.0f;
    }
}

static void enter_right_angle_turn(uint32_t tickMs, uint8_t dir, uint8_t bits)
{
    g_lineTrack.cornerTurning = 1;
    g_lineTrack.rightAngleAssist = 1;
    g_lineTrack.rightAngleAcceptSeen = 0;
    g_lineTrack.cornerDir = dir;
    g_lineTrack.cornerStartTick = tickMs;
    g_lineTrack.cornerStartYaw = g_lineTrack._currentYaw;
    g_lineTrack.rightAngleRearmTick = tickMs + TRACK_RIGHT_ANGLE_REARM_MS;
    g_lineTrack.overrunCount = 0;
    g_lineTrack.lastCornerBits = bits;
}

/* Pivot turn: one wheel forward, one backward.
 * Uses MotorDriver_SetDiffPWM with negative values handled by
 * calling the lower-level motor_set functions via SetDiffPWM.
 * BUT SetDiffPWM clamps negatives to 0!
 * So we need a raw approach — directly call the public API
 * with the understanding that for turns we bypass deadzone.
 *
 * Solution: We add MotorDriver_SetTurnPWM() to motor_driver.
 * For now, implement using SetCoreDiff with a trick, or
 * just add the function.
 *
 * Actually, let's use the simplest approach: set direction + PWM directly.
 * We'll call MotorDriver_SetDiffPWM for the positive wheel
 * and handle direction via core/diff manipulation.
 *
 * Simplest: left turn = right forward, left backward.
 *   → MotorDriver_SetCoreDiff(0, -turnPwm) won't work (core=0).
 *
 * Let's just use the raw motor functions through a new public API.
 */

/* Forward declaration - we'll add this to motor_driver */
extern void MotorDriver_SetTurnPWM(int16_t left, int16_t right);

static void track_turn_left(int16_t leftPwm, int16_t rightPwm)
{
    /* Left turn: left backward, right forward */
    MotorDriver_Enable();
    MotorDriver_SetTurnPWM(-leftPwm, rightPwm);
}

static void track_turn_right(int16_t leftPwm, int16_t rightPwm)
{
    /* Right turn: left forward, right backward */
    MotorDriver_Enable();
    MotorDriver_SetTurnPWM(leftPwm, -rightPwm);
}

/* corner_handler 已废弃, 改为 LineTrack_Update 中的非阻塞状态机 */

/* ========== Signal Handler (from 5-sensor, adapted for 8) ========== */
/*
 * Reads sensors, detects corners/crossings, updates bearingDev.
 */
static void signal_handler(volatile uint32_t *pTickMs)
{
    uint8_t bits, count;
    int16_t rawPos;

    g_lineTrack.filterTimes++;
    if (g_lineTrack.filterTimes >= TRACK_CROSS_FILTER)
        g_lineTrack.filterTimes = TRACK_CROSS_FILTER;

    bits = read_sensor_bits();
    g_lineTrack.sensorBits = bits;
    count = bit_count(bits);

    if (g_lineTrack.acuteState == LT_ACUTE_IDLE
        && !g_lineTrack.cornerTurning
        && crossing_keep_straight_match(bits, count))
    {
        g_lineTrack.crossBypassUntilTick = *pTickMs + TRACK_CROSS_BYPASS_HOLD_MS;
    }
    g_lineTrack.crossBypassActive = (uint8_t)((g_lineTrack.acuteState == LT_ACUTE_IDLE
                                  && !g_lineTrack.cornerTurning
                                  && *pTickMs < g_lineTrack.crossBypassUntilTick) ? 1u : 0u);

    /* ===== 锐角检测: 时间窗监控 (acuteState==1) =====
     * 在时间窗T内: 对侧S8/S1也亮→交叉口, 撤销; T到期对侧没亮→确认锐角 */
    if (g_lineTrack.acuteState == LT_ACUTE_WINDOW)
    {
        uint32_t elapsed = *pTickMs - g_lineTrack.acuteStartTick;
        uint8_t oppBit = (g_lineTrack.acuteSide == LT_DIR_LEFT) ? 0x80 : 0x01;
        if (bits & oppBit)
        {
            g_lineTrack.acuteState = LT_ACUTE_IDLE;  /* 对侧也亮 → 交叉口, 不是锐角 */
            g_lineTrack.acuteRearmTick = *pTickMs + TRACK_ACUTE_REARM_MS;
        }
        else if (elapsed >= TRACK_ACUTE_WINDOW_MS)
        {
            g_lineTrack.acuteState = LT_ACUTE_TURNING;  /* 确认锐角, 进入差速转弯 */
            g_lineTrack.acuteStartTick = *pTickMs;
            g_lineTrack.acuteStartYaw = g_lineTrack._currentYaw;
            g_lineTrack.overrunCount = 0;  /* 清除丢线计数, 防止corner干扰 */
        }
    }

    /* If any sensor sees line, reset overrun
     * 但在丢线累积过程中, 如果检测到的是旧线方向(反方向), 不重置
     * 这防止锐角处旧线在远端传感器闪现导致永远无法进入旋转模式 */
    if (g_lineTrack.crossBypassActive)
    {
        uint8_t centerBits = (uint8_t)(bits & LT_MASK_ACUTE_ZONE);
        g_lineTrack.overrunCount = 0;
        if (centerBits != 0u)
        {
            rawPos = weighted_position(centerBits);
            g_lineTrack.bearingDev = position_to_bearing(rawPos);
        }
        else
        {
            rawPos = g_lineTrack.weightedPos;
        }
    }
    else if (bits != 0x00)
    {
        uint8_t shouldReset = 1;
        if (g_lineTrack.overrunCount > 0)
        {
            /* 已在丢线计数中: 判断检测到的线是否在反方向(旧线) */
            if ((g_lineTrack.lastCornerBits & LT_MASK_FAR_LEFT) && (bits & (LT_MASK_FAR_RIGHT | LT_MASK_RIGHT)))
                shouldReset = 0;  /* 最后向左丢线, 但右侧检测到 = 旧线, 不重置 */
            else if ((g_lineTrack.lastCornerBits & LT_MASK_FAR_RIGHT) && (bits & (LT_MASK_FAR_LEFT | LT_MASK_LEFT)))
                shouldReset = 0;  /* 最后向右丢线, 但左侧检测到 = 旧线, 不重置 */
        }
        if (shouldReset)
            g_lineTrack.overrunCount = 0;
    }

    /* Track outermost sensor activity for corner direction */
    if (!g_lineTrack.crossBypassActive && (bits & (LT_MASK_FAR_LEFT | LT_MASK_FAR_RIGHT)))
        g_lineTrack.lastCornerBits = bits;

    /* ===== Case 1: 全灭 — 弯道/脱轨 =====
     * 策略:
     *   短暂丢线(<CORNER_CONFIRM): 冻结位置, PD惯性纠偏
     *   持续丢线(>=CORNER_CONFIRM): 进入非阻塞原地旋转找线
     *   超时(OVERRUN_LIMIT): 安全停车
     */
    /* ===== 交叉直行绕过: 只用中间4路循迹, 屏蔽转弯入口 ===== */
    if (g_lineTrack.crossBypassActive)
    {
        /* rawPos 已在前面按 S3~S6 计算/冻结 */
    }
    /* ===== 锐角转弯执行中: 冻结位置, 跳过角落/交叉检测 ===== */
    else if (g_lineTrack.acuteState == LT_ACUTE_TURNING)
    {
        rawPos = g_lineTrack.weightedPos;  /* 冻结, 电机控制由Update接管 */
    }
    else if (g_lineTrack.acuteState == LT_ACUTE_RECOVER)
    {
        g_lineTrack.overrunCount = 0;
        rawPos = (bits != 0x00) ? weighted_position(bits) : 0;
        g_lineTrack.bearingDev = position_to_bearing(rawPos);
    }
    /* ===== Case 1: 全灭 — 弯道/脱轨 ===== */
    else if (bits == 0x00)
    {
        if (!g_lineTrack.cornerTurning)
            g_lineTrack.overrunCount++;  /* 旋转模式下不累加, 由IMU角度控制 */

        if (g_lineTrack.cornerTurning)
        {
            /* 正在旋转找线: 保持位置冻结, 由Update中的IMU逻辑控制 */
            rawPos = g_lineTrack.weightedPos;
        }
        else if (g_lineTrack.overrunCount < TRACK_CORNER_CONFIRM)
        {
            /* 短暂丢线: 冻结当前位置, PD保持惯性转向 */
            rawPos = g_lineTrack.weightedPos;
        }
        else if (g_lineTrack.overrunCount < TRACK_OVERRUN_LIMIT)
        {
            /* 进入非阻塞原地旋转模式 */
            g_lineTrack.cornerTurning = 1;
            g_lineTrack.cornerStartTick = *pTickMs;
            g_lineTrack.cornerStartYaw = g_lineTrack._currentYaw;
            g_lineTrack.rightAngleAssist = 0;
            g_lineTrack.rightAngleAcceptSeen = 0;
            if (g_lineTrack.lastCornerBits & LT_MASK_FAR_LEFT)
                g_lineTrack.cornerDir = LT_DIR_LEFT;
            else
                g_lineTrack.cornerDir = LT_DIR_RIGHT;
            rawPos = g_lineTrack.weightedPos;
        }
        else
        {
            /* 超时: 不停车, 重置计数继续尝试, 只靠手动按键停 */
            g_lineTrack.overrunCount = 0;
            rawPos = g_lineTrack.weightedPos;
        }
    }
    /* ===== Case 2+3: 有传感器亮 — 正常巡线 (交叉口检测已移除) ===== */
    else
    {
        rawPos = weighted_position(bits);
        g_lineTrack.bearingDev = position_to_bearing(rawPos);

        /* 锐角触发检测: 中间区域有线(S3~S6) + 最外侧S1或S8同时亮
         * 放宽中间判定: 小车稍偏时入线可能在S3或S6而非S4/S5
         * 正常弯道线渐移到边缘时中间区不亮, 只有锐角V型才同时亮 */
        if (g_lineTrack.acuteState == LT_ACUTE_IDLE
            && *pTickMs >= g_lineTrack.acuteRearmTick
            && (bits & LT_MASK_ACUTE_ZONE))
        {
            if (bits & 0x01)       /* S1 (最左) 触发 → 左侧锐角 */
            {
                if (acute_trigger_allowed(bits, LT_DIR_LEFT))
                {
                    g_lineTrack.acuteState = LT_ACUTE_WINDOW;
                    g_lineTrack.acuteSide = LT_DIR_LEFT;
                    g_lineTrack.acuteStartTick = *pTickMs;
                }
            }
            else if (bits & 0x80)  /* S8 (最右) 触发 → 右侧锐角 */
            {
                if (acute_trigger_allowed(bits, LT_DIR_RIGHT))
                {
                    g_lineTrack.acuteState = LT_ACUTE_WINDOW;
                    g_lineTrack.acuteSide = LT_DIR_RIGHT;
                    g_lineTrack.acuteStartTick = *pTickMs;
                }
            }
        }
    }

    /* ===== 统一位置低通滤波 =====
     * 所有情况都经过这里，消除离散传感器跳变和状态切换的阶跃：
     *   - 正常巡线：S4→S5 跳变被平滑为渐变
     *   - 短暂丢线：位置渐变趋向0，不会瞬间清零
     *   - 过交叉口：位置渐变趋向0
     *   - 恢复巡线：位置渐变回到实际值，D项不会产生尖峰 */
    g_lineTrack.weightedPos = (int16_t)(
        TRACK_POS_LPF * (float)rawPos +
        (1.0f - TRACK_POS_LPF) * (float)g_lineTrack.weightedPos);
}

/* ========== Track PID (from 5-sensor bsp_pid_control.c) ========== */

static int16_t dev_speed_pid(int16_t pos)
{
    float dPos = (float)(pos - g_lineTrack.lastPos);

    /* D项低通滤波：平滑传感器离散跳变，防止D尖峰 */
    g_lineTrack.filteredDPos = TRACK_DERIV_LPF * dPos
                             + (1.0f - TRACK_DERIV_LPF) * g_lineTrack.filteredDPos;

    float pwm_f = g_lineTrack.kp * (float)pos
                + g_lineTrack.kd * g_lineTrack.filteredDPos;
    int16_t pwm = (int16_t)pwm_f;
    g_lineTrack.lastPos = pos;
    return pwm;
}

static void compute_and_drive(int16_t basePwm)
{
    int16_t left, right, devMax;

    g_lineTrack.devSpeed = dev_speed_pid(g_lineTrack.weightedPos);

    /* 限制差速幅度: 内轮至少保留 (1-RATIO)*basePwm 的前进速度,
     * 防止内轮频繁停转导致走走停停 */
    devMax = (int16_t)(basePwm * TRACK_DEV_MAX_RATIO);
    if (g_lineTrack.devSpeed > devMax)  g_lineTrack.devSpeed = devMax;
    if (g_lineTrack.devSpeed < -devMax) g_lineTrack.devSpeed = -devMax;

    left  = (int16_t)(basePwm + g_lineTrack.devSpeed);
    right = (int16_t)(basePwm - g_lineTrack.devSpeed);

    if (left > TRACK_PWM_MAX)  left  = TRACK_PWM_MAX;
    if (left < TRACK_PWM_MIN)  left  = TRACK_PWM_MIN;
    if (right > TRACK_PWM_MAX) right = TRACK_PWM_MAX;
    if (right < TRACK_PWM_MIN) right = TRACK_PWM_MIN;

    track_motor_forward(left, right);
}

/* ========== Public API ========== */

void LineTrack_Init(void)
{
    /* GPIO init is done by LineSensor_Init() in main */
    g_lineTrack.kp = PID_TRACK_LINE_KP;   /* load compile-time defaults */
    g_lineTrack.kd = PID_TRACK_LINE_KD;
    LineTrack_Stop();
}

void LineTrack_Start(uint8_t crossings)
{
    g_lineTrack.state = LT_STATE_STARTING;
    g_lineTrack.autoFlag = LT_FLAG_START;
    g_lineTrack.bearingDev = 0;
    g_lineTrack.sensorBits = 0;
    g_lineTrack.lastCornerBits = LT_MASK_MID;  /* assume centered at start */
    g_lineTrack.overrunCount = 0;
    g_lineTrack.filterTimes = 0;
    g_lineTrack.crossing = crossings;
    g_lineTrack.crossCount = 0;
    g_lineTrack.crossState = 1;
    g_lineTrack.devSpeed = 0;
    g_lineTrack.weightedPos = 0;
    g_lineTrack.lastPos = 0;
    g_lineTrack.filteredDPos = 0.0f;
    g_lineTrack.cornerDone = 0;
    g_lineTrack.cornerTurning = 0;
    g_lineTrack.cornerDir = 0;
    g_lineTrack.cornerStartTick = 0;
    g_lineTrack.crossBypassActive = 0;
    g_lineTrack.crossBypassUntilTick = 0;
    g_lineTrack.rightAngleAssist = 0;
    g_lineTrack.rightAngleRearmTick = 0;
    g_lineTrack.rightAngleAcceptSeen = 0;
    g_lineTrack.acuteState = LT_ACUTE_IDLE;
    g_lineTrack.acuteSide = 0;
    g_lineTrack.acuteStartTick = 0;
    g_lineTrack.acuteStartYaw = 0.0f;
    g_lineTrack.acuteRearmTick = 0;
    g_lineTrack.acuteRecoverStartTick = 0;
    g_lineTrack.acuteRecoverStableCount = 0;
    g_lineTrack.dbgTrackState = 0;
    g_lineTrack.dbgCornerDir = 0;
    g_lineTrack.dbgCornerYawDelta = 0.0f;
    g_lineTrack.dbgCornerBits = 0;
    g_lineTrack.dbgCornerAcceptMask = 0;
    g_lineTrack.dbgCornerYawReady = 0;
    g_lineTrack.dbgCornerAcceptHit = 0;
}

void LineTrack_Stop(void)
{
    g_lineTrack.state = LT_STATE_IDLE;
    g_lineTrack.autoFlag = LT_FLAG_STOP;
    track_motor_stop();
}

void LineTrack_Update(uint32_t tickMs, int16_t basePwm, float currentYaw)
{
    volatile uint32_t tick = tickMs;
    g_lineTrack._currentYaw = currentYaw;  /* 供 signal_handler 记录起始角度 */

    switch (g_lineTrack.state)
    {
    case LT_STATE_STARTING:
        g_lineTrack.autoFlag = LT_FLAG_START;
        g_lineTrack.state = LT_STATE_RUNNING;
        MotorDriver_Enable();
        break;

    case LT_STATE_RUNNING:
        signal_handler(&tick);
        g_lineTrack.dbgTrackState = 0;
        g_lineTrack.dbgCornerDir = 0;
        g_lineTrack.dbgCornerYawDelta = 0.0f;
        g_lineTrack.dbgCornerBits = 0;
        g_lineTrack.dbgCornerAcceptMask = 0;
        g_lineTrack.dbgCornerYawReady = 0;
        g_lineTrack.dbgCornerAcceptHit = 0;

        if (g_lineTrack.crossBypassActive)
            g_lineTrack.dbgTrackState = 6;

        if (!g_lineTrack.cornerTurning
            && g_lineTrack.acuteState == LT_ACUTE_IDLE
            && tickMs >= g_lineTrack.rightAngleRearmTick)
        {
            if ((g_lineTrack.sensorBits & 0x01u) && right_angle_trigger_match(g_lineTrack.sensorBits, LT_DIR_LEFT))
            {
                enter_right_angle_turn(tickMs, LT_DIR_LEFT, g_lineTrack.sensorBits);
            }
            else if ((g_lineTrack.sensorBits & 0x80u) && right_angle_trigger_match(g_lineTrack.sensorBits, LT_DIR_RIGHT))
            {
                enter_right_angle_turn(tickMs, LT_DIR_RIGHT, g_lineTrack.sensorBits);
            }
        }

        if (g_lineTrack.autoFlag == LT_FLAG_STOP)
        {
            g_lineTrack.cornerTurning = 0;
            g_lineTrack.rightAngleAssist = 0;
            g_lineTrack.rightAngleAcceptSeen = 0;
            g_lineTrack.acuteState = LT_ACUTE_IDLE;
            track_motor_stop();
            g_lineTrack.state = LT_STATE_IDLE;
            break;
        }
        /* ===== 锐角差速转弯 (最高优先级, 禁止反向自转) =====
         * 外轮快+内轮慢, 都前进, 沿触发侧搜索弯后延伸线
         * 退出条件: 转过MIN_YAW后在当前周期重新扫到S3~S6 */
        if (g_lineTrack.acuteState == LT_ACUTE_TURNING)
        {
            g_lineTrack.dbgTrackState = 2;
            uint32_t elapsed = tickMs - g_lineTrack.acuteStartTick;
            float deltaYaw = currentYaw - g_lineTrack.acuteStartYaw;
            if (deltaYaw > 180.0f)  deltaYaw -= 360.0f;
            if (deltaYaw < -180.0f) deltaYaw += 360.0f;
            float absYaw = (deltaYaw < 0.0f) ? -deltaYaw : deltaYaw;

            /* 退出: 转够角度 + 当前周期S3~S6重新见线 → 回归PD循迹
             * 复用 signal_handler() 已采到的 sensorBits，避免二次采样抖动。 */
            if (absYaw >= TRACK_ACUTE_MIN_YAW)
            {
                if (g_lineTrack.sensorBits & LT_MASK_ACUTE_ZONE)
                {
                    acute_enter_recover(tickMs);
                    acute_drive_recovery(basePwm);
                    break;
                }
            }
            /* 超时保护 */
            if (elapsed >= TRACK_ACUTE_TIMEOUT_MS)
            {
                acute_enter_recover(tickMs);
                acute_drive_recovery(basePwm);
                break;
            }
            /* 差速前进: 外轮快, 内轮慢, 都正转(不自转) */
            if (g_lineTrack.acuteSide == LT_DIR_LEFT)
                track_motor_forward(TRACK_ACUTE_INNER_PWM, TRACK_ACUTE_OUTER_PWM);
            else
                track_motor_forward(TRACK_ACUTE_OUTER_PWM, TRACK_ACUTE_INNER_PWM);
            break;
        }
        if (g_lineTrack.acuteState == LT_ACUTE_RECOVER)
        {
            g_lineTrack.dbgTrackState = 3;
            uint32_t elapsed = tickMs - g_lineTrack.acuteRecoverStartTick;
            uint8_t exitReady = acute_recover_line_ready(g_lineTrack.sensorBits,
                                                         g_lineTrack.weightedPos);

            if (exitReady)
            {
                if (g_lineTrack.acuteRecoverStableCount < 255u)
                    g_lineTrack.acuteRecoverStableCount++;
            }
            else
            {
                g_lineTrack.acuteRecoverStableCount = 0;
            }

            if (elapsed >= TRACK_ACUTE_RECOVER_HOLD_MS
                && exitReady
                && g_lineTrack.acuteRecoverStableCount >= TRACK_ACUTE_RECOVER_STABLE_COUNT)
            {
                g_lineTrack.acuteState = LT_ACUTE_IDLE;
                g_lineTrack.lastPos = g_lineTrack.weightedPos;
                g_lineTrack.filteredDPos = 0.0f;
                compute_and_drive(basePwm);
                break;
            }

            if (elapsed >= TRACK_ACUTE_RECOVER_TIMEOUT_MS)
            {
                acute_enter_corner_search(tickMs, currentYaw);
                break;
            }

            acute_drive_recovery(basePwm);
            break;
        }
        /* ===== 非阻塞原地旋转找线 (备用, 用于非锐角弯道) =====
         * 至少转MIN_YAW才开始找线 —— 跳过旧线
         * 超过MAX_YAW则停止 —— 防止转到背后 */
        if (g_lineTrack.cornerTurning)
        {
            uint32_t elapsed = tickMs - g_lineTrack.cornerStartTick;
            float deltaYaw = currentYaw - g_lineTrack.cornerStartYaw;
            uint8_t bits = read_sensor_bits();
            uint8_t count = bit_count(bits);
            uint8_t acceptMask;
            uint8_t yawReady;
            float minYaw = g_lineTrack.rightAngleAssist ? TRACK_RIGHT_ANGLE_MIN_YAW : TRACK_CORNER_MIN_YAW;
            uint32_t timeoutMs = g_lineTrack.rightAngleAssist ? TRACK_RIGHT_ANGLE_TIMEOUT_MS : TRACK_CORNER_TIMEOUT_MS;
            int16_t fastPwm = g_lineTrack.rightAngleAssist ? TRACK_RIGHT_ANGLE_TURN_PWM_FAST : TRACK_TURN_PWM_FAST;
            int16_t slowPwm = g_lineTrack.rightAngleAssist ? TRACK_RIGHT_ANGLE_TURN_PWM_SLOW : TRACK_TURN_PWM_SLOW;
            /* 角度绕圈处理 */
            if (deltaYaw > 180.0f)  deltaYaw -= 360.0f;
            if (deltaYaw < -180.0f) deltaYaw += 360.0f;
            if (deltaYaw < 0.0f)    deltaYaw = -deltaYaw;

            yawReady = (deltaYaw >= minYaw) ? 1u : 0u;
            if (g_lineTrack.cornerDir == LT_DIR_RIGHT)
                acceptMask = 0xF8;  /* S4-S8: 右半+中间, 忽略S1S2S3(左侧旧线) */
            else
                acceptMask = 0x1F;  /* S1-S5: 左半+中间, 忽略S6S7S8(右侧旧线) */

            if ((g_lineTrack.rightAngleAssist
                 && deltaYaw >= TRACK_RIGHT_ANGLE_ACCEPT_LATCH_YAW
                 && (bits & acceptMask))
                || (!g_lineTrack.rightAngleAssist
                    && deltaYaw >= TRACK_CORNER_ACCEPT_LATCH_YAW
                    && (bits & acceptMask)))
            {
                g_lineTrack.rightAngleAcceptSeen = 1;
            }

            g_lineTrack.dbgTrackState = g_lineTrack.rightAngleAssist ? 5 : 4;
            g_lineTrack.dbgCornerDir = g_lineTrack.cornerDir;
            g_lineTrack.dbgCornerYawDelta = deltaYaw;
            g_lineTrack.dbgCornerBits = bits;
            g_lineTrack.dbgCornerAcceptMask = acceptMask;
            g_lineTrack.dbgCornerYawReady = yawReady;
            g_lineTrack.dbgCornerAcceptHit = ((bits & acceptMask) != 0u) ? 1u : 0u;

            /* 直角专用找线如果在早期就出现左右同时分支+中心主线，
               说明这更像交叉区，立即切换到 CRS 直行绕过态。 */
            if (g_lineTrack.rightAngleAssist
                && deltaYaw < TRACK_RIGHT_ANGLE_MIN_YAW
                && crossing_keep_straight_match(bits, count))
            {
                enter_cross_bypass(tickMs, bits);
                g_lineTrack.dbgTrackState = 6;
                compute_and_drive(basePwm);
                break;
            }

            /* 达到最小转角后开始检测传感器
             * 方向感知mask: 右转只检右半+中间(忽略左侧旧线),
             *              左转只检左半+中间(忽略右侧旧线)
             * S1S2=最左(0x03), S3(0x04), S4(0x08), S5(0x10), S6(0x20), S7S8=最右(0xC0)
             */
            if (yawReady)
            {
                uint8_t currentFrameOk = corner_current_frame_consistent(bits,
                                                                        g_lineTrack.cornerDir,
                                                                        g_lineTrack.rightAngleAssist);
                if ((bits & acceptMask)
                    || (g_lineTrack.rightAngleAcceptSeen && currentFrameOk))
                {
                    /* 找到线! 先刹车消除旋转动量, 再恢复PD循迹 */
                    track_motor_stop();
                    g_lineTrack.cornerTurning = 0;
                    g_lineTrack.rightAngleAssist = 0;
                    g_lineTrack.rightAngleAcceptSeen = 0;
                    g_lineTrack.overrunCount = 0;
                    g_lineTrack.lastCornerBits = LT_MASK_MID;
                    g_lineTrack.cornerDone = 1;
                    break;
                }
            }
            /* 超过最大转角或超时: 不停车, 退出旋转回PD纠偏继续尝试
             * 真正停车由 OVERRUN_LIMIT 控制 */
            if (deltaYaw >= TRACK_CORNER_MAX_YAW || elapsed > timeoutMs)
            {
                g_lineTrack.cornerTurning = 0;
                g_lineTrack.rightAngleAssist = 0;
                g_lineTrack.rightAngleAcceptSeen = 0;
                g_lineTrack.overrunCount = TRACK_CORNER_CONFIRM; /* 重新开始累计 */
                g_lineTrack.cornerDone = 1;
                compute_and_drive(basePwm);
                break;
            }
            /* 继续旋转 */
            if (g_lineTrack.cornerDir == LT_DIR_RIGHT)
                track_turn_right(fastPwm, slowPwm);
            else
                track_turn_left(slowPwm, fastPwm);
        }
        else
        {
            if (g_lineTrack.acuteState == LT_ACUTE_WINDOW)
                g_lineTrack.dbgTrackState = 1;
            compute_and_drive(basePwm);
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

void LineTrack_SetPID(float kp, float kd)
{
    g_lineTrack.kp = kp;
    g_lineTrack.kd = kd;
}
