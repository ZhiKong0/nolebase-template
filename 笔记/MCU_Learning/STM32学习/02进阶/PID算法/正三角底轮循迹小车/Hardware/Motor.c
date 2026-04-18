#include "Motor.h"



void Motor_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure;
    TIM_TimeBaseInitTypeDef TIM_TimeBaseStructure;
    TIM_OCInitTypeDef TIM_OCInitStructure;

    // 开启时钟
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_GPIOB, ENABLE);
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_TIM1, ENABLE);

    // ======================
    // STBY 引脚 (PB0)
    // ======================
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_0;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOB, &GPIO_InitStructure);

    // ======================
    // 方向引脚 必须配置为推挽输出
    // ======================
    // 左电机：PA4(AIN1)、PA5(AIN2)
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_4 | GPIO_Pin_5;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    // 右电机：PB1(BIN1)、PB10(BIN2)
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_1 | GPIO_Pin_10;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOB, &GPIO_InitStructure);

    // ======================
    // PWM 引脚 (PA8、PA9)
    // ======================
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_8 | GPIO_Pin_9;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    // ======================
    // TIM1 配置 10kHz PWM
    // ======================
    TIM_TimeBaseStructure.TIM_Period = 999;
    TIM_TimeBaseStructure.TIM_Prescaler = 0;
    TIM_TimeBaseStructure.TIM_ClockDivision = TIM_CKD_DIV1;
    TIM_TimeBaseStructure.TIM_CounterMode = TIM_CounterMode_Up;
    TIM_TimeBaseStructure.TIM_RepetitionCounter = 0;
    TIM_TimeBaseInit(TIM1, &TIM_TimeBaseStructure);

    // ======================
    // PWM 模式配置
    // ======================
    TIM_OCInitStructure.TIM_OCMode = TIM_OCMode_PWM1;
    TIM_OCInitStructure.TIM_OutputState = TIM_OutputState_Enable;
    TIM_OCInitStructure.TIM_Pulse = 0;
    TIM_OCInitStructure.TIM_OCPolarity = TIM_OCPolarity_High;
    TIM_OC1Init(TIM1, &TIM_OCInitStructure);
    TIM_OC2Init(TIM1, &TIM_OCInitStructure);

    // 启动 TIM1
    TIM_Cmd(TIM1, ENABLE);
    TIM_CtrlPWMOutputs(TIM1, ENABLE);

    // 使能 TB6612
    GPIO_SetBits(GPIOB, GPIO_Pin_0);//1

    // 上电停止电机
    Motor_Stop();
}





void Motor_Set(int16_t left_speed, int16_t right_speed)
{
    // ====================== 左电机 ======================
    if(left_speed > 0)
    {
        // 正转
        GPIO_SetBits(GPIOA, GPIO_Pin_4);//1
        GPIO_ResetBits(GPIOA, GPIO_Pin_5);//0
    }
    else if(left_speed < 0)
    {
        // 反转
        GPIO_ResetBits(GPIOA, GPIO_Pin_4);//0
        GPIO_SetBits(GPIOA, GPIO_Pin_5);//1
        left_speed = -left_speed;
    }

    // ====================== 右电机 ======================
    if(right_speed > 0)
    {
        // 正转
        GPIO_SetBits(GPIOB, GPIO_Pin_1);//1
        GPIO_ResetBits(GPIOB, GPIO_Pin_10);//0
    }
    else if(right_speed < 0)    
    {
        // 反转
        GPIO_ResetBits(GPIOB, GPIO_Pin_1);//0
        GPIO_SetBits(GPIOB, GPIO_Pin_10);//1
        right_speed = -right_speed;
    }

    // ====================== 限幅保护 ======================
    if(left_speed > 999)     left_speed = 999;
    if(left_speed < -999)    left_speed = -999;

    if(right_speed > 999)    right_speed = 999;
    if(right_speed < -999)   right_speed = -999;

    // 设置 PWM
    TIM_SetCompare1(TIM1, left_speed);
    TIM_SetCompare2(TIM1, right_speed);
}

void Motor_Stop(void)
{
    // PWM 清零
    TIM_SetCompare1(TIM1, 0);
    TIM_SetCompare2(TIM1, 0);

    // 方向引脚全部低电平
    GPIO_ResetBits(GPIOA, GPIO_Pin_4 | GPIO_Pin_5);
    GPIO_ResetBits(GPIOB, GPIO_Pin_1 | GPIO_Pin_10);
}