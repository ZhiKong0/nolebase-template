#include "DrvMotor.h"
#include "BoardConfig.h"

static uint8_t s_motorEnabled = 0u;

static int16_t drv_motor_abs(int16_t value)
{
    return (value < 0) ? (int16_t)(-value) : value;
}

static int16_t drv_motor_clamp(int16_t value)
{
    if (value > (int16_t)BOARD_MOTOR_PWM_PERIOD) {
        return (int16_t)BOARD_MOTOR_PWM_PERIOD;
    }
    if (value < (int16_t)(-((int16_t)BOARD_MOTOR_PWM_PERIOD))) {
        return (int16_t)(-((int16_t)BOARD_MOTOR_PWM_PERIOD));
    }
    return value;
}

static void drv_motor_set_left(int16_t pwm)
{
    int16_t duty = drv_motor_abs(pwm);

    if (pwm > 0) {
        GPIO_SetBits(BOARD_MOTOR_AIN_PORT, BOARD_MOTOR_AIN1_PIN);
        GPIO_ResetBits(BOARD_MOTOR_AIN_PORT, BOARD_MOTOR_AIN2_PIN);
    } else if (pwm < 0) {
        GPIO_ResetBits(BOARD_MOTOR_AIN_PORT, BOARD_MOTOR_AIN1_PIN);
        GPIO_SetBits(BOARD_MOTOR_AIN_PORT, BOARD_MOTOR_AIN2_PIN);
    } else {
        GPIO_ResetBits(BOARD_MOTOR_AIN_PORT, BOARD_MOTOR_AIN1_PIN | BOARD_MOTOR_AIN2_PIN);
        duty = 0;
    }

    TIM_SetCompare1(BOARD_MOTOR_PWM_TIMER, (uint16_t)duty);
}

static void drv_motor_set_right(int16_t pwm)
{
    int16_t duty = drv_motor_abs(pwm);

    if (pwm > 0) {
        GPIO_SetBits(BOARD_MOTOR_BIN_PORT, BOARD_MOTOR_BIN1_PIN);
        GPIO_ResetBits(BOARD_MOTOR_BIN_PORT, BOARD_MOTOR_BIN2_PIN);
    } else if (pwm < 0) {
        GPIO_ResetBits(BOARD_MOTOR_BIN_PORT, BOARD_MOTOR_BIN1_PIN);
        GPIO_SetBits(BOARD_MOTOR_BIN_PORT, BOARD_MOTOR_BIN2_PIN);
    } else {
        GPIO_ResetBits(BOARD_MOTOR_BIN_PORT, BOARD_MOTOR_BIN1_PIN | BOARD_MOTOR_BIN2_PIN);
        duty = 0;
    }

    TIM_SetCompare2(BOARD_MOTOR_PWM_TIMER, (uint16_t)duty);
}

void DrvMotor_Init(void)
{
    GPIO_InitTypeDef gpio;
    TIM_TimeBaseInitTypeDef tb;
    TIM_OCInitTypeDef oc;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_GPIOB | RCC_APB2Periph_TIM1, ENABLE);

    gpio.GPIO_Mode = GPIO_Mode_Out_PP;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;

    gpio.GPIO_Pin = BOARD_MOTOR_STBY_PIN;
    GPIO_Init(BOARD_MOTOR_STBY_PORT, &gpio);

    gpio.GPIO_Pin = BOARD_MOTOR_AIN1_PIN | BOARD_MOTOR_AIN2_PIN;
    GPIO_Init(BOARD_MOTOR_AIN_PORT, &gpio);

    gpio.GPIO_Pin = BOARD_MOTOR_BIN1_PIN | BOARD_MOTOR_BIN2_PIN;
    GPIO_Init(BOARD_MOTOR_BIN_PORT, &gpio);

    gpio.GPIO_Mode = GPIO_Mode_AF_PP;
    gpio.GPIO_Pin = GPIO_Pin_8 | GPIO_Pin_9;
    GPIO_Init(GPIOA, &gpio);

    tb.TIM_ClockDivision = TIM_CKD_DIV1;
    tb.TIM_CounterMode = TIM_CounterMode_Up;
    tb.TIM_Period = BOARD_MOTOR_PWM_PERIOD - 1u;
    tb.TIM_Prescaler = BOARD_MOTOR_PWM_PRESCALER;
    tb.TIM_RepetitionCounter = 0u;
    TIM_TimeBaseInit(BOARD_MOTOR_PWM_TIMER, &tb);

    TIM_OCStructInit(&oc);
    oc.TIM_OCMode = TIM_OCMode_PWM1;
    oc.TIM_OCPolarity = TIM_OCPolarity_High;
    oc.TIM_OutputState = TIM_OutputState_Enable;
    oc.TIM_Pulse = 0u;
    TIM_OC1Init(BOARD_MOTOR_PWM_TIMER, &oc);
    TIM_OC2Init(BOARD_MOTOR_PWM_TIMER, &oc);
    TIM_OC1PreloadConfig(BOARD_MOTOR_PWM_TIMER, TIM_OCPreload_Enable);
    TIM_OC2PreloadConfig(BOARD_MOTOR_PWM_TIMER, TIM_OCPreload_Enable);
    TIM_ARRPreloadConfig(BOARD_MOTOR_PWM_TIMER, ENABLE);

    TIM_CtrlPWMOutputs(BOARD_MOTOR_PWM_TIMER, ENABLE);
    TIM_Cmd(BOARD_MOTOR_PWM_TIMER, ENABLE);

    DrvMotor_Stop();
    DrvMotor_SetEnabled(0u);
}

void DrvMotor_SetEnabled(uint8_t enabled)
{
    s_motorEnabled = enabled ? 1u : 0u;
    if (s_motorEnabled != 0u) {
        GPIO_SetBits(BOARD_MOTOR_STBY_PORT, BOARD_MOTOR_STBY_PIN);
    } else {
        GPIO_ResetBits(BOARD_MOTOR_STBY_PORT, BOARD_MOTOR_STBY_PIN);
        DrvMotor_Stop();
    }
}

void DrvMotor_Stop(void)
{
    drv_motor_set_left(0);
    drv_motor_set_right(0);
}

void DrvMotor_Apply(int16_t leftPwm, int16_t rightPwm)
{
    leftPwm = drv_motor_clamp(leftPwm);
    rightPwm = drv_motor_clamp(rightPwm);

    if (s_motorEnabled == 0u) {
        DrvMotor_Stop();
        return;
    }

    drv_motor_set_left(leftPwm);
    drv_motor_set_right(rightPwm);
}
