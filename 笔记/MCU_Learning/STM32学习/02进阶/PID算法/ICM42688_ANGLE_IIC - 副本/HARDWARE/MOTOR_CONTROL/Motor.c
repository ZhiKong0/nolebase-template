#include "Motor.h"
#include "PWM.h"

void Motor_Init(uint16_t pwm_arr)
{
    GPIO_InitTypeDef gpio;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_GPIOB | RCC_APB2Periph_AFIO, ENABLE);

    gpio.GPIO_Mode = GPIO_Mode_Out_PP;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;

    gpio.GPIO_Pin = GPIO_Pin_4 | GPIO_Pin_5;
    GPIO_Init(GPIOA, &gpio);

    gpio.GPIO_Pin = GPIO_Pin_1 | GPIO_Pin_10 | GPIO_Pin_0;
    GPIO_Init(GPIOB, &gpio);

    Motor_Enable(0);
    PWM_Init(pwm_arr);
}

void Motor_Enable(uint8_t en)
{
    if (en) GPIO_SetBits(GPIOB, GPIO_Pin_0);
    else GPIO_ResetBits(GPIOB, GPIO_Pin_0);
}

void l_go(void)
{
    GPIO_SetBits(GPIOA, GPIO_Pin_4);
    GPIO_ResetBits(GPIOA, GPIO_Pin_5);
}

void r_go(void)
{
    GPIO_SetBits(GPIOB, GPIO_Pin_1);
    GPIO_ResetBits(GPIOB, GPIO_Pin_10);
}

void l_back(void)
{
    GPIO_ResetBits(GPIOA, GPIO_Pin_4);
    GPIO_SetBits(GPIOA, GPIO_Pin_5);
}

void r_back(void)
{
    GPIO_ResetBits(GPIOB, GPIO_Pin_1);
    GPIO_SetBits(GPIOB, GPIO_Pin_10);
}

void left(short output)
{
    if (output > 1000) output = 1000;
    else if (output < -1000) output = -1000;

    if (output > 0)
    {
        l_go();
        PWM_SetCompare1((uint16_t)output);
    }
    else if (output < 0)
    {
        l_back();
        PWM_SetCompare1((uint16_t)(-output));
    }
    else
    {
        PWM_SetCompare1(0);
    }
}

void right(short output)
{
    if (output > 1000) output = 1000;
    else if (output < -1000) output = -1000;

    if (output > 0)
    {
        r_go();
        PWM_SetCompare2((uint16_t)output);
    }
    else if (output < 0)
    {
        r_back();
        PWM_SetCompare2((uint16_t)(-output));
    }
    else
    {
        PWM_SetCompare2(0);
    }
}
