#include "Control.h"

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

    if (step <= 0) return target;

    diff = (int16_t)(target - current);
    if (diff > step) return (int16_t)(current + step);
    if (diff < -step) return (int16_t)(current - step);
    return target;
}

static int16_t apply_min_pwm(int16_t v, int16_t min_pwm)
{
    if (v > 0)
    {
        if (v < min_pwm) return min_pwm;
        return v;
    }
    if (v < 0)
    {
        if (-v < min_pwm) return (int16_t)(-min_pwm);
        return v;
    }
    return 0;
}

static void apply_pid_cfg(pid_ctrl_t* pid, const control_pid_cfg_t* cfg)
{
    PID_Init(pid, cfg->kp, cfg->ki, cfg->kd, 0.01f);
    PID_SetIntegralLimits(pid, cfg->integral_min, cfg->integral_max);
    PID_SetOutputLimits(pid, cfg->out_min, cfg->out_max);
    PID_SetDerivFilter(pid, cfg->deriv_alpha);
}

static void update_yaw_state(control_ctx_t* ctx, float yaw_abs)
{
    if (!ctx->yaw_zero_inited)
    {
        Control_SetYawZero(ctx, yaw_abs);
    }

    ctx->state.yaw_abs = yaw_abs;
    ctx->state.yaw_relative = wrap_deg(yaw_abs - ctx->state.yaw_zero);
    ctx->state.yaw_error = ctx->basic.yaw_target_rel - ctx->state.yaw_relative;

    if (ctx->state.yaw_error > -ctx->basic.yaw_deadband && ctx->state.yaw_error < ctx->basic.yaw_deadband)
    {
        ctx->state.yaw_error = 0.0f;
    }
}

static void update_speed_state(control_ctx_t* ctx, int16_t dl, int16_t dr)
{
    float alpha;

    alpha = clampf(ctx->basic.speed_alpha, 0.0f, 1.0f);
    if (alpha <= 0.0f)
    {
        ctx->state.meas_l = (float)dl;
        ctx->state.meas_r = (float)dr;
        return;
    }

    ctx->state.meas_l = ctx->state.meas_l + alpha * ((float)dl - ctx->state.meas_l);
    ctx->state.meas_r = ctx->state.meas_r + alpha * ((float)dr - ctx->state.meas_r);
}

void Control_LoadDefaultConfig(control_ctx_t* ctx)
{
    ctx->basic.base_speed_ref = 120.0f;
    ctx->basic.yaw_target_rel = 0.0f;
    ctx->basic.yaw_deadband = 2.0f;
    ctx->basic.speed_alpha = 0.35f;
    ctx->basic.trim_pwm = 30;
    ctx->basic.pwm_step = 60;
    ctx->basic.min_pwm = 140;

    ctx->yaw_pid_cfg.kp = 0.15f;
    ctx->yaw_pid_cfg.ki = 0.0f;
    ctx->yaw_pid_cfg.kd = 0.0f;
    ctx->yaw_pid_cfg.integral_min = -100.0f;
    ctx->yaw_pid_cfg.integral_max = 100.0f;
    ctx->yaw_pid_cfg.out_min = -10.0f;
    ctx->yaw_pid_cfg.out_max = 10.0f;
    ctx->yaw_pid_cfg.deriv_alpha = 0.25f;

    ctx->speed_l_pid_cfg.kp = 10.0f;
    ctx->speed_l_pid_cfg.ki = 0.04f;
    ctx->speed_l_pid_cfg.kd = 0.0f;
    ctx->speed_l_pid_cfg.integral_min = -5000.0f;
    ctx->speed_l_pid_cfg.integral_max = 5000.0f;
    ctx->speed_l_pid_cfg.out_min = -1000.0f;
    ctx->speed_l_pid_cfg.out_max = 1000.0f;
    ctx->speed_l_pid_cfg.deriv_alpha = 0.20f;

    ctx->speed_r_pid_cfg = ctx->speed_l_pid_cfg;
}

void Control_ApplyPidConfig(control_ctx_t* ctx)
{
    apply_pid_cfg(&ctx->yaw_pid, &ctx->yaw_pid_cfg);
    apply_pid_cfg(&ctx->speed_l_pid, &ctx->speed_l_pid_cfg);
    apply_pid_cfg(&ctx->speed_r_pid, &ctx->speed_r_pid_cfg);
}

void Control_ResetRuntime(control_ctx_t* ctx)
{
    PID_Reset(&ctx->yaw_pid);
    PID_Reset(&ctx->speed_l_pid);
    PID_Reset(&ctx->speed_r_pid);

    ctx->state.yaw_error = 0.0f;
    ctx->state.yaw_correction = 0.0f;
    ctx->state.speed_ref_l = 0.0f;
    ctx->state.speed_ref_r = 0.0f;
    ctx->state.meas_l = 0.0f;
    ctx->state.meas_r = 0.0f;
    ctx->state.pwm_l = 0;
    ctx->state.pwm_r = 0;
}

void Control_Init(control_ctx_t* ctx)
{
    Control_LoadDefaultConfig(ctx);
    ctx->yaw_zero_inited = 0;

    ctx->state.yaw_abs = 0.0f;
    ctx->state.yaw_zero = 0.0f;
    ctx->state.yaw_relative = 0.0f;
    ctx->state.yaw_error = 0.0f;
    ctx->state.yaw_correction = 0.0f;
    ctx->state.speed_ref_l = 0.0f;
    ctx->state.speed_ref_r = 0.0f;
    ctx->state.meas_l = 0.0f;
    ctx->state.meas_r = 0.0f;
    ctx->state.pwm_l = 0;
    ctx->state.pwm_r = 0;

    Control_ApplyPidConfig(ctx);
    Control_ResetRuntime(ctx);
}

void Control_SetYawZero(control_ctx_t* ctx, float yaw_abs)
{
    ctx->state.yaw_abs = yaw_abs;
    ctx->state.yaw_zero = yaw_abs;
    ctx->state.yaw_relative = 0.0f;
    ctx->state.yaw_error = 0.0f;
    ctx->state.yaw_correction = 0.0f;
    ctx->yaw_zero_inited = 1;
    PID_Reset(&ctx->yaw_pid);
}

void Control_UpdateObserve(control_ctx_t* ctx, float yaw_abs, int16_t dl, int16_t dr)
{
    update_yaw_state(ctx, yaw_abs);
    update_speed_state(ctx, dl, dr);
}

void Control_Step10ms(control_ctx_t* ctx, float yaw, int16_t dl, int16_t dr,
                     int16_t* pwmL, int16_t* pwmR,
                     float* yaw_err, float* corr)
{
    int32_t cmdL;
    int32_t cmdR;

    Control_UpdateObserve(ctx, yaw, dl, dr);

    ctx->state.yaw_correction = PID_UpdateByError(&ctx->yaw_pid, ctx->state.yaw_error);
    ctx->state.speed_ref_l = ctx->basic.base_speed_ref - ctx->state.yaw_correction;
    ctx->state.speed_ref_r = ctx->basic.base_speed_ref + ctx->state.yaw_correction;

    cmdL = (int32_t)PID_Update(&ctx->speed_l_pid, ctx->state.speed_ref_l, ctx->state.meas_l);
    cmdR = (int32_t)PID_Update(&ctx->speed_r_pid, ctx->state.speed_ref_r, ctx->state.meas_r);

    cmdL = cmdL - (int32_t)ctx->basic.trim_pwm;
    cmdR = cmdR + (int32_t)ctx->basic.trim_pwm;

    ctx->state.pwm_l = ramp_i16((int16_t)cmdL, ctx->state.pwm_l, ctx->basic.pwm_step);
    ctx->state.pwm_r = ramp_i16((int16_t)cmdR, ctx->state.pwm_r, ctx->basic.pwm_step);

    if (ctx->basic.min_pwm > 0)
    {
        ctx->state.pwm_l = apply_min_pwm(ctx->state.pwm_l, ctx->basic.min_pwm);
        ctx->state.pwm_r = apply_min_pwm(ctx->state.pwm_r, ctx->basic.min_pwm);
    }

    *pwmL = ctx->state.pwm_l;
    *pwmR = ctx->state.pwm_r;
    *yaw_err = ctx->state.yaw_error;
    *corr = ctx->state.yaw_correction;
}
