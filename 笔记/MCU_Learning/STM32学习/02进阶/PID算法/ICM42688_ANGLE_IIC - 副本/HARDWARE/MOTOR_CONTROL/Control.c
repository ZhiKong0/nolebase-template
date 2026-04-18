#include "Control.h"
#include "PID.h"

static float clampf(float v, float lo, float hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static float wrap_deg(float deg)
{
    while (deg > 180.0f) deg -= 360.0f;
    while (deg < -180.0f) deg += 360.0f;
    return deg;
}

static int16_t ramp_i16(int16_t target, int16_t current, int16_t step)
{
    int16_t diff;
    diff = (int16_t)(target - current);
    if (diff > step) return (int16_t)(current + step);
    if (diff < -step) return (int16_t)(current - step);
    return target;
}

void Control_Reset(control_ctx_t* ctx)
{
    ctx->outL = 0;
    ctx->outR = 0;
    ctx->yaw0 = 0.0f;
    ctx->yaw0_inited = 0;
    PID_ResetAB();
}

void Control_LockYaw0(control_ctx_t* ctx, float yaw)
{
    ctx->yaw0 = yaw;
    ctx->yaw0_inited = 1;
}

void Control_Step10ms(control_ctx_t* ctx, float yaw, int16_t dl, int16_t dr,
                     int16_t* pwmL, int16_t* pwmR,
                     float* yaw_err, float* corr)
{
    float e;
    float c;
    float ref_l;
    float ref_r;
    int32_t cmdL;
    int32_t cmdR;

    if (!ctx->yaw0_inited)
    {
        Control_LockYaw0(ctx, yaw);
    }

    e = wrap_deg(yaw - ctx->yaw0);
    if (e > -ctx->yaw_deadband && e < ctx->yaw_deadband) e = 0.0f;

    c = ctx->yaw_k * e;
    c = clampf(c, -ctx->corr_max, ctx->corr_max);

    ref_l = ctx->base_ref - c;
    ref_r = ctx->base_ref + c;

    Speed1 = dl;
    Speed2 = dr;

    cmdL = (int32_t)PID_A((short)ref_l);
    cmdR = (int32_t)PID_B((short)ref_r);

    cmdL = cmdL - (int32_t)ctx->trim_pwm;
    cmdR = cmdR + (int32_t)ctx->trim_pwm;

    ctx->outL = ramp_i16((int16_t)cmdL, ctx->outL, ctx->pwm_step);
    ctx->outR = ramp_i16((int16_t)cmdR, ctx->outR, ctx->pwm_step);

    *pwmL = ctx->outL;
    *pwmR = ctx->outR;
    *yaw_err = e;
    *corr = c;
}
