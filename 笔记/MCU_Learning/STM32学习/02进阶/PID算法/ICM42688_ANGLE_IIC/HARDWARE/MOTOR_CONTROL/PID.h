#ifndef __PID_SPEED_H
#define __PID_SPEED_H

#include "stm32f10x.h"
#include <stdint.h>

typedef struct
{
    float kp;
    float ki;
    float kd;
    float dt;

    float integral;
    float integral_min;
    float integral_max;

    float prev_err;
    float deriv;
    float deriv_alpha;

    float out_min;
    float out_max;
    float last_out;

    uint8_t deriv_inited;
} pid_ctrl_t;

void PID_Init(pid_ctrl_t* pid, float kp, float ki, float kd, float dt);
void PID_Reset(pid_ctrl_t* pid);
void PID_SetTunings(pid_ctrl_t* pid, float kp, float ki, float kd);
void PID_SetDt(pid_ctrl_t* pid, float dt);
void PID_SetIntegralLimits(pid_ctrl_t* pid, float integral_min, float integral_max);
void PID_SetOutputLimits(pid_ctrl_t* pid, float out_min, float out_max);
void PID_SetDerivFilter(pid_ctrl_t* pid, float deriv_alpha);
float PID_Update(pid_ctrl_t* pid, float target, float measure);
float PID_UpdateByError(pid_ctrl_t* pid, float error);

#endif
