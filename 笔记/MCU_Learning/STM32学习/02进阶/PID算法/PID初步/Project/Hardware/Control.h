#ifndef __CONTROL_H
#define __CONTROL_H
#include "stm32f10x.h"
#include "MPU6050.h"
#include "Encoder_Timer.h"
#include "PID.h"
#include "FuzzyPID.h"

// 测试模式
typedef enum {
    TEST_MODE_NORMAL = 0,     // 正常模式（航向+速度双环）
    TEST_MODE_SPEED_ONLY      // 速度环单独测试（手持调参用）
} TestMode_t;

// 控制系统状态
typedef struct {
    // 传感器数据
    MPU6050_Data_t mpu;
    Encoder_Data_t encoder;
    
    // PID 控制器
    PID_t headingPID;              // 传统航向 PID（备用）
    FuzzyPIDController fuzzyHeadingPID;  // 模糊PID航向控制器（主用）
    PID_t leftSpeedPID;            // 左轮速度 PID（内环）
    PID_t rightSpeedPID;           // 右轮速度 PID（内环）
    
    // 控制目标
    float targetYaw;      // 目标航向角
    int16_t targetSpeed;  // 目标速度（计数/周期）
    
    // 控制输出
    float headingCorr;    // 航向修正量
    int16_t leftPWM;      // 左轮 PWM
    int16_t rightPWM;     // 右轮 PWM
    
    // 状态标志
    uint8_t isRunning;    // 是否在运行
    uint32_t tickCount;   // 时钟计数
    TestMode_t testMode;  // 测试模式
    
    // 航向误差（用于 VOFA+ 显示）
    float yawErr;
} ControlSystem_t;

// 初始化控制系统
// skipMPU: 1=跳过MPU6050初始化（速度环测试模式），0=正常初始化
void Control_Init(ControlSystem_t *sys, uint8_t skipMPU);

// 启动/停止控制
void Control_Start(ControlSystem_t *sys);
void Control_Stop(ControlSystem_t *sys);

// 设置测试模式
void Control_SetTestMode(ControlSystem_t *sys, TestMode_t mode);

// 设置目标速度
void Control_SetTargetSpeed(ControlSystem_t *sys, int16_t speed);

// 航向锁定（记录当前航向为目标）
void Control_LockHeading(ControlSystem_t *sys);

// 外环控制（航向模糊PID）- 20ms 调用
void Control_OuterLoop(ControlSystem_t *sys);

// 内环控制（速度 PI）- 10ms 调用
void Control_InnerLoop(ControlSystem_t *sys);

// 数据融合（陀螺仪 + 编码器差分）
void Control_FuseHeading(ControlSystem_t *sys, float dt);

// VOFA+ 数据发送 - 50ms 调用
void Control_SendVOFA(ControlSystem_t *sys);

// VOFA+ 参数解析
void Control_ParseVOFA(ControlSystem_t *sys);

// 定时器中断回调（1ms）
void Control_Tick(ControlSystem_t *sys);

// 获取当前航向误差
float Control_GetYawError(ControlSystem_t *sys);

#endif
