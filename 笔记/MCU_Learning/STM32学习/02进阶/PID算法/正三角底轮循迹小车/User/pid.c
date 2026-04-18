#include "pid.h"
#include "Motor.h"
#include "Encoder.h"

PID_TypeDef PID_Left;
PID_TypeDef PID_Right;

int16_t g_left_speed;   // 编码器实时速度
int16_t g_right_speed;

// ====================== PID 初始化（增量式） ======================
void PID_Init(void)
{
    // 左轮速度环
    PID_Left.Kp = 1.0f;
    PID_Left.Ki = 0.08f;
    PID_Left.Kd = 0.05f;
    PID_Left.err = 0;
    PID_Left.last_err = 0;
    PID_Left.prev_err = 0;
    PID_Left.output = 0;

    // 右轮速度环
    PID_Right.Kp = 1.0f;
    PID_Right.Ki = 0.08f;
    PID_Right.Kd = 0.05f;
    PID_Right.err = 0;
    PID_Right.last_err = 0;
    PID_Right.prev_err = 0;
    PID_Right.output = 0;
}

// ====================== 增量式 PID（电机速度环专用，最稳） ======================
float PID_Calc(PID_TypeDef *pid, float target, float actual)
{
    pid->SetSpeed = target;
    pid->ActualSpeed = actual;

    pid->err = target - actual;

    // 增量式公式
    float increment = pid->Kp * (pid->err - pid->last_err)
                    + pid->Ki * pid->err
                    + pid->Kd * (pid->err - 2 * pid->last_err + pid->prev_err);

    pid->output += increment;

    // PWM 限幅（直接限制最终输出，干净利落）
    if (pid->output > 1999) pid->output = 1999;
    if (pid->output < 0)    pid->output = 0;

    // 保存误差
    pid->prev_err = pid->last_err;
    pid->last_err = pid->err;

    return pid->output;
}

// ====================== 速度闭环 + 直线校正（真正能跑直） ======================
#define PWM_MIN  30     // 启动阈值（根据你的电机调整）
#define PWM_MAX  1999

void PID_SpeedControl(int16_t target_speed)
{
    // 1. 读取速度
    g_left_speed  = Encoder_GetLeft();
    g_right_speed = Encoder_GetRight();

    // 2. 直线校正：两轮速度同步（核心！不抖、不飘）
    int16_t sync_err = g_left_speed - g_right_speed;
    int16_t adjust = sync_err / 4;   // 同步校正系数，越小越柔和

    // 3. 给两轮分配目标（自动补偿偏差，保持同速）
    int16_t target_L = target_speed - adjust;
    int16_t target_R = target_speed + adjust;

    // 4. PID 计算输出（直接输出 PWM）
    int16_t pwm_L = (int16_t)PID_Calc(&PID_Left,  target_L, g_left_speed);
    int16_t pwm_R = (int16_t)PID_Calc(&PID_Right, target_R, g_right_speed);

    // 5. 最终限幅
    if (pwm_L > PWM_MAX) pwm_L = PWM_MAX;
    if (pwm_L < PWM_MIN && pwm_L > 0) pwm_L = PWM_MIN;
    if (pwm_L < 0) pwm_L = 0;

    if (pwm_R > PWM_MAX) pwm_R = PWM_MAX;
    if (pwm_R < PWM_MIN && pwm_R > 0) pwm_R = PWM_MIN;
    if (pwm_R < 0) pwm_R = 0;

    // 6. 输出到电机
    Motor_Set(pwm_L, pwm_R);
}