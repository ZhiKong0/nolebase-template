#include "Tracker.h"
#include "stm32f10x.h"
#include "Delay.h"
#include "Motor.h"



Tracker_Typedef tracker;


#define TRACK_BASE_SPEED          230
#define TRACK_CORNER_SPEED        180
#define TRACK_MIN_SPEED            90
#define TRACK_MAX_SPEED           420

#define TRACK_KP_NUM               24
#define TRACK_KD_NUM               52
#define TRACK_GAIN_DEN            100
#define TRACK_TURN_LIMIT          240

#define SEARCH_TURN_OUTER_SPEED   260
#define SEARCH_TURN_INNER_SPEED    20
#define SEARCH_FORWARD_SPEED      140

#define ACUTE_OUTER_SPEED         360
#define ACUTE_INNER_SPEED        -150
#define ACUTE_CONFIRM_TICKS         2
#define ACUTE_MIN_HOLD_TICKS        7   // 至少锁存 70ms
#define ACUTE_MAX_HOLD_TICKS       18   // 最多锁存 180ms

#define LOST_SEARCH_TICKS           8   // 丢线后先沿上一次方向找 80ms
#define LOST_REVERSE_TICKS          4   // 仍找不到再轻退 40ms

static const int16_t tracker_weight[TRACKER_CHANNEL_NUM] = {-700, -480, -260, -120, 0, 120, 300, 700};

static int8_t tracker_last_turn_dir = 0;       // -1 左转，1 右转
static int8_t tracker_acute_turn_dir = 0;      // 锐角锁存方向
static uint8_t tracker_acute_hold_ticks = 0;   // 锐角剩余锁存时间
static uint8_t tracker_lost_ticks = 0;         // 连续丢线时间
static uint8_t tracker_right_acute_hits = 0;   // 右锐角确认计数
static uint8_t tracker_left_acute_hits = 0;    // 左锐角确认计数
static int16_t tracker_last_error = 0;         // 上次误差
static int16_t tracker_filtered_error = 0;     // 低通后的误差

static int16_t Tracker_Abs(int16_t value)
{
    return (value >= 0) ? value : -value;
}

static int16_t Tracker_Clamp(int16_t value, int16_t min_value, int16_t max_value)
{
    if(value < min_value) return min_value;
    if(value > max_value) return max_value;
    return value;
}

static void Tracker_Run_Turn(int8_t dir, int16_t outer_speed, int16_t inner_speed)
{
    if(dir > 0)
    {
        Motor_Set(outer_speed, inner_speed);
    }
    else if(dir < 0)
    {
        Motor_Set(inner_speed, outer_speed);
    }
    else
    {
        Motor_Set(TRACK_BASE_SPEED, TRACK_BASE_SPEED);
    }
}

static uint8_t Tracker_Is_All_White(uint8_t L3, uint8_t L2, uint8_t L1, uint8_t L0,
                                    uint8_t M, uint8_t R0, uint8_t R1, uint8_t R3)
{
    return (L3==0 && L2==0 && L1==0 && L0==0 &&
            M==0 && R0==0 && R1==0 && R3==0);
}

static uint8_t Tracker_Is_Right_Acute_Candidate(uint8_t L3, uint8_t L2, uint8_t L1, uint8_t L0,
                                                uint8_t M, uint8_t R0, uint8_t R1, uint8_t R3)
{
    uint8_t total = L3 + L2 + L1 + L0 + M + R0 + R1 + R3;
    uint8_t right_cluster = (R3 == 1) || ((R1 == 1) && (R0 == 1));

    return (M == 0) &&
           (L0 == 0) && (L1 == 0) && (L2 == 0) && (L3 == 0) &&
           right_cluster &&
           (total <= 3);
}

static uint8_t Tracker_Is_Left_Acute_Candidate(uint8_t L3, uint8_t L2, uint8_t L1, uint8_t L0,
                                               uint8_t M, uint8_t R0, uint8_t R1, uint8_t R3)
{
    uint8_t total = L3 + L2 + L1 + L0 + M + R0 + R1 + R3;
    uint8_t left_cluster = (L3 == 1) || ((L1 == 1) && (L0 == 1));

    return (M == 0) &&
           (R0 == 0) && (R1 == 0) && (R3 == 0) &&
           left_cluster &&
           (total <= 3);
}

static void Tracker_Lock_Acute_Turn(int8_t dir)
{
    tracker_last_turn_dir = dir;
    tracker_acute_turn_dir = dir;
    tracker_acute_hold_ticks = ACUTE_MAX_HOLD_TICKS;
    tracker_lost_ticks = 0;
}

static uint8_t Tracker_Is_Acute_Recovered(int8_t dir, uint8_t L1, uint8_t L0,
                                          uint8_t M, uint8_t R0, uint8_t R1)
{
    if(dir > 0)
    {
        return (M == 1) || (R0 == 1) || (R1 == 1);
    }
    return (M == 1) || (L0 == 1) || (L1 == 1);
}

static void Tracker_Update_Acute_Confirm(uint8_t right_candidate, uint8_t left_candidate)
{
    if(right_candidate)
    {
        if(tracker_right_acute_hits < 255) tracker_right_acute_hits++;
        tracker_left_acute_hits = 0;
    }
    else if(left_candidate)
    {
        if(tracker_left_acute_hits < 255) tracker_left_acute_hits++;
        tracker_right_acute_hits = 0;
    }
    else
    {
        tracker_right_acute_hits = 0;
        tracker_left_acute_hits = 0;
    }
}

static int16_t Tracker_Calc_Error(void)
{
    int32_t weighted_sum = 0;
    int16_t active_count = 0;
    uint8_t i;

    for(i = 0; i < TRACKER_CHANNEL_NUM; i++)
    {
        if(tracker.channel[i])
        {
            weighted_sum += tracker_weight[i];
            active_count++;
        }
    }

    if(active_count == 0)
    {
        return tracker_last_error;
    }

    return (int16_t)(weighted_sum / active_count);
}

static void Tracker_Run_PD_Control(void)
{
    int16_t error = Tracker_Calc_Error();
    int16_t base_speed;
    int16_t turn;
    int16_t diff;
    int16_t left_speed;
    int16_t right_speed;

    tracker_filtered_error = (int16_t)((tracker_filtered_error * 3 + error) / 4);
    diff = tracker_filtered_error - tracker_last_error;

    turn = (int16_t)((tracker_filtered_error * TRACK_KP_NUM + diff * TRACK_KD_NUM) / TRACK_GAIN_DEN);
    turn = Tracker_Clamp(turn, -TRACK_TURN_LIMIT, TRACK_TURN_LIMIT);

    base_speed = TRACK_BASE_SPEED;
    if(Tracker_Abs(tracker_filtered_error) > 450)
    {
        base_speed = TRACK_CORNER_SPEED;
    }
    else if(Tracker_Abs(tracker_filtered_error) > 220)
    {
        base_speed = (TRACK_BASE_SPEED + TRACK_CORNER_SPEED) / 2;
    }

    left_speed = base_speed + turn;
    right_speed = base_speed - turn;

    left_speed = Tracker_Clamp(left_speed, TRACK_MIN_SPEED, TRACK_MAX_SPEED);
    right_speed = Tracker_Clamp(right_speed, TRACK_MIN_SPEED, TRACK_MAX_SPEED);

    Motor_Set(left_speed, right_speed);

    if(tracker_filtered_error > 40)
    {
        tracker_last_turn_dir = 1;
    }
    else if(tracker_filtered_error < -40)
    {
        tracker_last_turn_dir = -1;
    }
    else
    {
        tracker_last_turn_dir = 0;
    }

    tracker_last_error = tracker_filtered_error;
}


static void tracker_delay_us(uint32_t us)
{
    uint32_t count = us * 8;
    while(count--);
}


static void Tracker_Select_Channel(uint8_t ch)
{
    if(ch > 7) return;

    GPIO_ResetBits(TRACKER_S2_PORT, TRACKER_S2_PIN);
    GPIO_ResetBits(TRACKER_S1_PORT, TRACKER_S1_PIN);
    GPIO_ResetBits(TRACKER_S0_PORT, TRACKER_S0_PIN);

    if(ch & 0x04) GPIO_SetBits(TRACKER_S2_PORT, TRACKER_S2_PIN);
    if(ch & 0x02) GPIO_SetBits(TRACKER_S1_PORT, TRACKER_S1_PIN);
    if(ch & 0x01) GPIO_SetBits(TRACKER_S0_PORT, TRACKER_S0_PIN);

    tracker_delay_us(20);
}


void Tracker_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);

    GPIO_InitStruct.GPIO_Pin = TRACKER_S0_PIN | TRACKER_S1_PIN | TRACKER_S2_PIN;
    GPIO_InitStruct.GPIO_Mode = GPIO_Mode_Out_PP;
    GPIO_InitStruct.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &GPIO_InitStruct);

    GPIO_ResetBits(TRACKER_S0_PORT, TRACKER_S0_PIN);
    GPIO_ResetBits(TRACKER_S1_PORT, TRACKER_S1_PIN);
    GPIO_ResetBits(TRACKER_S2_PORT, TRACKER_S2_PIN);

    GPIO_InitStruct.GPIO_Pin = TRACKER_Z_PIN;
    GPIO_InitStruct.GPIO_Mode = GPIO_Mode_IPU;
    GPIO_Init(GPIOA, &GPIO_InitStruct);

    tracker.raw_data = 0x00;
    for(uint8_t i=0; i<TRACKER_CHANNEL_NUM; i++)
    {
        tracker.channel[i] = 0;
    }
}


void Tracker_Scan(void)
{
    tracker.raw_data = 0;

    for(uint8_t i=0; i<TRACKER_CHANNEL_NUM; i++)
    {
        Tracker_Select_Channel(i);
        uint8_t val = GPIO_ReadInputDataBit(TRACKER_Z_PORT, TRACKER_Z_PIN);
        tracker.channel[i] = val;
        tracker.raw_data |= (val << i);
    }
}



uint8_t Tracker_Get(uint8_t ch)
{
    if(ch >= TRACKER_CHANNEL_NUM)
    {
        return 0;
    }
    return tracker.channel[ch];
}



void xunji_8(void)
{
    uint8_t right_acute_candidate;
    uint8_t left_acute_candidate;

    Tracker_Scan();

    uint8_t L3 = Tracker_Get(0);
    uint8_t L2 = Tracker_Get(1);
    uint8_t L1 = Tracker_Get(2);
    uint8_t L0 = Tracker_Get(3);
    uint8_t M  = Tracker_Get(4);
    uint8_t R0 = Tracker_Get(5);
    uint8_t R1 = Tracker_Get(6);
    uint8_t R3 = Tracker_Get(7);

    right_acute_candidate = Tracker_Is_Right_Acute_Candidate(L3, L2, L1, L0, M, R0, R1, R3);
    left_acute_candidate = Tracker_Is_Left_Acute_Candidate(L3, L2, L1, L0, M, R0, R1, R3);

    // 锐角模式：先锁存一小段，再等近中间探头重新吃到线才退出
    if(tracker_acute_turn_dir != 0)
    {
        uint8_t min_hold_done = (tracker_acute_hold_ticks <= (ACUTE_MAX_HOLD_TICKS - ACUTE_MIN_HOLD_TICKS));
        if(min_hold_done && Tracker_Is_Acute_Recovered(tracker_acute_turn_dir, L1, L0, M, R0, R1))
        {
            tracker_acute_turn_dir = 0;
            tracker_acute_hold_ticks = 0;
            tracker_right_acute_hits = 0;
            tracker_left_acute_hits = 0;
        }
        else if(tracker_acute_hold_ticks > 0)
        {
            tracker_acute_hold_ticks--;
            Tracker_Run_Turn(tracker_acute_turn_dir, ACUTE_OUTER_SPEED, ACUTE_INNER_SPEED);
            return;
        }
        else
        {
            tracker_acute_turn_dir = 0;
        }
    }

    // ====================== 2. 全白丢线 ======================
    if(Tracker_Is_All_White(L3, L2, L1, L0, M, R0, R1, R3))
    {
        tracker_lost_ticks++;

        if(tracker_last_turn_dir != 0)
        {
            if(tracker_lost_ticks <= LOST_SEARCH_TICKS)
            {
                Tracker_Run_Turn(tracker_last_turn_dir, SEARCH_TURN_OUTER_SPEED, SEARCH_TURN_INNER_SPEED);
            }
            else if(tracker_lost_ticks <= (LOST_SEARCH_TICKS + LOST_REVERSE_TICKS))
            {
                Motor_Set(-110, -110);
            }
            else
            {
                Tracker_Run_Turn(tracker_last_turn_dir, SEARCH_TURN_OUTER_SPEED + 20, -40);
            }
        }
        else
        {
            Motor_Set(SEARCH_FORWARD_SPEED, SEARCH_FORWARD_SPEED);
        }
        return;
    }

    tracker_lost_ticks = 0;
    Tracker_Update_Acute_Confirm(right_acute_candidate, left_acute_candidate);

    if(tracker_right_acute_hits >= ACUTE_CONFIRM_TICKS)
    {
        Tracker_Lock_Acute_Turn(1);
        Tracker_Run_Turn(1, ACUTE_OUTER_SPEED, ACUTE_INNER_SPEED);
        return;
    }

    if(tracker_left_acute_hits >= ACUTE_CONFIRM_TICKS)
    {
        Tracker_Lock_Acute_Turn(-1);
        Tracker_Run_Turn(-1, ACUTE_OUTER_SPEED, ACUTE_INNER_SPEED);
        return;
    }

    Tracker_Run_PD_Control();
}
