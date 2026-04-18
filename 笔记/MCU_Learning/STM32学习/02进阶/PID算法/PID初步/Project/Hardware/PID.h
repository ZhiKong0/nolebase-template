#ifndef __PID_H
#define __PID_H
#include "stm32f10x.h"

// PID 参数结构
typedef struct {
    float Kp;
    float Ki;
    float Kd;
    float integral;      // 积分累计
    float prevError;     // 上次误差
    float integralLimit; // 积分限幅
    float outputLimit;   // 输出限幅
    
    // 前馈控制参数
    float Kff;           // 前馈系数（基于目标速度）
    uint8_t enableFeedforward;  // 前馈使能
    
    // 开环力矩控制参数
    float Kemf;          // 反电动势系数（基于当前速度）
    uint8_t enableTorqueControl;  // 开环力矩使能
} PID_t;

// 初始化 PID
void PID_Init(PID_t *pid, float Kp, float Ki, float Kd, float integralLimit, float outputLimit);

// 重置 PID 状态
void PID_Reset(PID_t *pid);

// 设置参数
void PID_SetParams(PID_t *pid, float Kp, float Ki, float Kd);

// 设置前馈参数
void PID_SetFeedforward(PID_t *pid, float Kff, uint8_t enable);

// 设置开环力矩参数
void PID_SetTorqueControl(PID_t *pid, float Kemf, uint8_t enable);

// PID 计算（基本PID）
float PID_Calculate(PID_t *pid, float error, float dt);

// PID 计算（带前馈）
// feedforwardValue: 前馈值（基于目标速度计算）
float PID_CalculateWithFeedforward(PID_t *pid, float error, float dt, float feedforwardValue);

// PID 计算（带开环力矩）
// currentSpeed: 当前速度，用于计算反电动势
float PID_CalculateWithTorque(PID_t *pid, float error, float dt, float currentSpeed);

// PID 计算（完整版：PID + 前馈 + 开环力矩）
// targetSpeed: 目标速度（用于前馈）
// currentSpeed: 当前速度（用于开环力矩）
float PID_CalculateFull(PID_t *pid, float error, float dt, float targetSpeed, float currentSpeed);

// 计算前馈值（基于目标速度）
float PID_CalculateFeedforward(PID_t *pid, float targetSpeed);

// 计算开环力矩补偿（基于当前速度的反电动势）
float PID_CalculateTorqueCompensation(PID_t *pid, float currentSpeed);

// 航向 PID 参数（默认值）
#define HEADING_PID_KP      1.60f
#define HEADING_PID_KI      0.02f
#define HEADING_PID_KD      0.24f
#define HEADING_PID_INT_LIM 50.0f
#define HEADING_PID_OUT_LIM 50.0f

// 速度 PID 参数
#define SPEED_PID_KP        700.0f   // 根据视频调参结果
#define SPEED_PID_KI        0.5f
#define SPEED_PID_KD        0.0f
#define SPEED_PID_INT_LIM   1000.0f  // 积分限幅放大
#define SPEED_PID_OUT_LIM   100.0f   // PWM 最大100%

// 前馈系数（需要空载测试确定）
#define SPEED_FEEDFORWARD_K  0.5f    // 占空比/速度 的系数

// 反电动势系数（需要空载测试确定）
#define SPEED_EMF_K          0.3f    // 反电动势/速度 的系数

// 控制方向（修正电机方向）
#define HEADING_CORR_SIGN   1

#endif
