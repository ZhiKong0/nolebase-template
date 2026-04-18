/**
 * @file StraightControl.h
 * @brief 走直线模式 - 串级双环PID
 * @description 
 *   控制架构（正确版）:
 *   ┌─────────────┐     ┌─────────────┐
 *   │  航向环(外)  │────>│   差速修正   │
 *   └─────────────┘     └──────┬──────┘
 *          │                   │
 *          │              ┌────▼──────┐
 *          │              │  速度环   │
 *          │              │ (内环)   │
 *          │              └────┬──────┘
 *          │                   │
 *          └─────────────>基础PWM
 * 
 *   即: 外环输出 → 内环目标修正
 */

#ifndef __STRAIGHT_CONTROL_H
#define __STRAIGHT_CONTROL_H

#include "stm32f10x.h"
#include "Encoder_Timer.h"
#include "BNO085.h"

/*===========================================================================
 * 走直线系统配置
 *========================================================================*/
typedef struct {
    // 航向环(外环)
    float headingKp;
    float headingKi;
    float headingKd;
    float headingILimit;
    float headingOutLimit;
    
    // 速度环(内环)
    float speedKp;
    float speedKi;
    float speedKd;
    float speedILimit;
    float speedOutLimit;
    
    // 电机
    int16_t maxPwm;
    int16_t deadzone;
    
    // 目标
    float targetSpeed;
} StraightConfig_t;

/*===========================================================================
 * 走直线系统状态
 *========================================================================*/
typedef struct {
    StraightConfig_t cfg;
    
    // 航向环(外环)
    float targetYaw;
    float actualYaw;
    float yawError;
    float yawI;
    float yawPrevErr;
    float headingOut;
    
    // 速度环(内环)
    float actualSpeed;
    float speedError;
    float speedI;
    float speedPrevErr;
    float speedOut;
    
    // 输出
    int16_t leftPwm;
    int16_t rightPwm;
    
    // 状态
    uint8_t run;
    uint32_t tick;
    uint8_t testMode;
    uint8_t imuOk;
    BNO085_Data_t imu;
} StraightSys_t;

/*===========================================================================
 * API
 *========================================================================*/
void Straight_Init(StraightSys_t *s);
void Straight_LoadCfg(StraightSys_t *s);
uint8_t Straight_Start(StraightSys_t *s);
void Straight_Stop(StraightSys_t *s);
void Straight_SetSpeed(StraightSys_t *s, float v);
void Straight_SetHPID(StraightSys_t *s, float kp, float ki, float kd);
void Straight_SetSPID(StraightSys_t *s, float kp, float ki, float kd);
void Straight_SetTest(StraightSys_t *s, uint8_t on);
void Straight_Tick(StraightSys_t *s);
void Straight_Run(StraightSys_t *s);

#endif
