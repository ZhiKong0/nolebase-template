/**
 * @file  line_track.h
 * @brief 8 路循迹状态与接口
 *
 * 原始 8 路传感器布局:
 *   S1 S2 S3 S4 S5 S6 S7 S8
 *
 * 用于转角判定的 5 路等效分组:
 *   L1  = S1|S2
 *   L0  = S3
 *   M   = S4|S5
 *   R0  = S6
 *   R1  = S7|S8
 *
 * 运行方式:
 *   1. 正常循迹: 8 路加权位置 -> 中心双传感器归零 -> 输入低通 -> PD -> 差速限幅 -> 自适应降速 -> 左右轮 PWM
 *   2. 转角恢复: 等效 5 路 -> Signal_Handler -> corner_handler
 */
#ifndef __LINE_TRACK_H
#define __LINE_TRACK_H

#include "stm32f10x.h"
#include "config.h"

#define LT_STATE_IDLE      0u
#define LT_STATE_STARTING  1u
#define LT_STATE_RUNNING   2u

#define LT_FLAG_STOP       0u
#define LT_FLAG_START      1u

#define LT_DIR_LEFT        1u
#define LT_DIR_RIGHT       2u

/* Raw 8-sensor masks */
#define LT_MASK_FAR_LEFT   0x03u  /* S1|S2 */
#define LT_MASK_LEFT       0x04u  /* S3    */
#define LT_MASK_MID        0x18u  /* S4|S5 */
#define LT_MASK_RIGHT      0x20u  /* S6    */
#define LT_MASK_FAR_RIGHT  0xC0u  /* S7|S8 */

/* 五路等效布局，仅用于转角/全灭恢复判断 */
#define LT_EQ_R1           0x02u
#define LT_EQ_R0           0x04u
#define LT_EQ_M            0x08u
#define LT_EQ_L0           0x10u
#define LT_EQ_L1           0x20u
#define LT_EQ_ACTIVE_MASK  0x3Eu

typedef struct {
    uint8_t state;
    uint8_t autoFlag;

    int8_t  bearingDev;     /* 五路等效离散偏差，仅保留给转角判断与调试 */
    uint8_t sensorBits;     /* 原始 S1~S8 bits */
    uint8_t sensorData5;    /* 转角逻辑使用的五路等效 bits */
    uint8_t lastData;       /* 五路转角逻辑的 last_data */

    uint8_t filterTimes;
    uint8_t crossing;
    uint8_t crossCount;
    uint8_t crossState;
    uint8_t overrunCount;

    int16_t weightedPos;    /* 8 路原始加权位置；S4/S5 单独触发时按中心 0 处理 */
    int16_t devSpeed;       /* 八路位置 PD + 差速限幅后的输出 */
    int16_t basePwm;        /* 循迹模式基础 PWM */
    int16_t motorLPwm;
    int16_t motorRPwm;

    float   kp;             /* 八路位置误差 P 系数 */
    float   kd;             /* 八路位置误差 D 系数 */

    uint8_t cornerDone;     /* 兼容调试字段 */
    uint8_t cornerTurning;
    uint8_t cornerDir;

    uint8_t dbgTrackState;      /* 0=正常八路循迹, 4=转角搜索 */
    uint8_t dbgCornerDir;       /* 0/1/2 */
    float   dbgCornerYawDelta;  /* 未使用，保留给串口兼容 */
    uint8_t dbgCornerBits;      /* 转角期间采到的原始 8 路 bits */
    uint8_t dbgCornerAcceptMask;/* 未使用 */
    uint8_t dbgCornerYawReady;  /* 转角执行中标志 */
    uint8_t dbgCornerAcceptHit; /* 未使用 */
} LineTrack_State_t;

extern LineTrack_State_t g_lineTrack;

void LineTrack_Init(void);
void LineTrack_Start(uint8_t crossings);
void LineTrack_Stop(void);
void LineTrack_Update(uint32_t tickMs, int16_t basePwm);
uint8_t LineTrack_IsRunning(void);
void LineTrack_SetPID(float kp, float kd);
void LineTrack_SetBasePwm(int16_t basePwm);
int16_t LineTrack_GetBasePwm(void);

#endif
