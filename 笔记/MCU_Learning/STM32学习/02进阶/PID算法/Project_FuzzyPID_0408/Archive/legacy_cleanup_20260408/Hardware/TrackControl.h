/**
 * @file TrackControl.h
 * @brief 巡线模式 - 串级双环PID
 * @description 
 *   控制架构（正确版）:
 *   ┌─────────────┐     ┌─────────────┐
 *   │  巡线环(外)  │────>│   差速修正   │
 *   └─────────────┘     └──────┬──────┘
 *          │                   │
 *          │              ┌────▼──────┐
 *          │              │  速度环   │
 *          │              │ (内环×2) │
 *          │              │ 左 + 右   │
 *          │              └────┬──────┘
 *          │                   │
 *          └─────────────>左右电机PWM
 * 
 *   即: 外环输出 → 内环目标修正
 */

#ifndef __TRACK_CONTROL_H
#define __TRACK_CONTROL_H

#include "stm32f10x.h"
#include "Encoder_Timer.h"

/*===========================================================================
 * 巡线系统配置
 *========================================================================*/
typedef struct {
    // 巡线环(外环)
    float lineKp;
    float lineKi;
    float lineKd;
    float lineILimit;
    float lineDiffLimit;
    
    // 速度环(内环)×2
    float speedKp;
    float speedKi;
    float speedKd;
    float speedILimit;
    float speedOutLimit;
    
    // 传感器
    float filtAlpha;
    float deadband;
    
    // 电机
    int16_t maxPwm;
    
    // 目标
    float targetSpeed;
} TrackConfig_t;

/*===========================================================================
 * 巡线系统状态
 *========================================================================*/
typedef struct {
    TrackConfig_t cfg;
    
    // 巡线环(外环)
    float linePos;
    float lineErr;
    float lineI;
    float linePrevErr;
    float lineOut;
    
    // 左速度环(内环)
    float leftTarget;
    float leftActual;
    float leftErr;
    float leftI;
    float leftPrevErr;
    float leftOut;
    
    // 右速度环(内环)
    float rightTarget;
    float rightActual;
    float rightErr;
    float rightI;
    float rightPrevErr;
    float rightOut;
    
    // 传感器
    uint8_t sensors;
    uint8_t count;
    uint8_t hasLine;
    uint8_t lostCnt;
    
    // 输出
    int16_t leftPwm;
    int16_t rightPwm;
    
    // 状态
    uint8_t run;
    uint32_t tick;
} TrackSys_t;

/*===========================================================================
 * API
 *========================================================================*/
void Track_Init(TrackSys_t *t);
void Track_LoadCfg(TrackSys_t *t);
uint8_t Track_Start(TrackSys_t *t);
void Track_Stop(TrackSys_t *t);
void Track_SetSpeed(TrackSys_t *t, float v);
void Track_SetLinePID(TrackSys_t *t, float kp, float ki, float kd);
void Track_SetSpeedPID(TrackSys_t *t, float kp, float ki, float kd);
void Track_Tick(TrackSys_t *t);
void Track_Run(TrackSys_t *t);

#endif
