/**
 * @file  line_track.h
 * @brief 8-channel line tracking state machine
 *        Ported from 5-sensor (至善电子 V2.3.0_B) logic, adapted for 8 sensors.
 *        Middle: 5-sensor has 1 IR (M), 8-sensor has 2 IR (S4, S5).
 *        Turning logic preserved from the 5-sensor reference.
 */
#ifndef __LINE_TRACK_H
#define __LINE_TRACK_H

#include "stm32f10x.h"
#include "config.h"

/* ========== Track State Machine ========== */
#define LT_STATE_IDLE      0
#define LT_STATE_STARTING  1
#define LT_STATE_RUNNING   2

#define LT_FLAG_STOP       0
#define LT_FLAG_START      1

#define LT_DIR_LEFT        1
#define LT_DIR_RIGHT       2

/* ========== 8-sensor bit layout ========== */
/*  Physical (left to right): S1  S2  S3  S4  S5  S6  S7  S8
 *  Bit index:                 0   1   2   3   4   5   6   7
 *
 *  5-sensor equivalence:
 *    S1,S2 ~ L1 (far left)    S3 ~ L0     S4,S5 ~ M (middle)
 *    S6 ~ R0                  S7,S8 ~ R1 (far right)
 *
 *  Masks:
 */
#define LT_MASK_FAR_LEFT   0x03  /* S1|S2 = bits 0,1 */
#define LT_MASK_LEFT       0x04  /* S3    = bit  2    */
#define LT_MASK_MID_L      0x08  /* S4    = bit  3    */
#define LT_MASK_MID_R      0x10  /* S5    = bit  4    */
#define LT_MASK_MID        0x18  /* S4|S5 = bits 3,4  */
#define LT_MASK_RIGHT      0x20  /* S6    = bit  5    */
#define LT_MASK_FAR_RIGHT  0xC0  /* S7|S8 = bits 6,7  */
#define LT_MASK_ALL        0xFF

typedef struct {
    uint8_t  state;            /* LT_STATE_xxx */
    uint8_t  autoFlag;         /* LT_FLAG_xxx  */

    int8_t   bearingDev;       /* position deviation: +=left, -=right (same as 5-sensor) */
    uint8_t  sensorBits;       /* current 8-bit raw sensor data (1=on line) */
    uint8_t  lastCornerBits;   /* last sensor data when outer sensors were active */

    uint8_t  overrunCount;     /* consecutive "no sensor" ticks */
    uint8_t  filterTimes;      /* general-purpose tick counter */
    uint8_t  cornerDone;       /* 1=刚完成转弯, main需重置速度PID */
    uint8_t  cornerTurning;    /* 1=正在非阻塞式原地旋转找线 */
    uint8_t  cornerDir;        /* 旋转方向: LT_DIR_LEFT / LT_DIR_RIGHT */
    uint32_t cornerStartTick;  /* 旋转开始时刻(ms) */

    uint8_t  crossing;         /* total crossings to count before auto-stop */
    uint8_t  crossCount;       /* crossings detected so far */
    uint8_t  crossState;       /* 1=waiting, 2=seen */

    int16_t  devSpeed;         /* PID output: differential speed */
    int16_t  weightedPos;      /* current weighted position [-350..+350] */
    int16_t  lastPos;          /* previous weightedPos for D term */
    float    filteredDPos;      /* D项低通滤波后的位置变化率 */

    float    kp;               /* runtime P gain (default: PID_TRACK_LINE_KP) */
    float    kd;               /* runtime D gain (default: PID_TRACK_LINE_KD) */
} LineTrack_State_t;

extern LineTrack_State_t g_lineTrack;

/* ========== API ========== */

/** Initialise GPIO (delegates to LineSensor_Init) and reset state. */
void LineTrack_Init(void);

/** Reset state and prepare for a new run.
 *  @param crossings  number of crossings to count before auto-stop (e.g. 4) */
void LineTrack_Start(uint8_t crossings);

/** Force stop. */
void LineTrack_Stop(void);

/** Call every control tick (10-20 ms) while in TRACK mode.
 *  Reads sensors, updates bearing_dev, runs line PD, drives motors.
 *  Handles corners (blocking pivot) and cross detection internally.
 *  @param tickMs   current system tick (ms), used for corner timeout.
 *  @param basePwm  speed-loop output (pwmCore) used as base motor PWM. */
void LineTrack_Update(uint32_t tickMs, int16_t basePwm);

/** @return 1 if tracking is actively running. */
uint8_t LineTrack_IsRunning(void);

/** Set PID gains at runtime (e.g. from serial #LKP=/#LKD= commands). */
void LineTrack_SetPID(float kp, float kd);

#endif /* __LINE_TRACK_H */
