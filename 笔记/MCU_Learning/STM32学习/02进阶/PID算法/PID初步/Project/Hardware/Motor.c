#include "stm32f10x.h"
#include "Motor.h"

// 引脚定义（根据接线表）
// TB6612: STBY=PB0, PWMA=PA8, AIN1=PA4, AIN2=PA5, PWMB=PA9, BIN1=PB1, BIN2=PB10
#define MOTOR_STBY_PIN      GPIO_Pin_0
#define MOTOR_STBY_PORT     GPIOB

#define MOTOR_AIN1_PIN      GPIO_Pin_4
#define MOTOR_AIN2_PIN      GPIO_Pin_5
#define MOTOR_AIN_PORT      GPIOA

#define MOTOR_BIN1_PIN      GPIO_Pin_1
#define MOTOR_BIN2_PIN      GPIO_Pin_10
#define MOTOR_BIN_PORT      GPIOB

// PWM 使用 TIM1 CH1(PA8) 和 CH2(PA9)
#define MOTOR_PWM_PERIOD    100  // ARR值，对应1kHz PWM

// 初始化 GPIO 和 PWM
void Motor_Init(void) {
    // 开启时钟
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_TIM1, ENABLE);
    
    // 配置控制引脚（推挽输出）
    GPIO_InitTypeDef GPIO_InitStructure;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    
    // STBY
    GPIO_InitStructure.GPIO_Pin = MOTOR_STBY_PIN;
    GPIO_Init(MOTOR_STBY_PORT, &GPIO_InitStructure);
    GPIO_ResetBits(MOTOR_STBY_PORT, MOTOR_STBY_PIN);  // 初始禁用
    
    // AIN1, AIN2
    GPIO_InitStructure.GPIO_Pin = MOTOR_AIN1_PIN | MOTOR_AIN2_PIN;
    GPIO_Init(MOTOR_AIN_PORT, &GPIO_InitStructure);
    GPIO_ResetBits(MOTOR_AIN_PORT, MOTOR_AIN1_PIN | MOTOR_AIN2_PIN);
    
    // BIN1, BIN2
    GPIO_InitStructure.GPIO_Pin = MOTOR_BIN1_PIN | MOTOR_BIN2_PIN;
    GPIO_Init(MOTOR_BIN_PORT, &GPIO_InitStructure);
    GPIO_ResetBits(MOTOR_BIN_PORT, MOTOR_BIN1_PIN | MOTOR_BIN2_PIN);
    
    // 配置 PWM 引脚（复用推挽）
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_8 | GPIO_Pin_9;  // PA8, PA9
    GPIO_Init(GPIOA, &GPIO_InitStructure);
    
    // TIM1 配置
    TIM_TimeBaseInitTypeDef TIM_TimeBaseInitStructure;
    TIM_TimeBaseInitStructure.TIM_ClockDivision = TIM_CKD_DIV1;
    TIM_TimeBaseInitStructure.TIM_CounterMode = TIM_CounterMode_Up;
    TIM_TimeBaseInitStructure.TIM_Period = MOTOR_PWM_PERIOD - 1;  // ARR
    TIM_TimeBaseInitStructure.TIM_Prescaler = 720 - 1;  // PSC: 72MHz/720=100kHz, 100kHz/100=1kHz
    TIM_TimeBaseInitStructure.TIM_RepetitionCounter = 0;
    TIM_TimeBaseInit(TIM1, &TIM_TimeBaseInitStructure);
    
    // PWM 模式配置
    TIM_OCInitTypeDef TIM_OCInitStructure;
    TIM_OCStructInit(&TIM_OCInitStructure);
    TIM_OCInitStructure.TIM_OCMode = TIM_OCMode_PWM1;
    TIM_OCInitStructure.TIM_OCPolarity = TIM_OCPolarity_High;
    TIM_OCInitStructure.TIM_OutputState = TIM_OutputState_Enable;
    TIM_OCInitStructure.TIM_Pulse = 0;  // 初始占空比 0
    
    // CH1 (左电机)
    TIM_OC1Init(TIM1, &TIM_OCInitStructure);
    // CH2 (右电机)
    TIM_OC2Init(TIM1, &TIM_OCInitStructure);
    
    // 高级定时器需要使能主输出
    TIM_CtrlPWMOutputs(TIM1, ENABLE);
    
    // 使能定时器
    TIM_Cmd(TIM1, ENABLE);
}

// 设置左电机
void Motor_SetLeft(int16_t speed, Motor_Dir_t dir) {
    // 限幅
    if (speed > 100) speed = 100;
    if (speed < 0) speed = 0;
    
    // 方向控制
    if (dir == MOTOR_DIR_FWD) {
        GPIO_SetBits(MOTOR_AIN_PORT, MOTOR_AIN1_PIN);
        GPIO_ResetBits(MOTOR_AIN_PORT, MOTOR_AIN2_PIN);
    } else {
        GPIO_ResetBits(MOTOR_AIN_PORT, MOTOR_AIN1_PIN);
        GPIO_SetBits(MOTOR_AIN_PORT, MOTOR_AIN2_PIN);
    }
    
    // PWM 占空比
    TIM_SetCompare1(TIM1, speed);
}

// 设置右电机
void Motor_SetRight(int16_t speed, Motor_Dir_t dir) {
    if (speed > 100) speed = 100;
    if (speed < 0) speed = 0;
    
    if (dir == MOTOR_DIR_FWD) {
        GPIO_SetBits(MOTOR_BIN_PORT, MOTOR_BIN1_PIN);
        GPIO_ResetBits(MOTOR_BIN_PORT, MOTOR_BIN2_PIN);
    } else {
        GPIO_ResetBits(MOTOR_BIN_PORT, MOTOR_BIN1_PIN);
        GPIO_SetBits(MOTOR_BIN_PORT, MOTOR_BIN2_PIN);
    }
    
    TIM_SetCompare2(TIM1, speed);
}

// 差速控制
void Motor_SetDiffSpeed(int16_t leftSpeed, int16_t rightSpeed) {
    // 左电机
    if (leftSpeed >= 0) {
        Motor_SetLeft(leftSpeed, MOTOR_DIR_FWD);
    } else {
        Motor_SetLeft(-leftSpeed, MOTOR_DIR_REV);
    }
    
    // 右电机
    if (rightSpeed >= 0) {
        Motor_SetRight(rightSpeed, MOTOR_DIR_FWD);
    } else {
        Motor_SetRight(-rightSpeed, MOTOR_DIR_REV);
    }
}

// 停止
void Motor_Stop(void) {
    GPIO_ResetBits(MOTOR_AIN_PORT, MOTOR_AIN1_PIN | MOTOR_AIN2_PIN);
    GPIO_ResetBits(MOTOR_BIN_PORT, MOTOR_BIN1_PIN | MOTOR_BIN2_PIN);
    TIM_SetCompare1(TIM1, 0);
    TIM_SetCompare2(TIM1, 0);
}

// 使能驱动
void Motor_Enable(void) {
    GPIO_SetBits(MOTOR_STBY_PORT, MOTOR_STBY_PIN);
}

// 禁用驱动
void Motor_Disable(void) {
    GPIO_ResetBits(MOTOR_STBY_PORT, MOTOR_STBY_PIN);
}
