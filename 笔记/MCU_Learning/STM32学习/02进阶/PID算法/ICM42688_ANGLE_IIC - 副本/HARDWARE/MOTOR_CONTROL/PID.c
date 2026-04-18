#include "PID.h"

int16_t Speed1 = 0;
int16_t Speed2 = 0;

static float clampf(float v, float lo, float hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

void PID_SpeedInit(pid_speed_t* p, float kp, float ki, float kd, float dt,
                   float integral_min, float integral_max,
                   float out_min, float out_max)
{
    p->kp = kp;
    p->ki = ki;
    p->kd = kd;
    p->dt = dt;
    p->integral = 0.0f;
    p->integral_min = integral_min;
    p->integral_max = integral_max;
    p->prev_err = 0.0f;
    p->out_min = out_min;
    p->out_max = out_max;
}

void PID_SpeedReset(pid_speed_t* p)
{
    p->integral = 0.0f;
    p->prev_err = 0.0f;
}

float PID_SpeedUpdate(pid_speed_t* p, float target, float measure)
{
    float err;
    float deriv;
    float out;

    err = target - measure;
    p->integral += err * p->dt;
    p->integral = clampf(p->integral, p->integral_min, p->integral_max);

    deriv = (err - p->prev_err) / p->dt;
    out = p->kp * err + p->ki * p->integral + p->kd * deriv;
    out = clampf(out, p->out_min, p->out_max);
    p->prev_err = err;

    return out;
}

void PID_AdvInit(pid_adv_t* pid,
                 float kp, float ki, float kd, float kaw,
                 float dt, float tau_d,
                 float i_min, float i_max,
                 float out_min, float out_max)
{
    pid->kp = kp;
    pid->ki = ki;
    pid->kd = kd;
    pid->kaw = kaw;
    pid->dt = dt;
    pid->tau_d = tau_d;
    pid->out_min = out_min;
    pid->out_max = out_max;
    pid->i_min = i_min;
    pid->i_max = i_max;
    pid->integral = 0.0f;
    pid->d_state = 0.0f;
    pid->prev_meas = 0.0f;
    pid->prev_out = 0.0f;
}

void PID_AdvReset(pid_adv_t* pid, float meas, float out_init)
{
    pid->integral = clampf(out_init, pid->i_min, pid->i_max);
    pid->d_state = 0.0f;
    pid->prev_meas = meas;
    pid->prev_out = clampf(out_init, pid->out_min, pid->out_max);
}

float PID_AdvUpdate(pid_adv_t* pid, float ref, float meas, float ff)
{
    float err;
    float d_raw;
    float alpha;
    float unsat;
    float out;

    err = ref - meas;
    if (pid->dt <= 0.0f)
    {
        return 0.0f;
    }

    d_raw = (meas - pid->prev_meas) / pid->dt;
    if (pid->tau_d > 0.0f)
    {
        alpha = pid->dt / (pid->tau_d + pid->dt);
        pid->d_state += alpha * (d_raw - pid->d_state);
    }
    else
    {
        pid->d_state = d_raw;
    }

    unsat = ff + pid->kp * err + pid->integral - pid->kd * pid->d_state;
    out = clampf(unsat, pid->out_min, pid->out_max);

    pid->integral += (pid->ki * err + pid->kaw * (out - unsat)) * pid->dt;
    pid->integral = clampf(pid->integral, pid->i_min, pid->i_max);

    unsat = ff + pid->kp * err + pid->integral - pid->kd * pid->d_state;
    out = clampf(unsat, pid->out_min, pid->out_max);

    pid->prev_meas = meas;
    pid->prev_out = out;
    return out;
}

static pid_adv_t g_pidA;
static pid_adv_t g_pidB;
static uint8_t g_pidInited = 0u;

static void PID_InitABOnce(void)
{
    if (g_pidInited) return;

    PID_AdvInit(&g_pidA,
                0.04f, 0.25f, 0.0f, 1.2f,
                0.005f, 0.02f,
                -350.0f, 350.0f,
                -1000.0f, 1000.0f);
    PID_AdvInit(&g_pidB,
                0.04f, 0.25f, 0.0f, 1.2f,
                0.005f, 0.02f,
                -350.0f, 350.0f,
                -1000.0f, 1000.0f);
    PID_AdvReset(&g_pidA, 0.0f, 0.0f);
    PID_AdvReset(&g_pidB, 0.0f, 0.0f);
    g_pidInited = 1u;
}

void PID_ResetAB(void)
{
    PID_InitABOnce();
    PID_AdvReset(&g_pidA, (float)Speed1, 0.0f);
    PID_AdvReset(&g_pidB, (float)Speed2, 0.0f);
}

int PID_A(short Aima_v)
{
    float out;

    PID_InitABOnce();
    out = PID_AdvUpdate(&g_pidA, (float)Aima_v, (float)Speed1, 0.0f);
    return (int)out;
}

int PID_B(short Aimb_v)
{
    float out;

    PID_InitABOnce();
    out = PID_AdvUpdate(&g_pidB, (float)Aimb_v, (float)Speed2, 0.0f);
    return (int)out;
}
