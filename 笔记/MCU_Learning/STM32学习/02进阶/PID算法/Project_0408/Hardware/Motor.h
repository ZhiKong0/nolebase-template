/**
 * @file Motor.h
 * @brief 电机驱动配置与API
 * @description 统一电机驱动的配置和接口
 */

#ifndef __MOTOR_H
#define __MOTOR_H

#include "stm32f10x.h"
#include "HardwareConfig.h"

/*===========================================================================
 * 电机配置结构体
 *========================================================================*/

/**
 * @brief 电机配置
 */
typedef struct {
    // PWM参数
    uint32_t pwmPeriod;          // PWM周期
    uint16_t maxPulse;          // 最大脉宽
    uint16_t minPulse;          // 最小脉宽
    
    // 通道配置
    TIM_TypeDef* timLeft;       // 左轮定时器
    uint8_t chLeft;            // 左轮通道
    TIM_TypeDef* timRight;     // 右轮定时器
    uint8_t chRight;           // 右轮通道
    
    // 引脚配置
    hw_gpio_config_t pinLeftA;
    hw_gpio_config_t pinLeftB;
    hw_gpio_config_t pinRightA;
    hw_gpio_config_t pinRightB;
    
    // 功能配置
    hw_bool_t enable;           // 使能标志
} MotorConfig_t;

/**
 * @brief 电机状态
 */
typedef struct {
    int16_t leftPwm;            // 左轮PWM
    int16_t rightPwm;           // 右轮PWM
    hw_bool_t enabled;          // 电机使能状态
    hw_bool_t running;         // 电机运行状态
} MotorState_t;

/*===========================================================================
 * 公共API
 *========================================================================*/

/**
 * @brief 电机初始化（使用默认配置）
 */
void Motor_Init(void);

/**
 * @brief 电机初始化（使用自定义配置）
 * @param config 指向MotorConfig_t的指针
 */
void Motor_InitWithConfig(MotorConfig_t *config);

/**
 * @brief 设置左轮PWM
 * @param pwm PWM值（-4095到4095）
 */
void Motor_SetLeftPwm(int16_t pwm);

/**
 * @brief 设置右轮PWM
 * @param pwm PWM值（-4095到4095）
 */
void Motor_SetRightPwm(int16_t pwm);

/**
 * @brief 设置差速（左右轮PWM不同）
 * @param left 左轮PWM
 * @param right 右轮PWM
 */
void Motor_SetDiffSpeed(int16_t left, int16_t right);

/**
 * @brief 使能/禁用电机
 * @param enable HW_TRUE使能，HW_FALSE禁用
 */
void Motor_Enable(void);
void Motor_Disable(void);

/**
 * @brief 电机停止（输出置零）
 */
void Motor_Stop(void);

/**
 * @brief 获取电机状态
 * @return 指向MotorState_t的指针
 */
MotorState_t* Motor_GetState(void);

/**
 * @brief 电机紧急停止（禁用并清零）
 */
void Motor_EmergencyStop(void);

#endif /* __MOTOR_H */
