/**
 * @file  line_track.c
 * @brief 8 路循迹核心
 *
 * 当前实现把“正常循迹”和“转角判定”拆开处理：
 *
 *   1. 正常循迹直接使用 8 路原始传感器的加权位置做控制输入。
 *      S4/S5 是双中心传感器，只要线落在中心对上，就按 0 偏差处理。
 *   2. 转角/全灭恢复仍沿用五路参考逻辑：
 *      把 8 路映射成 L1/L0/M/R0/R1，再按 last outer hit 决定转向。
 *   3. 位置输入先低通，再做 D 项限幅 + 低通，减小 8 路对射抖动。
 *   4. 正常循迹输出增加左右差速上限，并在大偏差时自动降速。
 *   5. 回到中心或瞬时无信号时，立即卸掉残留转向，避免左右来回扫。
 *   6. 正常循迹输出不再把低于阈值的正向 PWM 直接切成 0。
 */

#include "line_track.h"
#include "sensor_fusion.h"
#include "motor_driver.h"
#include "Delay.h"

#include <string.h>

LineTrack_State_t g_lineTrack;

/* S4/S5 为双中心，对直线区不主动施加左右偏置；
   邻近 S3/S6 的权重也略收窄，减少数字量输入下的中心摆动。 */
static const int16_t s_weights[8] = { -260, -180, -90, 0, 0, 90, 180, 260 };

static int16_t s_trackLastPosError = 0;
static float   s_trackFilteredPos = 0.0f;
static float   s_trackFilteredDelta = 0.0f;

static int16_t weighted_position(uint8_t bits)
{
    int32_t sum = 0;
    uint8_t count = 0u;
    uint8_t i;

    if ((bits & (uint8_t)(~LT_MASK_MID)) == 0u)
        return 0;

    for (i = 0u; i < 8u; i++)
    {
        if (bits & (1u << i))
        {
            sum += s_weights[i];
            count++;
        }
    }

    if (count == 0u)
        return 0;

    return (int16_t)(sum / (int32_t)count);
}

static uint8_t read_raw_bits(void)
{
    LineSensor_Data_t data;
    LineSensor_Read(&data);
    return data.bits;
}

static uint8_t map_to_eq5(uint8_t rawBits)
{
    uint8_t eqBits = 0u;

    if (rawBits & LT_MASK_MID)
        eqBits |= LT_EQ_M;
    if (rawBits & LT_MASK_LEFT)
        eqBits |= LT_EQ_L0;
    if (rawBits & LT_MASK_FAR_LEFT)
        eqBits |= LT_EQ_L1;
    if (rawBits & LT_MASK_RIGHT)
        eqBits |= LT_EQ_R0;
    if (rawBits & LT_MASK_FAR_RIGHT)
        eqBits |= LT_EQ_R1;

    return eqBits;
}

static void track_motor_stop(void)
{
    MotorDriver_Stop();
}

static void track_motor_forward(int16_t left, int16_t right)
{
    if (left < 0)
        left = 0;
    if (right < 0)
        right = 0;

    MotorDriver_Enable();
    MotorDriver_SetTurnPWM(left, right);
}

static void track_turn_left(int16_t leftPwm, int16_t rightPwm)
{
    MotorDriver_Enable();
    MotorDriver_SetTurnPWM(-leftPwm, rightPwm);
}

static void track_turn_right(int16_t leftPwm, int16_t rightPwm)
{
    MotorDriver_Enable();
    MotorDriver_SetTurnPWM(leftPwm, -rightPwm);
}

static void reset_track_pid_runtime(void)
{
    s_trackLastPosError = 0;
    s_trackFilteredPos = 0.0f;
    s_trackFilteredDelta = 0.0f;
}

static void read_equivalent_data(void)
{
    uint8_t rawBits;

    rawBits = read_raw_bits();
    g_lineTrack.sensorBits = rawBits;
    g_lineTrack.sensorData5 = map_to_eq5(rawBits);
    g_lineTrack.weightedPos = weighted_position(rawBits);
    g_lineTrack.dbgCornerBits = rawBits;
}

static int16_t position_pd_output(int16_t positionError)
{
    int16_t filteredError;
    int16_t delta;
    int16_t steerRef;
    float pTerm;
    float dTerm;
    float pwmf;

    s_trackFilteredPos += TRACK_POS_INPUT_LPF_ALPHA * ((float)positionError - s_trackFilteredPos);
    filteredError = (int16_t)s_trackFilteredPos;

    /* Once the line is back on the center pair, or no sensor is active in
       the non-corner path, clear the residual steer immediately. */
    if (positionError == 0)
    {
        s_trackFilteredPos = 0.0f;
        s_trackFilteredDelta = 0.0f;
        s_trackLastPosError = 0;
        return 0;
    }

    /* Eight digital sensors jump side-to-side in discrete steps. When the
       raw position crosses to the other side, do not let the LPF lag keep
       steering in the old direction for another few cycles. */
    if ((positionError > 0 && filteredError < 0) ||
        (positionError < 0 && filteredError > 0))
    {
        s_trackFilteredPos = (float)positionError;
        filteredError = positionError;
        s_trackFilteredDelta = 0.0f;
        s_trackLastPosError = filteredError;
    }

    delta = (int16_t)(filteredError - s_trackLastPosError);
    if (delta > TRACK_POS_DELTA_LIMIT)
        delta = TRACK_POS_DELTA_LIMIT;
    else if (delta < -TRACK_POS_DELTA_LIMIT)
        delta = (int16_t)(-TRACK_POS_DELTA_LIMIT);

    s_trackFilteredDelta += TRACK_POS_D_LPF_ALPHA * ((float)delta - s_trackFilteredDelta);

    pTerm = g_lineTrack.kp * (float)filteredError;
    dTerm = g_lineTrack.kd * s_trackFilteredDelta;
    pwmf = pTerm + dTerm;
    steerRef = (positionError != 0) ? positionError : filteredError;

    /* Pure digital 8-sensor input is still coarse; let D damp P, but never
       flip the steering direction away from the current line side. */
    if ((steerRef > 0 && pwmf < 0.0f) ||
        (steerRef < 0 && pwmf > 0.0f))
    {
        pwmf = TRACK_D_REVERSE_GUARD_RATIO * g_lineTrack.kp * (float)steerRef;
    }

    if (pwmf > TRACK_DIFF_PWM_MAX)
        pwmf = (float)TRACK_DIFF_PWM_MAX;
    else if (pwmf < -(float)TRACK_DIFF_PWM_MAX)
        pwmf = -(float)TRACK_DIFF_PWM_MAX;

    s_trackLastPosError = filteredError;
    return (int16_t)pwmf;
}

static void Compute_PWM(void)
{
    int16_t absDiff;
    int16_t effectiveBase;
    int32_t reduce;

    absDiff = (g_lineTrack.devSpeed >= 0) ? g_lineTrack.devSpeed : (int16_t)(-g_lineTrack.devSpeed);
    reduce = ((int32_t)absDiff * (int32_t)TRACK_BASE_PWM_REDUCE_MAX) / (int32_t)TRACK_DIFF_PWM_MAX;
    effectiveBase = (int16_t)(g_lineTrack.basePwm - (int16_t)reduce);
    if (effectiveBase < TRACK_BASE_PWM_MIN)
        effectiveBase = TRACK_BASE_PWM_MIN;
    else if (effectiveBase > TRACK_BASE_PWM_MAX)
        effectiveBase = TRACK_BASE_PWM_MAX;

    g_lineTrack.motorLPwm = (int16_t)(effectiveBase + g_lineTrack.devSpeed);
    g_lineTrack.motorRPwm = (int16_t)(effectiveBase - g_lineTrack.devSpeed);

    if (g_lineTrack.motorLPwm > TRACK_PWM_MAX)
        g_lineTrack.motorLPwm = TRACK_PWM_MAX;
    else if (g_lineTrack.motorLPwm <= 0)
        g_lineTrack.motorLPwm = 0;
    else if (g_lineTrack.motorLPwm < TRACK_PWM_MIN)
        g_lineTrack.motorLPwm = TRACK_PWM_MIN;

    if (g_lineTrack.motorRPwm > TRACK_PWM_MAX)
        g_lineTrack.motorRPwm = TRACK_PWM_MAX;
    else if (g_lineTrack.motorRPwm <= 0)
        g_lineTrack.motorRPwm = 0;
    else if (g_lineTrack.motorRPwm < TRACK_PWM_MIN)
        g_lineTrack.motorRPwm = TRACK_PWM_MIN;
}

static void Track_position_control(void)
{
    track_motor_forward(g_lineTrack.motorLPwm, g_lineTrack.motorRPwm);
}

static void Track_Handler(void)
{
    g_lineTrack.devSpeed = position_pd_output(g_lineTrack.weightedPos);
    Compute_PWM();
    Track_position_control();
}

static void corner_handler(uint8_t dir_, uint16_t leftPwm, uint16_t rightPwm)
{
    g_lineTrack.cornerTurning = 1u;
    g_lineTrack.cornerDir = dir_;
    g_lineTrack.dbgTrackState = 4u;
    g_lineTrack.dbgCornerDir = dir_;
    g_lineTrack.dbgCornerYawReady = 1u;

    track_motor_stop();
    Delay_ms(TRACK_CORNER_STOP_DELAY_MS);

    if (dir_ == LT_DIR_RIGHT)
        track_turn_right((int16_t)leftPwm, (int16_t)rightPwm);
    else if (dir_ == LT_DIR_LEFT)
        track_turn_left((int16_t)leftPwm, (int16_t)rightPwm);

    while (1)
    {
        read_equivalent_data();
        if (g_lineTrack.sensorData5 & LT_EQ_ACTIVE_MASK)
        {
            track_motor_stop();
            Delay_ms(TRACK_CORNER_STOP_DELAY_MS);
            break;
        }
    }

    g_lineTrack.cornerTurning = 0u;
    g_lineTrack.cornerDir = 0u;
    g_lineTrack.dbgTrackState = 0u;
    g_lineTrack.dbgCornerDir = 0u;
    g_lineTrack.dbgCornerYawReady = 0u;
    g_lineTrack.cornerDone = 1u;
}

static void Signal_Handler(void)
{
    g_lineTrack.filterTimes++;
    if (g_lineTrack.filterTimes >= TRACK_CROSS_FILTER)
        g_lineTrack.filterTimes = TRACK_CROSS_FILTER;

    read_equivalent_data();

    if (g_lineTrack.sensorData5 != 0x00u)
        g_lineTrack.overrunCount = 0u;

    if (g_lineTrack.sensorData5 & 0x22u)
        g_lineTrack.lastData = g_lineTrack.sensorData5;

    switch (g_lineTrack.sensorData5)
    {
    case 0x00u:
        if (g_lineTrack.lastData & 0x20u)
        {
            corner_handler(LT_DIR_LEFT,
                           TRACK_TURN_LEFT_L_PWM,
                           TRACK_TURN_LEFT_R_PWM);
        }
        else if (g_lineTrack.lastData & 0x02u)
        {
            corner_handler(LT_DIR_RIGHT,
                           TRACK_TURN_RIGHT_L_PWM,
                           TRACK_TURN_RIGHT_R_PWM);
        }
        else
        {
            g_lineTrack.overrunCount++;
            if (g_lineTrack.overrunCount >= TRACK_OVERRUN_LIMIT)
            {
                g_lineTrack.overrunCount = 0u;
                g_lineTrack.autoFlag = LT_FLAG_STOP;
            }
        }
        break;

    case 0x08u:
        g_lineTrack.bearingDev = 0;
        break;

    case 0x18u:
        g_lineTrack.bearingDev = -1;
        break;

    case 0x0Cu:
        g_lineTrack.bearingDev = 1;
        break;

    case 0x10u:
        g_lineTrack.bearingDev = -2;
        break;

    case 0x04u:
        g_lineTrack.bearingDev = 2;
        break;

    case 0x30u:
        g_lineTrack.bearingDev = -4;
        break;

    case 0x06u:
        g_lineTrack.bearingDev = 4;
        break;

    case 0x20u:
        g_lineTrack.bearingDev = -7;
        break;

    case 0x02u:
        g_lineTrack.bearingDev = 7;
        break;

    case 0x3Eu:
        switch (g_lineTrack.crossState)
        {
        case 1u:
            g_lineTrack.crossCount++;
            g_lineTrack.crossState = 2u;
            g_lineTrack.filterTimes = 0u;
            break;

        case 2u:
            if (g_lineTrack.filterTimes >= TRACK_CROSS_FILTER)
            {
                g_lineTrack.filterTimes = 0u;
                g_lineTrack.crossCount++;
                if (g_lineTrack.crossCount >= g_lineTrack.crossing)
                    g_lineTrack.autoFlag = LT_FLAG_STOP;
            }
            break;

        default:
            break;
        }
        break;

    default:
        break;
    }
}

static void restore_track_state(void)
{
    float kp = g_lineTrack.kp;
    float kd = g_lineTrack.kd;
    int16_t basePwm = g_lineTrack.basePwm;

    memset(&g_lineTrack, 0, sizeof(g_lineTrack));

    if (kp <= 0.0f)
        kp = PID_TRACK_LINE_KP;
    if (kd < 0.0f)
        kd = PID_TRACK_LINE_KD;
    if (basePwm <= 0)
        basePwm = TRACK_BASE_PWM_DEFAULT;

    g_lineTrack.kp = kp;
    g_lineTrack.kd = kd;
    g_lineTrack.basePwm = basePwm;
    g_lineTrack.sensorData5 = LT_EQ_M;
    g_lineTrack.lastData = LT_EQ_M;
    g_lineTrack.crossing = TRACK_DEFAULT_CROSSINGS;
    g_lineTrack.crossState = 1u;
    reset_track_pid_runtime();
}

void LineTrack_Init(void)
{
    memset(&g_lineTrack, 0, sizeof(g_lineTrack));
    g_lineTrack.kp = PID_TRACK_LINE_KP;
    g_lineTrack.kd = PID_TRACK_LINE_KD;
    g_lineTrack.basePwm = TRACK_BASE_PWM_DEFAULT;
    LineTrack_Stop();
}

void LineTrack_Start(uint8_t crossings)
{
    restore_track_state();
    g_lineTrack.state = LT_STATE_STARTING;
    g_lineTrack.autoFlag = LT_FLAG_STOP;
    g_lineTrack.crossing = crossings ? crossings : TRACK_DEFAULT_CROSSINGS;
}

void LineTrack_Stop(void)
{
    g_lineTrack.state = LT_STATE_IDLE;
    g_lineTrack.autoFlag = LT_FLAG_STOP;
    g_lineTrack.cornerTurning = 0u;
    g_lineTrack.cornerDir = 0u;
    g_lineTrack.cornerDone = 0u;
    g_lineTrack.dbgTrackState = 0u;
    g_lineTrack.dbgCornerDir = 0u;
    g_lineTrack.dbgCornerYawDelta = 0.0f;
    g_lineTrack.dbgCornerBits = 0u;
    g_lineTrack.dbgCornerAcceptMask = 0u;
    g_lineTrack.dbgCornerYawReady = 0u;
    g_lineTrack.dbgCornerAcceptHit = 0u;
    track_motor_stop();
}

void LineTrack_Update(uint32_t tickMs, int16_t basePwm)
{
    (void)tickMs;
    (void)basePwm;

    switch (g_lineTrack.state)
    {
    case LT_STATE_STARTING:
        g_lineTrack.autoFlag = LT_FLAG_START;
        g_lineTrack.state = LT_STATE_RUNNING;
        break;

    case LT_STATE_RUNNING:
        g_lineTrack.cornerDone = 0u;
        g_lineTrack.dbgTrackState = 0u;
        g_lineTrack.dbgCornerDir = 0u;
        g_lineTrack.dbgCornerYawDelta = 0.0f;
        g_lineTrack.dbgCornerAcceptMask = 0u;
        g_lineTrack.dbgCornerYawReady = 0u;
        g_lineTrack.dbgCornerAcceptHit = 0u;

        Signal_Handler();
        if (g_lineTrack.autoFlag == LT_FLAG_STOP)
        {
            LineTrack_Stop();
            break;
        }

        Track_Handler();
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
    if (kp > 0.0f)
        g_lineTrack.kp = kp;
    if (kd >= 0.0f)
        g_lineTrack.kd = kd;
}

void LineTrack_SetBasePwm(int16_t basePwm)
{
    if (basePwm < TRACK_BASE_PWM_MIN)
        basePwm = TRACK_BASE_PWM_MIN;
    if (basePwm > TRACK_BASE_PWM_MAX)
        basePwm = TRACK_BASE_PWM_MAX;
    g_lineTrack.basePwm = basePwm;
}

int16_t LineTrack_GetBasePwm(void)
{
    return g_lineTrack.basePwm;
}
