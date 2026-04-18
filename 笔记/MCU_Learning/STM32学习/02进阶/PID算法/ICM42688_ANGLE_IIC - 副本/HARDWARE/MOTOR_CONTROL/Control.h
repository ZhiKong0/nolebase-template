#ifndef __CONTROL_H
#define __CONTROL_H

#include "stm32f10x.h"
#include <stdint.h>

typedef struct
{
    float base_ref;
    float yaw_k;
    float corr_max;
    float yaw_deadband;
    int16_t trim_pwm;
    int16_t pwm_step;
    int16_t outL;
    int16_t outR;
    float yaw0;
    uint8_t yaw0_inited;
} control_ctx_t;

void Control_Reset(control_ctx_t* ctx);
void Control_LockYaw0(control_ctx_t* ctx, float yaw);
void Control_Step10ms(control_ctx_t* ctx, float yaw, int16_t dl, int16_t dr,
                     int16_t* pwmL, int16_t* pwmR,
                     float* yaw_err, float* corr);

#endif
