/**
 * @file StraightControl.c
 * @brief 走直线模式 - 内联PID计算
 */

#include "StraightControl.h"
#include "Motor.h"
#include "VOFA.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>

#define DT 0.010f       // 10ms

/*===========================================================================
 * 私有函数
 *========================================================================*/
static float wrap180(float v) {
    while (v > 180) v -= 360;
    while (v < -180) v += 360;
    return v;
}

static float clamp(float v, float lo, float hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static int16_t toPwm(float v, int16_t lo, int16_t hi) {
    v = (v >= 0) ? v + 0.5f : v - 0.5f;
    int16_t p = (int16_t)v;
    return (p < lo) ? lo : (p > hi) ? hi : p;
}

/*===========================================================================
 * 航向环PID(外环)
 *========================================================================*/
static void heading_pid(StraightSys_t *s) {
    if (!s->imuOk) {
        s->headingOut = 0;
        return;
    }
    
    float target = s->testMode ? s->targetYaw : 0.0f;
    float actual = wrap180(s->imu.yaw - target);
    s->actualYaw = actual;
    
    // 误差
    s->yawError = 0.0f - actual;
    
    // 积分
    s->yawI += s->yawError * DT;
    s->yawI = clamp(s->yawI, -s->cfg.headingILimit, s->cfg.headingILimit);
    
    // 微分
    float deriv = (s->yawError - s->yawPrevErr) / DT;
    
    // PID
    s->headingOut = s->cfg.headingKp * s->yawError +
                    s->cfg.headingKi * s->yawI +
                    s->cfg.headingKd * deriv;
    s->headingOut = clamp(s->headingOut, -s->cfg.headingOutLimit, s->cfg.headingOutLimit);
    
    s->yawPrevErr = s->yawError;
}

/*===========================================================================
 * 速度环PID(内环)
 *========================================================================*/
static void speed_pid(StraightSys_t *s) {
    if (!s->run) {
        s->speedOut = 0;
        return;
    }
    
    Encoder_UpdateSpeed(10);
    EncoderData_t *e = Encoder_GetData();
    s->actualSpeed = (e->leftSpeed + e->rightSpeed) * 0.5f;
    
    // 误差
    s->speedError = s->cfg.targetSpeed - s->actualSpeed;
    
    // 积分
    s->speedI += s->speedError * DT;
    s->speedI = clamp(s->speedI, -s->cfg.speedILimit, s->cfg.speedILimit);
    
    // 微分
    float deriv = (s->speedError - s->speedPrevErr) / DT;
    
    // PID
    s->speedOut = s->cfg.speedKp * s->speedError +
                  s->cfg.speedKi * s->speedI +
                  s->cfg.speedKd * deriv;
    s->speedOut = clamp(s->speedOut, -s->cfg.speedOutLimit, s->cfg.speedOutLimit);
    
    s->speedPrevErr = s->speedError;
}

/*===========================================================================
 * 输出混控
 *========================================================================*/
static void apply(StraightSys_t *s) {
    if (!s->run) {
        s->leftPwm = 0;
        s->rightPwm = 0;
        Motor_Stop();
        Motor_Disable();
        return;
    }
    
    int16_t base = toPwm(s->speedOut, -s->cfg.maxPwm, s->cfg.maxPwm);
    int16_t diff = toPwm(s->headingOut, -s->cfg.maxPwm, s->cfg.maxPwm);
    
    s->leftPwm = base + diff;
    s->rightPwm = base - diff;
    
    s->leftPwm = clamp(s->leftPwm, (float)-s->cfg.maxPwm, (float)s->cfg.maxPwm);
    s->rightPwm = clamp(s->rightPwm, (float)-s->cfg.maxPwm, (float)s->cfg.maxPwm);
    
    Motor_SetDiffSpeed(s->leftPwm, s->rightPwm);
}

/*===========================================================================
 * 命令解析
 *========================================================================*/
static float getVal(const char *c) {
    const char *p = strchr(c, '=');
    return p ? atof(p + 1) : 0;
}

static void parse(StraightSys_t *s, char *cmd) {
    if (!cmd) return;
    
    if (!strcmp(cmd, "#RUN")) { Straight_Start(s); VOFA_SendString("OK RUN\n"); return; }
    if (!strcmp(cmd, "#STOP")) { Straight_Stop(s); VOFA_SendString("OK STOP\n"); return; }
    if (!strncmp(cmd, "#TS=", 4)) { s->cfg.targetSpeed = getVal(cmd); return; }
    
    if (!strncmp(cmd, "#HKP=", 5)) s->cfg.headingKp = getVal(cmd);
    if (!strncmp(cmd, "#HKI=", 5)) s->cfg.headingKi = getVal(cmd);
    if (!strncmp(cmd, "#HKD=", 5)) s->cfg.headingKd = getVal(cmd);
    
    if (!strncmp(cmd, "#SKP=", 5)) s->cfg.speedKp = getVal(cmd);
    if (!strncmp(cmd, "#SKI=", 5)) s->cfg.speedKi = getVal(cmd);
    if (!strncmp(cmd, "#SKD=", 5)) s->cfg.speedKd = getVal(cmd);
    
    if (!strncmp(cmd, "#TEST=", 6)) {
        if (strstr(cmd, "YAW")) Straight_SetTest(s, 1);
        else Straight_SetTest(s, 0);
    }
}

static void sendHb(StraightSys_t *s) {
    static char buf[96];
    snprintf(buf, sizeof(buf),
             "HB run=%d ts=%.1f as=%.1f he=%.2f ho=%.2f L=%d R=%d\n",
             s->run, s->cfg.targetSpeed, s->actualSpeed, 
             s->yawError, s->headingOut, s->leftPwm, s->rightPwm);
    VOFA_SendString(buf);
}

/*===========================================================================
 * API实现
 *========================================================================*/
void Straight_Init(StraightSys_t *s) {
    memset(s, 0, sizeof(StraightSys_t));
    Motor_Init();
    Encoder_Timer_Init();
    BNO085_Init();
    VOFA_Init();
    Straight_LoadCfg(s);
}

void Straight_LoadCfg(StraightSys_t *s) {
    // 航向环(外)
    s->cfg.headingKp = 1.5f;
    s->cfg.headingKi = 0.0f;
    s->cfg.headingKd = 0.12f;
    s->cfg.headingILimit = 200.0f;
    s->cfg.headingOutLimit = 20.0f;
    
    // 速度环(内)
    s->cfg.speedKp = 0.6f;
    s->cfg.speedKi = 0.012f;
    s->cfg.speedKd = 0.0f;
    s->cfg.speedILimit = 400.0f;
    s->cfg.speedOutLimit = 60.0f;
    
    s->cfg.maxPwm = 60;
    s->cfg.deadzone = 11;
    s->cfg.targetSpeed = 35.0f;
}

uint8_t Straight_Start(StraightSys_t *s) {
    Straight_Stop(s);
    
    for (int t = 0; t < 300; t++) {
        if (BNO085_ReadAll(&s->imu)) {
            BNO085_UpdateYaw(&s->imu, 0.005f);
            if (s->imu.ahrsInited) break;
        }
    }
    
    if (!s->imu.ahrsInited) return 0;
    
    s->targetYaw = s->imu.yaw;
    s->run = 1;
    
    s->yawI = 0;
    s->yawPrevErr = 0;
    s->speedI = 0;
    s->speedPrevErr = 0;
    
    Encoder_Reset();
    Motor_Enable();
    
    return 1;
}

void Straight_Stop(StraightSys_t *s) {
    s->run = 0;
    s->leftPwm = 0;
    s->rightPwm = 0;
    s->speedOut = 0;
    s->headingOut = 0;
    Motor_Stop();
    Motor_Disable();
}

void Straight_SetSpeed(StraightSys_t *s, float v) { s->cfg.targetSpeed = v; }
void Straight_SetHPID(StraightSys_t *s, float kp, float ki, float kd) {
    s->cfg.headingKp = kp; s->cfg.headingKi = ki; s->cfg.headingKd = kd;
}
void Straight_SetSPID(StraightSys_t *s, float kp, float ki, float kd) {
    s->cfg.speedKp = kp; s->cfg.speedKi = ki; s->cfg.speedKd = kd;
    s->speedI = 0;
}
void Straight_SetTest(StraightSys_t *s, uint8_t on) {
    s->testMode = on;
    if (on) s->targetYaw = s->imu.yaw;
}

void Straight_Tick(StraightSys_t *s) {
    s->tick++;
    
    if (BNO085_ReadAll(&s->imu)) {
        BNO085_UpdateYaw(&s->imu, DT);
        s->imuOk = 1;
    }
    
    // 外环(航向) → 内环(速度)
    heading_pid(s);
    speed_pid(s);
    apply(s);
}

void Straight_Run(StraightSys_t *s) {
    char cmd[64];
    for (int i = 0; i < 16; i++) {
        if (!VOFA_TakeCommand(cmd, sizeof(cmd))) break;
        parse(s, cmd);
    }
    if ((s->tick % 20) == 0) sendHb(s);
}
