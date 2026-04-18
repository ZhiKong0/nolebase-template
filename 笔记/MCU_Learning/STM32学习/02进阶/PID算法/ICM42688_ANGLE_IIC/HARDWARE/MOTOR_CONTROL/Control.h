#ifndef __CONTROL_H
#define __CONTROL_H

#include "stm32f10x.h"
#include <stdint.h>
#include "PID.h"

typedef struct
{
    float kp;
    float ki;
    float kd;
    float integral_min;
    float integral_max;
    float out_min;
    float out_max;
    float deriv_alpha;
} control_pid_cfg_t;

typedef struct
{
    float base_speed_ref;
    float yaw_target_rel;
    float yaw_deadband;
    float speed_alpha;
    int16_t trim_pwm;
    int16_t pwm_step;
    int16_t min_pwm;
} control_basic_cfg_t;

typedef struct
{
    float yaw_abs;
    float yaw_zero;
    float yaw_relative;
    float yaw_error;
    float yaw_correction;
    float speed_ref_l;
    float speed_ref_r;
    float meas_l;
    float meas_r;
    int16_t pwm_l;
    int16_t pwm_r;
} control_state_t;

typedef struct
{
    control_basic_cfg_t basic;
    control_pid_cfg_t yaw_pid_cfg;
    control_pid_cfg_t speed_l_pid_cfg;
    control_pid_cfg_t speed_r_pid_cfg;
    pid_ctrl_t yaw_pid;
    pid_ctrl_t speed_l_pid;
    pid_ctrl_t speed_r_pid;
    control_state_t state;
    uint8_t yaw_zero_inited;
} control_ctx_t;

void Control_Init(control_ctx_t* ctx);
void Control_LoadDefaultConfig(control_ctx_t* ctx);
void Control_ApplyPidConfig(control_ctx_t* ctx);
void Control_ResetRuntime(control_ctx_t* ctx);
void Control_SetYawZero(control_ctx_t* ctx, float yaw_abs);
void Control_UpdateObserve(control_ctx_t* ctx, float yaw_abs, int16_t dl, int16_t dr);
void Control_Step10ms(control_ctx_t* ctx, float yaw, int16_t dl, int16_t dr,
                     int16_t* pwmL, int16_t* pwmR,
                     float* yaw_err, float* corr);

#endif
