#ifndef __MOTOR_H
#define __MOTOR_H
#include "stm32f10x.h"

// 电机方向定义
typedef enum {
    MOTOR_DIR_FWD = 0,   // 前进
    MOTOR_DIR_REV = 1    // 后退
} Motor_Dir_t;

// 初始化
void Motor_Init(void);

// 设置单个电机速度和方向
// speed: 0~100 (PWM占空比百分比)
// dir: 方向
void Motor_SetLeft(int16_t speed, Motor_Dir_t dir);
void Motor_SetRight(int16_t speed, Motor_Dir_t dir);

// 差速控制（用于PID输出）
// leftSpeed, rightSpeed: -100~100，负数为反转
void Motor_SetDiffSpeed(int16_t leftSpeed, int16_t rightSpeed);

// 停止所有电机
void Motor_Stop(void);

// 使能/失能驱动
void Motor_Enable(void);
void Motor_Disable(void);

#endif
