/**
 * @file  line_track.c
 * @brief 8-channel line tracking state machine
 *        Ported from 5-sensor (至善电子 V2.3.0_B), adapted for 8 sensors.
 *
 *  5-sensor layout:  L1  L0  M   R0  R1      (1 middle)
 *  8-sensor layout:  S1  S2  S3  S4  S5  S6  S7  S8  (2 middle: S4,S5)
 *
 *  Key adaptation: "center" = S4 and/or S5 active.
 *  Turning logic (corner_handler) is preserved from 5-sensor reference.
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

/* ========== Corner Handler (blocking, from 5-sensor) ========== */
/*
 * Stops car, pivots until sensor detects line, then stops.
 * This is a BLOCKING function — will hold the main loop.
 *
 * 防过转策略:
 *   1. 盲区时间(BLIND_MS): 前300ms不检测传感器, 跳过刚离开的线
 *   2. 超时保护(TIMEOUT_MS=1500): 限制最大旋转~150°, 防止转到背后的线
 */
static void corner_handler(uint8_t dir, uint16_t fastPwm, uint16_t slowPwm,
                            volatile uint32_t *pTickMs)
{
    uint32_t startTick, elapsed;
    uint8_t bits;

    track_motor_stop();
    Delay_ms(5);

    startTick = *pTickMs;

    if (dir == LT_DIR_RIGHT)
        track_turn_right(fastPwm, slowPwm);
    else
        track_turn_left(slowPwm, fastPwm);

    while (1)
    {
        elapsed = *pTickMs - startTick;

        /* 盲区结束后才开始检测传感器 */
        if (elapsed >= TRACK_CORNER_BLIND_MS)
        {
            bits = read_sensor_bits();
            if (bits & LT_MASK_ALL)
            {
                track_motor_stop();
                Delay_ms(5);
                break;
            }
        }

        /* 超时保护: 1500ms ≈ 最大旋转150°, 防止转过头 */
        if (elapsed > TRACK_CORNER_TIMEOUT_MS)
        {
            track_motor_stop();
            g_lineTrack.autoFlag = LT_FLAG_STOP;
            break;
        }
    }
}

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

    /* If any sensor sees line, reset overrun */
    if (bits != 0x00)
        g_lineTrack.overrunCount = 0;

    /* Track outermost sensor activity for corner direction */
    if (bits & (LT_MASK_FAR_LEFT | LT_MASK_FAR_RIGHT))
        g_lineTrack.lastCornerBits = bits;

    /* ===== Case 1: 全灭 — 弯道/脱轨 =====
     * 策略:
     *   短暂丢线(<CORNER_CONFIRM): 冻结位置, PD惯性纠偏
     *   持续丢线(>=CORNER_CONFIRM): 进入非阻塞原地旋转找线
     *   超时(OVERRUN_LIMIT): 安全停车
     */
    if (bits == 0x00)
    {
        g_lineTrack.overrunCount++;

        if (g_lineTrack.overrunCount < TRACK_CORNER_CONFIRM)
        {
            /* 短暂丢线: 冻结当前位置, PD保持惯性转向 */
            rawPos = g_lineTrack.weightedPos;
        }
        else if (!g_lineTrack.cornerTurning)
        {
            /* 进入非阻塞原地旋转模式 */
            g_lineTrack.cornerTurning = 1;
            g_lineTrack.cornerStartTick = *pTickMs;
            if (g_lineTrack.lastCornerBits & LT_MASK_FAR_LEFT)
                g_lineTrack.cornerDir = LT_DIR_LEFT;
            else
                g_lineTrack.cornerDir = LT_DIR_RIGHT;
            rawPos = g_lineTrack.weightedPos;
        }
        else if (g_lineTrack.overrunCount >= TRACK_OVERRUN_LIMIT)
        {
            /* 超时停车 */
            g_lineTrack.cornerTurning = 0;
            g_lineTrack.overrunCount = 0;
            g_lineTrack.autoFlag = LT_FLAG_STOP;
            rawPos = 0;
        }
        else
        {
            rawPos = g_lineTrack.weightedPos;
        }
    }
    /* ===== Case 2: 多灯亮 — 交叉口检测 ===== */
    else if (count >= TRACK_CROSS_MIN_COUNT)
    {
        switch (g_lineTrack.crossState)
        {
        case 1:
            g_lineTrack.crossCount++;
            g_lineTrack.crossState = 2;
            g_lineTrack.filterTimes = 0;
            break;
        case 2:
            if (g_lineTrack.filterTimes >= TRACK_CROSS_FILTER)
            {
                g_lineTrack.filterTimes = 0;
                g_lineTrack.crossCount++;
                if (g_lineTrack.crossCount >= g_lineTrack.crossing)
                    g_lineTrack.autoFlag = LT_FLAG_STOP;
            }
            break;
        default:
            break;
        }
        g_lineTrack.bearingDev = 0;
        rawPos = 0;  /* 目标：渐变趋向直行过路口 */
    }
    /* ===== Case 3: 正常巡线 ===== */
    else
    {
        rawPos = weighted_position(bits);
        g_lineTrack.bearingDev = position_to_bearing(rawPos);
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
}

void LineTrack_Stop(void)
{
    g_lineTrack.state = LT_STATE_IDLE;
    g_lineTrack.autoFlag = LT_FLAG_STOP;
    track_motor_stop();
}

void LineTrack_Update(uint32_t tickMs, int16_t basePwm)
{
    volatile uint32_t tick = tickMs;  /* for corner_handler timeout */

    switch (g_lineTrack.state)
    {
    case LT_STATE_STARTING:
        g_lineTrack.autoFlag = LT_FLAG_START;
        g_lineTrack.state = LT_STATE_RUNNING;
        MotorDriver_Enable();
        break;

    case LT_STATE_RUNNING:
        signal_handler(&tick);
        if (g_lineTrack.autoFlag == LT_FLAG_STOP)
        {
            g_lineTrack.cornerTurning = 0;
            track_motor_stop();
            g_lineTrack.state = LT_STATE_IDLE;
            break;
        }
        /* 非阻塞原地旋转找线: 方向感知检测
         * 左转时只接受左侧传感器(S1-S4), 右转只接受右侧(S5-S8)
         * 这样不会误检测到转到背后的旧线 */
        if (g_lineTrack.cornerTurning)
        {
            uint32_t elapsed = tickMs - g_lineTrack.cornerStartTick;
            /* 短盲区(50ms)跳过起始位置 */
            if (elapsed >= 50u)
            {
                uint8_t bits = read_sensor_bits();
                /* 方向感知: 排除对面远端传感器(那是旧线)
                 * 左转: 排除S7,S8(far-right), 接受S1-S6
                 * 右转: 排除S1,S2(far-left),  接受S3-S8 */
                uint8_t acceptMask = (g_lineTrack.cornerDir == LT_DIR_LEFT)
                                   ? 0x3Fu   /* S1-S6: 排除far-right */
                                   : 0xFCu;  /* S3-S8: 排除far-left  */
                if (bits & acceptMask)
                {
                    /* 正确方向找到线! 退出旋转, 恢复PD循迹 */
                    g_lineTrack.cornerTurning = 0;
                    g_lineTrack.overrunCount = 0;
                    g_lineTrack.lastCornerBits = LT_MASK_MID;
                    g_lineTrack.cornerDone = 1;
                    compute_and_drive(basePwm);
                    break;
                }
            }
            /* 超时保护 */
            if (elapsed > TRACK_CORNER_TIMEOUT_MS)
            {
                g_lineTrack.cornerTurning = 0;
                g_lineTrack.autoFlag = LT_FLAG_STOP;
                track_motor_stop();
                g_lineTrack.state = LT_STATE_IDLE;
                break;
            }
            /* 继续旋转 */
            if (g_lineTrack.cornerDir == LT_DIR_RIGHT)
                track_turn_right(TRACK_TURN_PWM_FAST, TRACK_TURN_PWM_SLOW);
            else
                track_turn_left(TRACK_TURN_PWM_SLOW, TRACK_TURN_PWM_FAST);
        }
        else
        {
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
