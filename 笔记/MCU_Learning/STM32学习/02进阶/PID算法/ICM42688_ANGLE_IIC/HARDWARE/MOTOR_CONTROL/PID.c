#include "PID.h"

static float clampf(float v, float lo, float hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

void PID_Init(pid_ctrl_t* pid, float kp, float ki, float kd, float dt)
{
    pid->kp = kp;
    pid->ki = ki;
    pid->kd = kd;
    pid->dt = dt;

    pid->integral = 0.0f;
    pid->integral_min = -1000.0f;
    pid->integral_max = 1000.0f;

    pid->prev_err = 0.0f;
    pid->deriv = 0.0f;
    pid->deriv_alpha = 1.0f;

    pid->out_min = -1000.0f;
    pid->out_max = 1000.0f;
    pid->last_out = 0.0f;
    pid->deriv_inited = 0;
}

void PID_Reset(pid_ctrl_t* pid)
{
    pid->integral = 0.0f;
    pid->prev_err = 0.0f;
    pid->deriv = 0.0f;
    pid->last_out = 0.0f;
    pid->deriv_inited = 0;
}

void PID_SetTunings(pid_ctrl_t* pid, float kp, float ki, float kd)
{
    pid->kp = kp;
    pid->ki = ki;
    pid->kd = kd;
}

void PID_SetDt(pid_ctrl_t* pid, float dt)
{
    if (dt > 0.0f)
    {
        pid->dt = dt;
    }
}

void PID_SetIntegralLimits(pid_ctrl_t* pid, float integral_min, float integral_max)
{
    pid->integral_min = integral_min;
    pid->integral_max = integral_max;
    pid->integral = clampf(pid->integral, pid->integral_min, pid->integral_max);
}

void PID_SetOutputLimits(pid_ctrl_t* pid, float out_min, float out_max)
{
    pid->out_min = out_min;
    pid->out_max = out_max;
    pid->last_out = clampf(pid->last_out, pid->out_min, pid->out_max);
}

void PID_SetDerivFilter(pid_ctrl_t* pid, float deriv_alpha)
{
    pid->deriv_alpha = clampf(deriv_alpha, 0.0f, 1.0f);
}

float PID_UpdateByError(pid_ctrl_t* pid, float error)
{
    float dt;
    float raw_deriv;
    float filt_deriv;
    float next_integral;
    float out;

    dt = pid->dt;
    if (dt <= 0.0f)
    {
        dt = 0.01f;
    }

    if (!pid->deriv_inited)
    {
        raw_deriv = 0.0f;
        filt_deriv = 0.0f;
        pid->deriv_inited = 1;
    }
    else
    {
        raw_deriv = (error - pid->prev_err) / dt;
        filt_deriv = pid->deriv + pid->deriv_alpha * (raw_deriv - pid->deriv);
    }
    pid->deriv = filt_deriv;

    next_integral = pid->integral + error * dt;
    next_integral = clampf(next_integral, pid->integral_min, pid->integral_max);

    out = pid->kp * error + pid->ki * next_integral + pid->kd * pid->deriv;

    if ((out > pid->out_max && error > 0.0f) || (out < pid->out_min && error < 0.0f))
    {
        next_integral = pid->integral;
        out = pid->kp * error + pid->ki * next_integral + pid->kd * pid->deriv;
    }

    pid->integral = next_integral;
    pid->prev_err = error;
    pid->last_out = clampf(out, pid->out_min, pid->out_max);
    return pid->last_out;
}

float PID_Update(pid_ctrl_t* pid, float target, float measure)
{
    return PID_UpdateByError(pid, target - measure);
}
