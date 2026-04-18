#include "stm32f10x.h"
#include "Motor.h"

#define MOTOR_STBY_PIN      GPIO_Pin_0
#define MOTOR_STBY_PORT     GPIOB

#define MOTOR_AIN1_PIN      GPIO_Pin_4
#define MOTOR_AIN2_PIN      GPIO_Pin_5
#define MOTOR_AIN_PORT      GPIOA

#define MOTOR_BIN1_PIN      GPIO_Pin_1
#define MOTOR_BIN2_PIN      GPIO_Pin_10
#define MOTOR_BIN_PORT      GPIOB

#define MOTOR_PWM_PERIOD    100

void Motor_Init(void) {
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_TIM1, ENABLE);

    GPIO_InitTypeDef g;
    g.GPIO_Mode = GPIO_Mode_Out_PP;
    g.GPIO_Speed = GPIO_Speed_50MHz;

    g.GPIO_Pin = MOTOR_STBY_PIN;
    GPIO_Init(MOTOR_STBY_PORT, &g);
    GPIO_ResetBits(MOTOR_STBY_PORT, MOTOR_STBY_PIN);

    g.GPIO_Pin = MOTOR_AIN1_PIN | MOTOR_AIN2_PIN;
    GPIO_Init(MOTOR_AIN_PORT, &g);
    GPIO_ResetBits(MOTOR_AIN_PORT, MOTOR_AIN1_PIN | MOTOR_AIN2_PIN);

    g.GPIO_Pin = MOTOR_BIN1_PIN | MOTOR_BIN2_PIN;
    GPIO_Init(MOTOR_BIN_PORT, &g);
    GPIO_ResetBits(MOTOR_BIN_PORT, MOTOR_BIN1_PIN | MOTOR_BIN2_PIN);

    g.GPIO_Mode = GPIO_Mode_AF_PP;
    g.GPIO_Pin = GPIO_Pin_8 | GPIO_Pin_9;
    GPIO_Init(GPIOA, &g);

    TIM_TimeBaseInitTypeDef tb;
    tb.TIM_ClockDivision = TIM_CKD_DIV1;
    tb.TIM_CounterMode = TIM_CounterMode_Up;
    tb.TIM_Period = MOTOR_PWM_PERIOD - 1;
    tb.TIM_Prescaler = 720 - 1;
    tb.TIM_RepetitionCounter = 0;
    TIM_TimeBaseInit(TIM1, &tb);

    TIM_OCInitTypeDef oc;
    TIM_OCStructInit(&oc);
    oc.TIM_OCMode = TIM_OCMode_PWM1;
    oc.TIM_OCPolarity = TIM_OCPolarity_High;
    oc.TIM_OutputState = TIM_OutputState_Enable;
    oc.TIM_Pulse = 0;

    TIM_OC1Init(TIM1, &oc);
    TIM_OC2Init(TIM1, &oc);

    TIM_CtrlPWMOutputs(TIM1, ENABLE);
    TIM_Cmd(TIM1, ENABLE);
}

void Motor_SetLeft(int16_t speed, Motor_Dir_t dir) {
    if (speed > 100) speed = 100;
    if (speed < 0) speed = 0;

    if (dir == MOTOR_DIR_FWD) {
        GPIO_SetBits(MOTOR_AIN_PORT, MOTOR_AIN1_PIN);
        GPIO_ResetBits(MOTOR_AIN_PORT, MOTOR_AIN2_PIN);
    } else {
        GPIO_ResetBits(MOTOR_AIN_PORT, MOTOR_AIN1_PIN);
        GPIO_SetBits(MOTOR_AIN_PORT, MOTOR_AIN2_PIN);
    }

    TIM_SetCompare1(TIM1, speed);
}

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

void Motor_SetDiffSpeed(int16_t leftSpeed, int16_t rightSpeed) {
    if (leftSpeed >= 0) Motor_SetLeft(leftSpeed, MOTOR_DIR_FWD);
    else Motor_SetLeft(-leftSpeed, MOTOR_DIR_REV);

    if (rightSpeed >= 0) Motor_SetRight(rightSpeed, MOTOR_DIR_FWD);
    else Motor_SetRight(-rightSpeed, MOTOR_DIR_REV);
}

void Motor_Stop(void) {
    GPIO_ResetBits(MOTOR_AIN_PORT, MOTOR_AIN1_PIN | MOTOR_AIN2_PIN);
    GPIO_ResetBits(MOTOR_BIN_PORT, MOTOR_BIN1_PIN | MOTOR_BIN2_PIN);
    TIM_SetCompare1(TIM1, 0);
    TIM_SetCompare2(TIM1, 0);
}

void Motor_Enable(void) {
    GPIO_SetBits(MOTOR_STBY_PORT, MOTOR_STBY_PIN);
}

void Motor_Disable(void) {
    GPIO_ResetBits(MOTOR_STBY_PORT, MOTOR_STBY_PIN);
}
