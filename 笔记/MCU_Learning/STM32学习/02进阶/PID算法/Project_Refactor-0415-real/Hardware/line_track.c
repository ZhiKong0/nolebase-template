/**
 * @file  line_track.c
 * @brief 8 路循迹核心
 *
 * 当前实现把“正常循迹”和“转角判定”拆开处理：
 *
 *   1. 正常循迹改成“图案解码 -> PD -> 建议混控”：
 *      先把 8 路数字图案解码成单一误差，再做轻度低通和 PD，
 *      不再叠加一串“软化/换侧记忆/最小差速/恢复增益”补丁。
 *   2. 转角/全灭恢复仍沿用五路参考逻辑：
 *      把 8 路映射成 L1/L0/M/R0/R1，再按 last outer hit 决定转向。
 *   3. 误差解码直接针对 8 路数字量特性：
 *      先补掉单个丢失位，再按黑线跨度中点求误差，
 *      让宽图案、粗线和单中心图案都落到同一套几何解释上。
 *   4. 正常循迹输出仍保留连续降速和差速限幅，但不再依赖多层修补状态。
 */

#include "line_track.h"
#include "sensor_fusion.h"
#include "motor_driver.h"
#include "Delay.h"

#include <string.h>

LineTrack_State_t g_lineTrack;

/* 8 路图案中心位置:
   通过“活动黑线跨度的中点”来估算当前横向偏差。 */
static const int16_t s_trackCenterPos[8] = { -280, -200, -120, -40, 40, 120, 200, 280 };

static float   s_trackFilteredError = 0.0f;
static int16_t s_trackLastFilteredError = 0;

static int8_t track_first_active_index(uint8_t bits)
{
    int8_t i;

    for (i = 0; i < 8; i++)
    {
        if (bits & (1u << i))
            return i;
    }
    return -1;
}

static int8_t track_last_active_index(uint8_t bits)
{
    int8_t i;

    for (i = 7; i >= 0; i--)
    {
        if (bits & (1u << i))
            return i;
    }
    return -1;
}

static uint8_t track_fill_single_hole(uint8_t bits)
{
    uint8_t i;

    for (i = 1u; i < 7u; i++)
    {
        if (((bits & (1u << i)) == 0u) &&
            (bits & (1u << (i - 1u))) &&
            (bits & (1u << (i + 1u))))
        {
            bits |= (1u << i);
        }
    }
    return bits;
}

static int16_t decode_track_error(uint8_t bits)
{
    int8_t leftIndex;
    int8_t rightIndex;
    uint8_t cleanedBits;

    if (bits == 0u)
        return 0;

    cleanedBits = track_fill_single_hole(bits);
    leftIndex = track_first_active_index(cleanedBits);
    rightIndex = track_last_active_index(cleanedBits);

    if ((leftIndex < 0) || (rightIndex < 0))
        return 0;

    return (int16_t)((s_trackCenterPos[leftIndex] + s_trackCenterPos[rightIndex]) / 2);
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
    s_trackFilteredError = 0.0f;
    s_trackLastFilteredError = 0;
}

static void read_equivalent_data(void)
{
    uint8_t rawBits;

    rawBits = read_raw_bits();
    g_lineTrack.sensorBits = rawBits;
    g_lineTrack.sensorData5 = map_to_eq5(rawBits);
    g_lineTrack.weightedPos = decode_track_error(rawBits);
    g_lineTrack.dbgCornerBits = rawBits;
}

static int16_t position_pd_output(int16_t positionError)
{
    int16_t delta;
    int16_t filteredError;
    uint8_t rawBits;
    float pTerm;
    float dTerm;
    float pwmf;

    rawBits = g_lineTrack.sensorBits;

    /* 双中心或全灭时直接卸掉正常循迹残留，转角逻辑仍由五路部分接管。 */
    if ((rawBits == LT_MASK_MID) || (rawBits == 0u))
    {
        s_trackFilteredError = 0.0f;
        s_trackLastFilteredError = 0;
        return 0;
    }

    s_trackFilteredError += TRACK_ERROR_LPF_ALPHA *
                            ((float)positionError - s_trackFilteredError);
    filteredError = (int16_t)s_trackFilteredError;

    /* D 看低通后误差的变化，减少宽图案和中心切换带来的离散尖峰。 */
    delta = (int16_t)(filteredError - s_trackLastFilteredError);
    if (delta > TRACK_ERROR_DELTA_LIMIT)
        delta = TRACK_ERROR_DELTA_LIMIT;
    else if (delta < -TRACK_ERROR_DELTA_LIMIT)
        delta = (int16_t)(-TRACK_ERROR_DELTA_LIMIT);

    pTerm = g_lineTrack.kp * (float)filteredError;
    dTerm = g_lineTrack.kd * (float)delta;
    pwmf = pTerm + dTerm;

    /* D 项只能帮忙加减，不能把当前图案方向直接翻转。 */
    if ((positionError > 0 && pwmf < 0.0f) ||
        (positionError < 0 && pwmf > 0.0f))
    {
        pwmf = TRACK_D_REVERSE_GUARD_RATIO * g_lineTrack.kp * (float)positionError;
    }

    if (pwmf > TRACK_DIFF_PWM_MAX)
        pwmf = (float)TRACK_DIFF_PWM_MAX;
    else if (pwmf < -(float)TRACK_DIFF_PWM_MAX)
        pwmf = -(float)TRACK_DIFF_PWM_MAX;

    s_trackLastFilteredError = filteredError;
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

static void Track_Handler(void)
{
    g_lineTrack.devSpeed = position_pd_output(g_lineTrack.weightedPos);
    Compute_PWM();
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
