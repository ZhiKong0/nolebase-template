#ifndef _PID_H
#define _PID_H

#include "stm32f10x.h"


extern int16_t g_left_speed;   
extern int16_t g_right_speed;

typedef struct
{
    float Kp;
    float Ki;
    float Kd;

    float err;       // 当前误差
    float last_err;  // 上一次
    float prev_err;  // 上上次
    float output;    // 输出 PWM

    float SetSpeed;
    float ActualSpeed;
} PID_TypeDef;



void PID_Init(void);
float PID_Calc(PID_TypeDef *pid, float target, float actual);
void PID_SpeedControl(int16_t target_speed);




#endif