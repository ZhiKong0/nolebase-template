#include "stm32f10x.h"
#include "Delay.h"
#include "OLED.h"
#include "Motor.h"
#include "Encoder.h"
#include "MPU6050.h"
#include "TIM4.h"

/* ========== 串级PID：角度环输出作为速度环输入 ==========
 * 
 * 视频讲解的正确方法：
 * 1. 角度环（外环）：输入Yaw，输出修正量
 * 2. 速度环（内环）：目标速度 = 基础速度 ± 角度环输出
 * 3. 左右电机分别做速度闭环
 * 
 * 关键：角度环输出不是直接PWM，而是修改速度目标值！
 */

/* ========== 参数配置 ========== */
#define BASE_SPEED          30      // 基础目标速度（编码器脉冲/10ms）
#define MAX_PWM             50
#define MIN_PWM             10

// 角度环PD（外环）
#define ANGLE_KP            10.0f   // Kp=10：偏1度→输出10
#define ANGLE_KD            2.0f    // 阻尼

// 速度环PI（内环）
#define SPEED_KP            2.0f    // 速度P
#define SPEED_KI            0.3f    // 速度I
#define SPEED_I_LIMIT       100.0f  // 积分限幅

// 启动参数
#define GYRO_ZERO_SAMPLES   500
#define YAW_LIMIT           60.0f

/* ========== 全局变量 ========== */
static volatile float g_yaw = 0.0f;
static volatile float g_gyroZero = 0.0f;
static volatile uint8_t g_ready = 0;

// 速度环状态（分别控制左右轮）
static volatile float g_integL = 0.0f;   // 左轮积分
static volatile float g_integR = 0.0f;   // 右轮积分
static volatile int16_t g_lastErrL = 0;  // 左轮上次误差
static volatile int16_t g_lastErrR = 0;  // 右轮上次误差

// 显示变量
static volatile int16_t g_yawDisp = 0;
static volatile int16_t g_angleOutDisp = 0;
static volatile int16_t g_pwmLDisp = 0;
static volatile int16_t g_pwmRDisp = 0;

/* ========== 函数声明 ========== */
static int16_t Angle_PD(float yaw, float gyroZ);
static int16_t Speed_PI(int16_t target, int16_t actual, float *integral, int16_t *lastErr);

int main(void)
{
    OLED_Init();
    OLED_ShowString(1, 1, "Cascade");
    OLED_ShowString(2, 1, "PID V1");
    
    Motor_Init();
    // Motor_Enable();  // 禁用电机！
    Encoder_Init();
    
    // MPU6050初始化
    MPU6050_Init();
    OLED_ShowString(3, 1, "MPU:");
    OLED_ShowHexNum(3, 5, MPU6050_WhoAmI(), 2);
    
    // === 严格零点校准 ===
    OLED_ShowString(4, 1, "Zero Cal...");
    double gyroSum = 0.0;
    for (int i = 0; i < GYRO_ZERO_SAMPLES; i++)
    {
        gyroSum += MPU6050_ReadGyroZ_dps();
        Delay_ms(5);
    }
    g_gyroZero = (float)(gyroSum / GYRO_ZERO_SAMPLES);
    
    // === 预热积分 ===
    OLED_ShowString(4, 1, "Warming... ");
    for (int i = 0; i < 200; i++)
    {
        float gz = MPU6050_ReadGyroZ_dps() - g_gyroZero;
        g_yaw += gz * 0.01f;
        Delay_ms(10);
    }
    g_yaw = 0.0f;
    
    OLED_ShowString(4, 1, "Ready      ");
    Delay_ms(300);
    OLED_Clear();
    
    OLED_ShowString(1, 1, "DEBUG MPU");
    OLED_ShowString(2, 1, "NO MOTOR");
    OLED_ShowString(3, 1, "RAW:");
    OLED_ShowString(4, 1, "DPS:");
    
    while(1)
    {
        int16_t rawGz = MPU6050_ReadGyroZ_Raw();  // 原始值
        float dps = MPU6050_ReadGyroZ_dps();      // 度/秒
        float gz = dps - g_gyroZero;
        g_yaw += gz * 0.01f;
        
        if (g_yaw > YAW_LIMIT) g_yaw = YAW_LIMIT;
        if (g_yaw < -YAW_LIMIT) g_yaw = -YAW_LIMIT;
        
        OLED_ShowSignedNum(3, 5, rawGz, 6);       // 原始值
        OLED_ShowSignedNum(4, 5, (int16_t)dps, 4); // DPS值
        
        Delay_ms(100);
    }
}

/* TIM4中断 - 10ms控制周期 */
void TIM4_IRQHandler(void)
{
    if (TIM_GetITStatus(TIM4, TIM_IT_Update) == SET)
    {
        TIM_ClearITPendingBit(TIM4, TIM_IT_Update);
        
        if (!g_ready) return;
        
        // === 1. 读取传感器 ===
        int16_t dL = EncoderA_GetDelta();  // 左轮实际速度
        int16_t dR = EncoderB_GetDelta();  // 右轮实际速度
        float gz = MPU6050_ReadGyroZ_dps() - g_gyroZero;
        g_yaw += gz * 0.01f;
        
        // Yaw限幅
        if (g_yaw > YAW_LIMIT) g_yaw = YAW_LIMIT;
        if (g_yaw < -YAW_LIMIT) g_yaw = -YAW_LIMIT;
        
        // === 2. 角度环（外环）：输出修正量 ===
        // Yaw>0表示左转，需要输出正值来纠正
        int16_t angleOutput = Angle_PD(g_yaw, gz);
        
        // === 3. 速度环（内环）：串级结构 ===
        // 关键：角度环输出修改速度目标，不是直接PWM！
        // Yaw>0(左转) → angleOutput>0 → 左轮加速、右轮减速 → 右转纠正
        int16_t targetL = BASE_SPEED + angleOutput;  // 左轮目标 = 基础 + 修正
        int16_t targetR = BASE_SPEED - angleOutput;  // 右轮目标 = 基础 - 修正
        
        int16_t pwmL = Speed_PI(targetL, dL, &g_integL, &g_lastErrL);
        int16_t pwmR = Speed_PI(targetR, dR, &g_integR, &g_lastErrR);
        
        // PWM限幅
        if (pwmL < MIN_PWM) pwmL = MIN_PWM;
        if (pwmL > MAX_PWM) pwmL = MAX_PWM;
        if (pwmR < MIN_PWM) pwmR = MIN_PWM;
        if (pwmR > MAX_PWM) pwmR = MAX_PWM;
        
        // 输出到电机
        MotorA_SetSpeed((uint8_t)pwmL);
        MotorB_SetSpeed((uint8_t)pwmR);
        
        // 更新显示
        g_yawDisp = (int16_t)g_yaw;
        g_angleOutDisp = angleOutput;
        g_pwmLDisp = pwmL;
        g_pwmRDisp = pwmR;
    }
}

/* 角度环PD（外环）
 * 输入：Yaw角度、角速度
 * 输出：速度目标修正量（不是PWM！）
 * Kp=10：偏1度→输出10（加到速度目标上）
 */
static int16_t Angle_PD(float yaw, float gyroZ)
{
    float output = ANGLE_KP * yaw + ANGLE_KD * gyroZ;
    
    // 限幅（不能太大，否则速度目标不合理）
    if (output > 20.0f) output = 20.0f;
    if (output < -20.0f) output = -20.0f;
    
    return (int16_t)output;
}

/* 速度环PI（内环）
 * 增量式PI：输出PWM
 */
static int16_t Speed_PI(int16_t target, int16_t actual, float *integral, int16_t *lastErr)
{
    int16_t error = target - actual;
    
    // 积分
    *integral += error;
    if (*integral > SPEED_I_LIMIT) *integral = SPEED_I_LIMIT;
    if (*integral < -SPEED_I_LIMIT) *integral = -SPEED_I_LIMIT;
    
    // 增量式：P*误差 + I*积分
    float output = SPEED_KP * error + SPEED_KI * (*integral);
    
    *lastErr = error;
    
    return (int16_t)output;
}