#ifndef __ENCODER_TIMER_H
#define __ENCODER_TIMER_H
#include "stm32f10x.h"

// 编码器数据结构
typedef struct {
    int32_t leftCount;      // 左轮累计计数
    int32_t rightCount;     // 右轮累计计数
    int16_t leftSpeed;      // 左轮速度 (计数/周期)
    int16_t rightSpeed;     // 右轮速度 (计数/周期)
    float leftRPM;          // 左轮转速 (RPM)
    float rightRPM;         // 右轮转速 (RPM)
} Encoder_Data_t;

// 初始化（TIM2/TIM3 编码器模式）
void Encoder_Timer_Init(void);

// 读取当前计数
int16_t Encoder_GetLeft(void);
int16_t Encoder_GetRight(void);

// 更新速度（定时调用，如10ms）
void Encoder_UpdateSpeed(Encoder_Data_t *data, uint16_t periodMs);

// 获取速度差（用于航向融合）
int16_t Encoder_GetSpeedDiff(void);

// 计算转速 RPM（已知减速比和线数）
// 减速比: 假设30:1，线数: 假设11线（44边沿/圈）
float Encoder_CountToRPM(int16_t countPerPeriod, uint16_t periodMs);

#endif
