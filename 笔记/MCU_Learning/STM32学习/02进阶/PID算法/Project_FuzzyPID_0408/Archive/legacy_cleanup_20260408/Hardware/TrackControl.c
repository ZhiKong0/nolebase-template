/**
 * @file TrackControl.c
 * @brief 巡线模式 - 内联PID计算
 */

#include "TrackControl.h"
#include "Motor.h"
#include "VOFA.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>

#define DT 0.010f

// 传感器引脚定义
static const int SENSOR_PINS[8] = {
    GPIO_Pin_10, GPIO_Pin_11, GPIO_Pin_12, GPIO_Pin_3,
    GPIO_Pin_4, GPIO_Pin_9, GPIO_Pin_11, GPIO_Pin_13
};
static const GPIO_TypeDef* SENSOR_PORTS[8] = {
    GPIOA, GPIOA, GPIOA, GPIOB, GPIOB, GPIOB, GPIOB, GPIOC
};
static const int SENSOR_W[8] = { -350, -250, -150, -50, 50, 150, 250, 350 };

/*===========================================================================
 * 私有函数
 *========================================================================*/
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
 * 传感器读取
 *========================================================================*/
static void readSensors(TrackSys_t *t) {
    uint8_t bits = 0;
    for (int i = 0; i < 8; i++) {
        if (GPIO_ReadInputDataBit((GPIO_TypeDef*)SENSOR_PORTS[i], SENSOR_PINS[i])) {
            bits |= (1 << i);
        }
    }
    t->sensors = bits;
    
    t->count = 0;
    for (int i = 0; i < 8; i++) {
        if (bits & (1 << i)) t->count++;
    }
    
    t->hasLine = (t->count > 0);
    if (!t->hasLine) t->lostCnt++;
    else t->lostCnt = 0;
    
    if (t->count > 0) {
        int32_t sum = 0;
        for (int i = 0; i < 8; i++) {
            if (bits & (1 << i)) sum += SENSOR_W[i];
        }
        float raw = (float)sum / t->count;
        t->linePos = t->cfg.filtAlpha * raw + (1 - t->cfg.filtAlpha) * t->linePos;
    }
}

/*===========================================================================
 * 巡线环PID(外环)
 *========================================================================*/
static void line_pid(TrackSys_t *t) {
    if (!t->run) {
        t->lineOut = 0;
        return;
    }
    
    float err = 0.0f - t->linePos;
    if (fabsf(err) < t->cfg.deadband) err = 0;
    
    t->lineErr = err;
    t->lineI += err * DT;
    t->lineI = clamp(t->lineI, -t->cfg.lineILimit, t->cfg.lineILimit);
    
    float deriv = (err - t->linePrevErr) / DT;
    
    t->lineOut = t->cfg.lineKp * err + t->cfg.lineKi * t->lineI + t->cfg.lineKd * deriv;
    t->lineOut = clamp(t->lineOut, -t->cfg.lineDiffLimit, t->cfg.lineDiffLimit);
    
    t->linePrevErr = err;
    
    // 脱线恢复
    if (t->lostCnt > 10) {
        t->lineOut = (t->linePrevErr > 0) ? t->cfg.lineDiffLimit : -t->cfg.lineDiffLimit;
    }
}

/*===========================================================================
 * 速度环PID(内环)×2
 *========================================================================*/
static void speed_pid(TrackSys_t *t) {
    if (!t->run) {
        t->leftOut = 0;
        t->rightOut = 0;
        return;
    }
    
    Encoder_UpdateSpeed(10);
    EncoderData_t *e = Encoder_GetData();
    
    t->leftActual = e->leftSpeed;
    t->rightActual = e->rightSpeed;
    
    float base = t->cfg.targetSpeed;
    float diff = t->lineOut;
    
    t->leftTarget = base + diff;
    t->rightTarget = base - diff;
    
    // 左轮
    float le = t->leftTarget - t->leftActual;
    t->leftErr = le;
    t->leftI += le * DT;
    t->leftI = clamp(t->leftI, -t->cfg.speedILimit, t->cfg.speedILimit);
    float ld = (le - t->leftPrevErr) / DT;
    t->leftOut = t->cfg.speedKp * le + t->cfg.speedKi * t->leftI + t->cfg.speedKd * ld;
    t->leftOut = clamp(t->leftOut, -t->cfg.speedOutLimit, t->cfg.speedOutLimit);
    t->leftPrevErr = le;
    
    // 右轮
    float re = t->rightTarget - t->rightActual;
    t->rightErr = re;
    t->rightI += re * DT;
    t->rightI = clamp(t->rightI, -t->cfg.speedILimit, t->cfg.speedILimit);
    float rd = (re - t->rightPrevErr) / DT;
    t->rightOut = t->cfg.speedKp * re + t->cfg.speedKi * t->rightI + t->cfg.speedKd * rd;
    t->rightOut = clamp(t->rightOut, -t->cfg.speedOutLimit, t->cfg.speedOutLimit);
    t->rightPrevErr = re;
}

/*===========================================================================
 * 输出
 *========================================================================*/
static void apply(TrackSys_t *t) {
    if (!t->run) {
        t->leftPwm = 0;
        t->rightPwm = 0;
        Motor_Stop();
        Motor_Disable();
        return;
    }
    
    t->leftPwm = toPwm(t->leftOut, -t->cfg.maxPwm, t->cfg.maxPwm);
    t->rightPwm = toPwm(t->rightOut, -t->cfg.maxPwm, t->cfg.maxPwm);
    
    Motor_SetDiffSpeed(t->leftPwm, t->rightPwm);
}

/*===========================================================================
 * 命令
 *========================================================================*/
static float getVal(const char *c) {
    const char *p = strchr(c, '=');
    return p ? atof(p + 1) : 0;
}

static void parse(TrackSys_t *t, char *cmd) {
    if (!cmd) return;
    
    if (!strcmp(cmd, "#RUN")) { Track_Start(t); VOFA_SendString("OK RUN\n"); return; }
    if (!strcmp(cmd, "#STOP")) { Track_Stop(t); VOFA_SendString("OK STOP\n"); return; }
    if (!strncmp(cmd, "#TS=", 4)) { t->cfg.targetSpeed = getVal(cmd); return; }
    
    if (!strncmp(cmd, "#LKP=", 5)) t->cfg.lineKp = getVal(cmd);
    if (!strncmp(cmd, "#LKI=", 5)) t->cfg.lineKi = getVal(cmd);
    if (!strncmp(cmd, "#LKD=", 5)) t->cfg.lineKd = getVal(cmd);
    
    if (!strncmp(cmd, "#SKP=", 5)) t->cfg.speedKp = getVal(cmd);
    if (!strncmp(cmd, "#SKI=", 5)) t->cfg.speedKi = getVal(cmd);
    if (!strncmp(cmd, "#SKD=", 5)) t->cfg.speedKd = getVal(cmd);
}

static void sendHb(TrackSys_t *t) {
    static char buf[96];
    snprintf(buf, sizeof(buf),
             "HB run=%d lp=%.2f le=%.2f lo=%.2f L=%d R=%d\n",
             t->run, t->linePos, t->lineErr, t->lineOut, t->leftPwm, t->rightPwm);
    VOFA_SendString(buf);
}

/*===========================================================================
 * API
 *========================================================================*/
void Track_Init(TrackSys_t *t) {
    memset(t, 0, sizeof(TrackSys_t));
    Motor_Init();
    Encoder_Timer_Init();
    VOFA_Init();
    Track_LoadCfg(t);
}

void Track_LoadCfg(TrackSys_t *t) {
    // 巡线环(外)
    t->cfg.lineKp = 0.4f;
    t->cfg.lineKi = 0.004f;
    t->cfg.lineKd = 0.0f;
    t->cfg.lineILimit = 120.0f;
    t->cfg.lineDiffLimit = 0.55f;
    
    // 速度环(内)×2
    t->cfg.speedKp = 10.0f;
    t->cfg.speedKi = 0.25f;
    t->cfg.speedKd = 0.0f;
    t->cfg.speedILimit = 240.0f;
    t->cfg.speedOutLimit = 100.0f;
    
    t->cfg.filtAlpha = 0.45f;
    t->cfg.deadband = 0.15f;
    t->cfg.maxPwm = 72;
    t->cfg.targetSpeed = 18.0f;
}

uint8_t Track_Start(TrackSys_t *t) {
    Track_Stop(t);
    t->run = 1;
    t->lineI = 0;
    t->leftI = 0;
    t->rightI = 0;
    t->linePrevErr = 0;
    t->leftPrevErr = 0;
    t->rightPrevErr = 0;
    Encoder_Reset();
    Motor_Enable();
    return 1;
}

void Track_Stop(TrackSys_t *t) {
    t->run = 0;
    t->leftPwm = 0;
    t->rightPwm = 0;
    Motor_Stop();
    Motor_Disable();
}

void Track_SetSpeed(TrackSys_t *t, float v) { t->cfg.targetSpeed = v; }
void Track_SetLinePID(TrackSys_t *t, float kp, float ki, float kd) {
    t->cfg.lineKp = kp; t->cfg.lineKi = ki; t->cfg.lineKd = kd;
    t->lineI = 0;
}
void Track_SetSpeedPID(TrackSys_t *t, float kp, float ki, float kd) {
    t->cfg.speedKp = kp; t->cfg.speedKi = ki; t->cfg.speedKd = kd;
    t->leftI = 0; t->rightI = 0;
}

void Track_Tick(TrackSys_t *t) {
    t->tick++;
    readSensors(t);
    line_pid(t);      // 外环
    speed_pid(t);     // 内环×2
    apply(t);
}

void Track_Run(TrackSys_t *t) {
    char cmd[64];
    for (int i = 0; i < 16; i++) {
        if (!VOFA_TakeCommand(cmd, sizeof(cmd))) break;
        parse(t, cmd);
    }
    if ((t->tick % 20) == 0) sendHb(t);
}
