/**
 * @file Encoder_Timer.h
 * @brief 编码器驱动配置与API
 * @description 统一编码器驱动的配置和接口
 */

#ifndef __ENCODER_TIMER_H
#define __ENCODER_TIMER_H

#include "stm32f10x.h"
#include "HardwareConfig.h"

/*===========================================================================
 * 编码器配置结构体
 *========================================================================*/

/**
 * @brief 编码器配置
 */
typedef struct {
    // 定时器配置
    TIM_TypeDef* timLeft;        // 左编码器定时器
    TIM_TypeDef* timRight;      // 右编码器定时器
    
    // 编码器模式
    uint8_t encoderMode;         // TIM_Encoder_Mode_TI1/TI2/TI12
    
    // 滤波器（ICR分频）
    uint16_t filter;            // 滤波器值
    
    // 计数参数
    int16_t maxCount;           // 最大计数值（用于计算速度）
    int16_t revolutionPulses;   // 每轮脉冲数
    
    // 引脚配置
    hw_gpio_config_t leftPinA;
    hw_gpio_config_t leftPinB;
    hw_gpio_config_t rightPinA;
    hw_gpio_config_t rightPinB;
} EncoderConfig_t;

/**
 * @brief 编码器状态
 */
typedef struct {
    int16_t leftCount;         // 左轮计数器
    int16_t rightCount;        // 右轮计数器
    int16_t leftDelta;         // 左轮本次增量
    int16_t rightDelta;        // 右轮本次增量
    int16_t leftSpeed;         // 左轮速度（脉冲/控制周期）
    int16_t rightSpeed;        // 右轮速度
    int16_t leftDeltaClamped; // 左轮限幅后的增量
    int16_t rightDeltaClamped; // 右轮限幅后的增量
    
    // 方向配置
    int8_t leftDir;            // 左轮方向（1或-1）
    int8_t rightDir;           // 右轮方向
    
    hw_bool_t initialized;      // 初始化标志
} EncoderData_t;

/*===========================================================================
 * 公共API
 *========================================================================*/

/**
 * @brief 编码器初始化（使用默认配置）
 */
void Encoder_Timer_Init(void);

/**
 * @brief 编码器初始化（使用自定义配置）
 * @param config 指向EncoderConfig_t的指针
 */
void Encoder_Timer_InitWithConfig(EncoderConfig_t *config);

/**
 * @brief 读取编码器计数
 * @param leftCount 左轮计数（输出）
 * @param rightCount 右轮计数（输出）
 */
void Encoder_ReadCount(int16_t *leftCount, int16_t *rightCount);

/**
 * @brief 读取编码器增量（两次调用之间的差值）
 * @param leftDelta 左轮增量（输出）
 * @param rightDelta 右轮增量（输出）
 */
void Encoder_ReadDelta(int16_t *leftDelta, int16_t *rightDelta);

/**
 * @brief 更新速度计算（应在固定周期调用）
 * @param periodMs 控制周期（毫秒）
 */
void Encoder_UpdateSpeed(int16_t periodMs);

/**
 * @brief 获取编码器数据
 * @return 指向EncoderData_t的指针
 */
EncoderData_t* Encoder_GetData(void);

/**
 * @brief 设置左轮方向
 * @param dir 方向（1或-1）
 */
void Encoder_SetLeftSign(int8_t dir);

/**
 * @brief 设置右轮方向
 * @param dir 方向（1或-1）
 */
void Encoder_SetRightSign(int8_t dir);

/**
 * @brief 复位编码器
 */
void Encoder_Reset(void);

/**
 * @brief 设置编码器计数最大值
 * @param maxCount 最大计数值
 */
void Encoder_SetMaxCount(int16_t maxCount);

#endif /* __ENCODER_TIMER_H */
