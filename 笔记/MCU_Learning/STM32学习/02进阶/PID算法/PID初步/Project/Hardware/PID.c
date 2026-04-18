#include "stm32f10x.h"
#include "PID.h"
#include <math.h>

// 初始化
void PID_Init(PID_t *pid, float Kp, float Ki, float Kd, float integralLimit, float outputLimit) {
    pid->Kp = Kp;
    pid->Ki = Ki;
    pid->Kd = Kd;
    pid->integral = 0.0f;
    pid->prevError = 0.0f;
    pid->integralLimit = integralLimit;
    pid->outputLimit = outputLimit;
    
    // 前馈和力矩控制默认禁用
    pid->Kff = 0.0f;
    pid->enableFeedforward = 0;
    pid->Kemf = 0.0f;
    pid->enableTorqueControl = 0;
}

// 重置
void PID_Reset(PID_t *pid) {
    pid->integral = 0.0f;
    pid->prevError = 0.0f;
}

// 设置参数
void PID_SetParams(PID_t *pid, float Kp, float Ki, float Kd) {
    pid->Kp = Kp;
    pid->Ki = Ki;
    pid->Kd = Kd;
    PID_Reset(pid);  // 参数改变时重置
}

// 设置前馈参数
void PID_SetFeedforward(PID_t *pid, float Kff, uint8_t enable) {
    pid->Kff = Kff;
    pid->enableFeedforward = enable;
}

// 设置开环力矩参数
void PID_SetTorqueControl(PID_t *pid, float Kemf, uint8_t enable) {
    pid->Kemf = Kemf;
    pid->enableTorqueControl = enable;
}

// 计算前馈值（基于目标速度）
// 思想：提前计算达到目标速度所需的电压，不用等误差产生
float PID_CalculateFeedforward(PID_t *pid, float targetSpeed) {
    if (!pid->enableFeedforward) {
        return 0.0f;
    }
    // 前馈值 = 目标速度 * 前馈系数
    // 这个系数需要通过空载测试确定：记录各速度下所需的占空比
    return targetSpeed * pid->Kff;
}

// 计算开环力矩补偿（基于当前速度的反电动势）
// 思想：电压 = 反电动势 + 电流*电阻，控制电流=控制力矩
// 反电动势与速度成正比，通过空载测试确定系数
float PID_CalculateTorqueCompensation(PID_t *pid, float currentSpeed) {
    if (!pid->enableTorqueControl) {
        return 0.0f;
    }
    // 反电动势补偿 = 当前速度 * 反电动势系数
    // 这样PID输出就主要控制力矩（电流），而不是总电压
    return currentSpeed * pid->Kemf;
}

// PID 计算（基本PID）
float PID_Calculate(PID_t *pid, float error, float dt) {
    float output;
    
    // P 项
    float pTerm = pid->Kp * error;
    
    // I 项（带限幅）
    pid->integral += error * dt;
    if (pid->integral > pid->integralLimit) {
        pid->integral = pid->integralLimit;
    } else if (pid->integral < -pid->integralLimit) {
        pid->integral = -pid->integralLimit;
    }
    float iTerm = pid->Ki * pid->integral;
    
    // D 项
    float dTerm = 0.0f;
    if (dt > 0.0f) {
        dTerm = pid->Kd * (error - pid->prevError) / dt;
    }
    pid->prevError = error;
    
    // 输出
    output = pTerm + iTerm + dTerm;
    
    // 输出限幅
    if (output > pid->outputLimit) {
        output = pid->outputLimit;
    } else if (output < -pid->outputLimit) {
        output = -pid->outputLimit;
    }
    
    return output;
}

// PID 计算（带前馈）
float PID_CalculateWithFeedforward(PID_t *pid, float error, float dt, float feedforwardValue) {
    float pidOutput = PID_Calculate(pid, error, dt);
    float output = pidOutput + feedforwardValue;
    
    // 输出限幅
    if (output > pid->outputLimit) {
        output = pid->outputLimit;
    } else if (output < -pid->outputLimit) {
        output = -pid->outputLimit;
    }
    
    return output;
}

// PID 计算（带开环力矩）
float PID_CalculateWithTorque(PID_t *pid, float error, float dt, float currentSpeed) {
    float pidOutput = PID_Calculate(pid, error, dt);
    float torqueComp = PID_CalculateTorqueCompensation(pid, currentSpeed);
    float output = pidOutput + torqueComp;
    
    // 输出限幅
    if (output > pid->outputLimit) {
        output = pid->outputLimit;
    } else if (output < -pid->outputLimit) {
        output = -pid->outputLimit;
    }
    
    return output;
}

// PID 计算（完整版：PID + 前馈 + 开环力矩）
// 这是视频中最完整的控制方式
float PID_CalculateFull(PID_t *pid, float error, float dt, float targetSpeed, float currentSpeed) {
    float pidOutput = PID_Calculate(pid, error, dt);
    float feedforward = PID_CalculateFeedforward(pid, targetSpeed);
    float torqueComp = PID_CalculateTorqueCompensation(pid, currentSpeed);
    
    // 总输出 = PID输出 + 前馈补偿 + 反电动势补偿
    float output = pidOutput + feedforward + torqueComp;
    
    // 输出限幅
    if (output > pid->outputLimit) {
        output = pid->outputLimit;
    } else if (output < -pid->outputLimit) {
        output = -pid->outputLimit;
    }
    
    return output;
}
