/*
 * 传感器融合接口说明:
 * 1. BNO085 相关接口负责输出姿态、角速度和初始化状态。
 * 2. 8 路循迹传感器接口负责输出位图、有效个数和加权位置。
 * 3. main.c 只依赖这里的抽象结果，不直接碰底层 GPIO/I2C 细节。
 */
#ifndef __SENSOR_FUSION_H
#define __SENSOR_FUSION_H

#include "stm32f10x.h"

typedef struct {
    /* 滤波后的加速度和角速度。 */
    float accelXf, accelYf, accelZf;
    float gyroXf, gyroYf, gyroZf;
    /* yaw / prevYaw / yawRate 是控制层最常用的三个姿态量。 */
    float yaw, prevYaw, yawRate;
    float pitch, roll;
    /* 四元数保留原始姿态表示，方便后续扩展。 */
    float q0, q1, q2, q3;
    uint8_t ahrsInited;
    uint8_t yawSampleUpdated;
    uint8_t yawRateValid;
} IMU_Data_t;

typedef struct {
    /* bits 按从左到右 8 位编码，count 为本次点亮数量。 */
    uint8_t bits;
    uint8_t count;
    uint8_t lineDetected;
    /* position 是按权重平均后的线位置，0 附近偏左，正值偏右。 */
    float position;
} LineSensor_Data_t;

/* 初始化 BNO085 软件 I2C 和内部状态机。 */
void BNO085_Init(void);
/* 读取一轮 IMU 数据，成功返回 1。 */
uint8_t BNO085_ReadAll(IMU_Data_t *data);
/* 重新定义当前车头为 0 度参考。 */
void BNO085_ResetAttitude(IMU_Data_t *data);
/* 根据新旧采样计算连续 yaw 和 yawRate。 */
void BNO085_UpdateYaw(IMU_Data_t *data, float dt);
/* 求带 180/-180 包络的最短偏航误差。 */
float BNO085_GetYawError(float target, float current);
/* 查询 IMU 是否完成初始化。 */
uint8_t BNO085_IsReady(void);
/* 查询 IMU 初始化阶段，主要给 OLED/日志使用。 */
uint8_t BNO085_GetInitStage(void);
uint8_t BNO085_GetI2CAddr(void);
/* 仅在停车态调用的恢复服务，避免运行中重初始化把控制循环卡死。 */
void BNO085_Service(void);

/* 初始化 8 路循迹传感器 GPIO。 */
void LineSensor_Init(void);
/* 读取当前 8 路循迹状态并计算加权位置。 */
void LineSensor_Read(LineSensor_Data_t *data);

#endif
