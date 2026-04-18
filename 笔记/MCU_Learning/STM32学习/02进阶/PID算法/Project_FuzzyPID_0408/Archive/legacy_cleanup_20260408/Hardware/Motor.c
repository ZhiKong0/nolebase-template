/**
 * @file Motor.c
 * @brief 电机驱动实现
 * @description 统一管理左右轮电机PWM输出
 */

#include "Motor.h"
#include "stm32f10x_gpio.h"
#include "stm32f10x_tim.h"

/*===========================================================================
 * 私有定义
 *========================================================================*/

// 默认PWM配置
#define MOTOR_PWM_PERIOD         1440        // PWM周期（TIM时钟/期望频率）
#define MOTOR_PWM_MAX           1440        // 最大脉宽

// 左轮引脚定义（可根据实际硬件调整）
#define MOTOR_LEFT_TIM          TIM3
#define MOTOR_LEFT_CH            TIM_Channel_1
#define MOTOR_LEFT_PIN_A         GPIO_Pin_6    // PA6 - TIM3_CH1
#define MOTOR_LEFT_PIN_B         GPIO_Pin_7    // PA7 - TIM3_CH2
#define MOTOR_LEFT_PORT_A        GPIOA
#define MOTOR_LEFT_PORT_B        GPIOA

// 右轮引脚定义
#define MOTOR_RIGHT_TIM          TIM3
#define MOTOR_RIGHT_CH           TIM_Channel_2
#define MOTOR_RIGHT_PIN_A        GPIO_Pin_8    // PB0 - TIM3_CH3
#define MOTOR_RIGHT_PIN_B        GPIO_Pin_9    // PB1 - TIM3_CH4
#define MOTOR_RIGHT_PORT_A       GPIOB
#define MOTOR_RIGHT_PORT_B       GPIOB

// 使能引脚
#define MOTOR_EN_PIN             GPIO_Pin_12   // 根据实际调整
#define MOTOR_EN_PORT            GPIOB

/*===========================================================================
 * 私有变量
 *========================================================================*/

static MotorConfig_t g_motorConfig;
static MotorState_t g_motorState;

/*===========================================================================
 * 私有函数
 *========================================================================*/

/**
 * @brief 设置PWM脉宽
 */
static void motor_set_pwm(TIM_TypeDef* tim, uint8_t channel, uint16_t pulse) {
    TIM_OCInitTypeDef TIM_OCInitStructure;
    
    TIM_OCInitStructure.TIM_OCMode = TIM_OCMode_PWM2;
    TIM_OCInitStructure.TIM_OutputState = TIM_OutputState_Enable;
    TIM_OCInitStructure.TIM_Pulse = pulse;
    TIM_OCInitStructure.TIM_OCPolarity = TIM_OCPolarity_High;
    
    switch(channel) {
        case TIM_Channel_1:
            TIM_OC1Init(tim, &TIM_OCInitStructure);
            TIM_OC1PreloadConfig(tim, TIM_OCPreload_Enable);
            break;
        case TIM_Channel_2:
            TIM_OC2Init(tim, &TIM_OCInitStructure);
            TIM_OC2PreloadConfig(tim, TIM_OCPreload_Enable);
            break;
        case TIM_Channel_3:
            TIM_OC3Init(tim, &TIM_OCInitStructure);
            TIM_OC3PreloadConfig(tim, TIM_OCPreload_Enable);
            break;
        case TIM_Channel_4:
            TIM_OC4Init(tim, &TIM_OCInitStructure);
            TIM_OC4PreloadConfig(tim, TIM_OCPreload_Enable);
            break;
    }
}

/**
 * @brief 电机引脚初始化
 */
static void motor_gpio_init(void) {
    GPIO_InitTypeDef GPIO_InitStructure;
    
    // 使能GPIO时钟
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_GPIOB, ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM3, ENABLE);
    
    // 左轮PWM引脚
    GPIO_InitStructure.GPIO_Pin = MOTOR_LEFT_PIN_A | MOTOR_LEFT_PIN_B;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(MOTOR_LEFT_PORT_A, &GPIO_InitStructure);
    
    // 右轮PWM引脚
    GPIO_InitStructure.GPIO_Pin = MOTOR_RIGHT_PIN_A | MOTOR_RIGHT_PIN_B;
    GPIO_Init(MOTOR_RIGHT_PORT_A, &GPIO_InitStructure);
    
    // 使能引脚（如果需要）
    // GPIO_InitStructure.GPIO_Pin = MOTOR_EN_PIN;
    // GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;
    // GPIO_Init(MOTOR_EN_PORT, &GPIO_InitStructure);
}

/**
 * @brief 定时器初始化
 */
static void motor_tim_init(void) {
    TIM_TimeBaseInitTypeDef TIM_TimeBaseStructure;
    
    // 定时器基础配置
    TIM_TimeBaseStructure.TIM_Period = MOTOR_PWM_PERIOD - 1;
    TIM_TimeBaseStructure.TIM_Prescaler = 0;
    TIM_TimeBaseStructure.TIM_ClockDivision = TIM_CKD_DIV1;
    TIM_TimeBaseStructure.TIM_CounterMode = TIM_CounterMode_Up;
    TIM_TimeBaseStructure.TIM_RepetitionCounter = 0;
    
    TIM_TimeBaseInit(MOTOR_LEFT_TIM, &TIM_TimeBaseStructure);
    TIM_TimeBaseInit(MOTOR_RIGHT_TIM, &TIM_TimeBaseStructure);
    
    // 使能ARR预加载
    TIM_ARRPreloadConfig(MOTOR_LEFT_TIM, ENABLE);
    
    // 使能定时器
    TIM_Cmd(MOTOR_LEFT_TIM, ENABLE);
    TIM_Cmd(MOTOR_RIGHT_TIM, ENABLE);
}

/*===========================================================================
 * 公共API实现
 *========================================================================*/

void Motor_Init(void) {
    // 初始化默认配置
    g_motorConfig.pwmPeriod = MOTOR_PWM_PERIOD;
    g_motorConfig.maxPulse = MOTOR_PWM_MAX;
    g_motorConfig.minPulse = 0;
    g_motorConfig.timLeft = MOTOR_LEFT_TIM;
    g_motorConfig.chLeft = MOTOR_LEFT_CH;
    g_motorConfig.timRight = MOTOR_RIGHT_TIM;
    g_motorConfig.chRight = MOTOR_RIGHT_CH;
    g_motorConfig.enable = HW_TRUE;
    
    // 初始化状态
    g_motorState.leftPwm = 0;
    g_motorState.rightPwm = 0;
    g_motorState.enabled = HW_FALSE;
    g_motorState.running = HW_FALSE;
    
    // 初始化硬件
    motor_gpio_init();
    motor_tim_init();
    
    // 初始PWM为0
    Motor_Stop();
}

void Motor_InitWithConfig(MotorConfig_t *config) {
    if (!config) {
        Motor_Init();
        return;
    }
    
    // 复制配置
    g_motorConfig = *config;
    
    // 初始化硬件
    motor_gpio_init();
    motor_tim_init();
    
    Motor_Stop();
}

void Motor_SetLeftPwm(int16_t pwm) {
    uint16_t pulse;
    
    // 限幅
    if (pwm > (int16_t)g_motorConfig.maxPulse) pwm = g_motorConfig.maxPulse;
    if (pwm < -(int16_t)g_motorConfig.maxPulse) pwm = -(int16_t)g_motorConfig.maxPulse;
    
    g_motorState.leftPwm = pwm;
    
    // 转换为脉宽
    if (pwm >= 0) {
        pulse = (uint16_t)pwm;
    } else {
        pulse = 0;
    }
    
    motor_set_pwm(g_motorConfig.timLeft, g_motorConfig.chLeft, pulse);
}

void Motor_SetRightPwm(int16_t pwm) {
    uint16_t pulse;
    
    // 限幅
    if (pwm > (int16_t)g_motorConfig.maxPulse) pwm = g_motorConfig.maxPulse;
    if (pwm < -(int16_t)g_motorConfig.maxPulse) pwm = -(int16_t)g_motorConfig.maxPulse;
    
    g_motorState.rightPwm = pwm;
    
    // 转换为脉宽
    if (pwm >= 0) {
        pulse = (uint16_t)pwm;
    } else {
        pulse = 0;
    }
    
    motor_set_pwm(g_motorConfig.timRight, g_motorConfig.chRight, pulse);
}

void Motor_SetDiffSpeed(int16_t left, int16_t right) {
    Motor_SetLeftPwm(left);
    Motor_SetRightPwm(right);
}

void Motor_Enable(void) {
    g_motorState.enabled = HW_TRUE;
    // 如果有使能引脚，在此设置
    // GPIO_SetBits(MOTOR_EN_PORT, MOTOR_EN_PIN);
}

void Motor_Disable(void) {
    g_motorState.enabled = HW_FALSE;
    // 如果有使能引脚，在此清除
    // GPIO_ResetBits(MOTOR_EN_PORT, MOTOR_EN_PIN);
}

void Motor_Stop(void) {
    g_motorState.leftPwm = 0;
    g_motorState.rightPwm = 0;
    g_motorState.running = HW_FALSE;
    
    motor_set_pwm(g_motorConfig.timLeft, g_motorConfig.chLeft, 0);
    motor_set_pwm(g_motorConfig.timRight, g_motorConfig.chRight, 0);
}

MotorState_t* Motor_GetState(void) {
    return &g_motorState;
}

void Motor_EmergencyStop(void) {
    Motor_Stop();
    Motor_Disable();
}
