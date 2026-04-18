/**
 * @file HardwareConfig.h
 * @brief 硬件驱动统一配置头文件
 * @description 所有硬件驱动的公共配置类型和接口定义
 */

#ifndef __HARDWARE_CONFIG_H
#define __HARDWARE_CONFIG_H

#include "stm32f10x.h"

/*===========================================================================
 * 公共类型定义
 *========================================================================*/

/**
 * @brief 布尔类型
 */
typedef uint8_t hw_bool_t;
#define HW_TRUE  1
#define HW_FALSE 0

/**
 * @brief 硬件状态码
 */
typedef enum {
    HW_OK = 0,
    HW_ERROR = 1,
    HW_BUSY = 2,
    HW_TIMEOUT = 3,
    HW_NOT_INIT = 4
} hw_status_t;

/*===========================================================================
 * GPIO配置
 *========================================================================*/

/**
 * @brief GPIO引脚配置
 */
typedef struct {
    GPIO_TypeDef* port;
    uint16_t pin;
    uint8_t mode;       // GPIO_Mode_IPU, GPIO_Mode_Out_PP, etc.
    uint8_t speed;      // GPIO_Speed_50MHz, etc.
} hw_gpio_config_t;

/*===========================================================================
 * PWM配置
 *========================================================================*/

/**
 * @brief PWM通道配置
 */
typedef struct {
    TIM_TypeDef* tim;           // 定时器
    uint8_t channel;           // 通道1-4
    uint32_t period;           // 周期值
    uint32_t pulse;            // 脉宽值
    hw_bool_t enable;          // 使能
} hw_pwm_config_t;

/*===========================================================================
 * 串口配置
 *========================================================================*/

/**
 * @brief 串口配置
 */
typedef struct {
    USART_TypeDef* usart;       // USART外设
    uint32_t baudrate;          // 波特率
    uint8_t databits;           // 数据位
    uint8_t stopbits;           // 停止位
    uint8_t parity;             // 校验位
} hw_uart_config_t;

/*===========================================================================
 * I2C配置
 *========================================================================*/

/**
 * @brief I2C配置
 */
typedef struct {
    I2C_TypeDef* i2c;           // I2C外设
    uint32_t clockSpeed;        // 时钟速度
    uint8_t address;            // 从机地址
} hw_i2c_config_t;

/*===========================================================================
 * 公共API宏定义
 *========================================================================*/

/**
 * @brief 硬件初始化函数指针类型
 */
typedef hw_status_t (*hw_init_func_t)(void);

/**
 * @brief 硬件使能函数指针类型
 */
typedef void (*hw_enable_func_t)(hw_bool_t enable);

/*===========================================================================
 * 版本信息
 *========================================================================*/

#define HARDWARE_VERSION_MAJOR 1
#define HARDWARE_VERSION_MINOR 0
#define HARDWARE_VERSION_PATCH 0

/**
 * @brief 获取硬件驱动版本
 */
void Hardware_GetVersion(uint8_t *major, uint8_t *minor, uint8_t *patch);

/**
 * @brief 硬件驱动初始化（统一入口）
 */
hw_status_t Hardware_InitAll(void);

#endif /* __HARDWARE_CONFIG_H */
