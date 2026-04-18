#include "delay.h"
#include "sys.h"
#include "usart.h" 
#include "icm42688.h"
#include "myiic.h"
#include "IMU.h"
#include "eeprom.h"
#include "spi.h"
#include "stm32f10x_tim.h"
#include <stdint.h>
#include "Motor.h"
#include "Control.h"
#include "VOFA.h"
#include "Key.h"
#include "Encoder.h"
// 模式选择需要在icm42688.h里选择宏定义ICM_USE_HARD_SPI/ICM_USE_I2C

// VCC--------5V或者3.3V都可以
//SPI 模式接线
// PA2------------------------CS
// PB13------------------------SCLK
// PB14------------------------MISO
// PB15------------------------MOSI

//IIC 模式接线
// PB10------------------------SCL
// PB11------------------------SDA
// AD0默认上拉可以不接
float motion6[7];         // IMU原始输出数据（加速度计/陀螺仪/磁力计）
float ypr[3];             // 上传yaw pitch roll的值（IMU解算输出：yaw/pitch/roll，单位：度）
int math_pl=0;            // 控制循环计数（用于简单统计/调试）

#define USE_TEXT_LOG 1
#define ENABLE_RX2_HEX 0

int main(void)
{
    uint16_t pwm_arr;         // TIM1的ARR（决定PWM周期）；这里用1000对应约1kHz
    uint16_t enc2_last;       // 上一次TIM2计数（用于求增量dl）
    uint16_t enc3_last;       // 上一次TIM3计数（用于求增量dr）
    uint32_t last_ctrl_tick;  // 上一次控制循环触发时刻（单位：100us tick）
    uint32_t now_tick;        // 当前时刻（单位：100us tick）
    uint16_t c2;              // TIM2当前计数值
    uint16_t c3;              // TIM3当前计数值
    int16_t dl;               // 左轮10ms编码器增量（计数/10ms）
    int16_t dr;               // 右轮10ms编码器增量（计数/10ms）
    float yaw_err;            // yaw误差（度，wrap到[-180,180]）
    float corr;               // yaw纠偏量（最终用于ref差速，单位：计数/10ms）
    uint8_t run_enable;       // 运行使能：1=跑控制，0=停止并关PWM
    uint8_t key_raw;          // PB5原始电平（1松开/0按下）
    uint8_t key_stable;       // PB5消抖后稳定电平（1松开/0按下）
    uint8_t key_last;         // 上一次稳定电平（用于沿检测）
    uint8_t key_cnt;          // 简单计数消抖（单位：10ms）
    uint32_t hb_last_tick;    // ASCII心跳上次发送时刻（100us tick）
    uint8_t rx_b;              // USART2接收调试：单字节
    uint8_t rx_cnt;            // USART2接收调试：本周期最多打印的字节数
    control_ctx_t ctrl;       // 双环控制上下文：参数、PID、相对偏航零点、运行态全在这里
    int16_t pwmL;
    int16_t pwmR;

    SystemInit();
    delay_init();         //延时函数初始化	  
    uart_init(115200);    //串口初始化为115200
    printf("OK\r\n");
    USART2_SendString("VOFA_READY\r\n");
    IIC_Init();              // 软件I2C初始化（当前工程已迁移到PB12/PB13）
    #ifdef ICM_USE_HARD_SPI
    SPI2_Init();
    #endif
    //SPI2_SetSpeed(SPI_BaudRatePrescaler_16);
    //delay_ms(100);
    
    //load_config();
    //delay_ms(50);
    IMU_init();              // ICM42688初始化 + 姿态解算初始化（内部启用TIM4做100us时基）

    // Motor/Encoder init (per 接线总表)
    key_pb5_init();            // PB5按键初始化（启停）
    pwm_arr = 1000;           // PWM周期ARR（1MHz/1000≈1kHz）
    Motor_Init(pwm_arr);
    encoder_tim2_init();      // TIM2编码器模式（左轮：PA0/PA1）
    encoder_tim3_init();      // TIM3编码器模式（右轮：PA6/PA7）
    left(0);
    right(0);

    enc2_last = 0;
    enc3_last = 0;
    last_ctrl_tick = nowtime; // 记录当前时刻，作为10ms周期起点
    run_enable = 0;        // 默认停止：需要按键短按启动
    key_raw = 1;
    key_stable = 1;
    key_last = 1;
    key_cnt = 0;
    Motor_Enable(0);
    hb_last_tick = 0;

    Control_Init(&ctrl);

    // ---- 基础控制参数：这些量决定整车行为，后续直接改这里即可 ----
    ctrl.basic.base_speed_ref = 120.0f;   // 内环基准速度目标（单位：编码器计数/10ms）
    ctrl.basic.yaw_target_rel = 0.0f;     // 相对偏航目标，通常保持0表示围绕初始朝向直行
    ctrl.basic.yaw_deadband = 2.0f;       // 相对偏航死区，小角度抖动不触发纠偏
    ctrl.basic.speed_alpha = 0.35f;       // 速度测量低通滤波系数，越大越灵敏
    ctrl.basic.trim_pwm = 30;             // 左右轮静态补偿，正值表示压左抬右
    ctrl.basic.pwm_step = 60;             // PWM斜坡限制，每10ms允许变化的最大幅度
    ctrl.basic.min_pwm = 140;             // 电机最小有效PWM，用于克服静摩擦

    // ---- 角度环PID：输入为相对偏航误差，输出为左右轮速度差 ----
    ctrl.yaw_pid_cfg.kp = 5.00f;
    ctrl.yaw_pid_cfg.ki = 0.0f;
    ctrl.yaw_pid_cfg.kd = 0.0f;
    ctrl.yaw_pid_cfg.integral_min = -100.0f;
    ctrl.yaw_pid_cfg.integral_max = 100.0f;
    ctrl.yaw_pid_cfg.out_min = -10.0f;
    ctrl.yaw_pid_cfg.out_max = 10.0f;
    ctrl.yaw_pid_cfg.deriv_alpha = 0.25f;

    // ---- 左右速度环PID：分别把左右轮速度目标闭环到编码器实测速度 ----
    ctrl.speed_l_pid_cfg.kp = 10.0f;
    ctrl.speed_l_pid_cfg.ki = 0.00f;
    ctrl.speed_l_pid_cfg.kd = 0.0f;
    ctrl.speed_l_pid_cfg.integral_min = -5000.0f;
    ctrl.speed_l_pid_cfg.integral_max = 5000.0f;
    ctrl.speed_l_pid_cfg.out_min = -1000.0f;
    ctrl.speed_l_pid_cfg.out_max = 1000.0f;
    ctrl.speed_l_pid_cfg.deriv_alpha = 0.20f;
    ctrl.speed_r_pid_cfg = ctrl.speed_l_pid_cfg;

    Control_ApplyPidConfig(&ctrl);
    IMU_getYawPitchRoll(ypr);
    Control_SetYawZero(&ctrl, ypr[0]);    // 上电时先把当前朝向定义为相对偏航零点
    Control_ResetRuntime(&ctrl);
    delay_ms(100);
    while(1)
    {   
        now_tick = nowtime;                               // nowtime单位=100us
        if ((uint16_t)(now_tick - last_ctrl_tick) >= 100) // 100 * 100us = 10ms（控制周期）
        {
            last_ctrl_tick = now_tick;

            if ((uint32_t)(now_tick - hb_last_tick) >= 5000u)
            {
                hb_last_tick = now_tick;
                printf("HB2 tick=%lu run=%u\r\n", (unsigned long)now_tick, (unsigned int)run_enable);
            }

            #if ENABLE_RX2_HEX
            rx_cnt = 0;
            while (USART2_ReadByte(&rx_b))
            {
                if (rx_cnt == 0)
                {
                    printf("RX2:");
                }
                printf(" %02X", (unsigned int)rx_b);
                rx_cnt++;
                if (rx_cnt >= 16) break;
            }
            if (rx_cnt)
            {
                printf("\r\n");
            }
            #endif

            IMU_getYawPitchRoll(ypr);                     // 每个控制周期先更新一次绝对yaw，供观察和控制共用

            // ---- PB5按键处理（短按切换启停，并把当前yaw重新置零）----
            key_raw = key_pb5_read_raw();                  // 读取原始电平
            if (key_raw == key_stable)
            {
                key_cnt = 0;                               // 与稳定态一致，清计数
            }
            else
            {
                if (key_cnt < 3) key_cnt++;                // 连续3次（约30ms）才确认状态变化
                if (key_cnt >= 3)
                {
                    key_stable = key_raw;                  // 更新稳定态
                    key_cnt = 0;
                }
            }

            if (key_last == 1 && key_stable == 0)          // 松开->按下：触发一次
            {
                Control_SetYawZero(&ctrl, ypr[0]);          // 每次按键都把当前朝向重新定义为相对偏航零点
                Control_ResetRuntime(&ctrl);                // 同时清空角度环/速度环内部状态，避免旧积分残留
                run_enable = (uint8_t)!run_enable;          // 切换启停
                if (!run_enable)
                {
                    left(0);
                    right(0);
                    Motor_Enable(0);
                }
                else
                {
                    Motor_Enable(1);
                }
            }
            key_last = key_stable;                          // 更新上一次稳定态

            c2 = TIM_GetCounter(TIM2);
            c3 = TIM_GetCounter(TIM3);
            dl = encoder_delta(c2, &enc2_last);
            dl = (int16_t)(-dl);                            // 左轮编码器方向在软件里统一取反，保持“前进为正”
            dr = encoder_delta(c3, &enc3_last);

            if (!run_enable)
            {
                // 停止状态下仍更新“相对偏航/编码器观察量”，这样你按键复位后能直接看到yrel回到0附近
                Control_UpdateObserve(&ctrl, ypr[0], dl, dr);
                #if USE_TEXT_LOG
                printf("y=%.6f y0=%.6f yrel=%.6f gzr=%.6f gzo=%.6f gzc=%.6f dl=%ld dr=%ld diff=%ld tick=%lu run=%u\r\n",
                    ypr[0], ctrl.state.yaw_zero, ctrl.state.yaw_relative,
                    imu_dbg_gyro_z_raw, imu_dbg_gyro_z_offset, imu_dbg_gyro_z_comp,
                    (long)dl, (long)dr, (long)(dr - dl),
                    (unsigned long)now_tick, (unsigned int)run_enable);
                #else
                VOFA_SendJustFloat4(ctrl.state.yaw_relative, (float)(dr - dl), 0.0f, 0.0f);
                #endif
                continue;
            }

            Control_Step10ms(&ctrl, ypr[0], dl, dr, &pwmL, &pwmR, &yaw_err, &corr);
            left(pwmL);
            right(pwmR);

            #if USE_TEXT_LOG
            printf("y=%.6f y0=%.6f yrel=%.6f yaw_e=%.6f corr=%.6f refL=%.2f refR=%.2f gzr=%.6f gzo=%.6f gzc=%.6f dl=%ld dr=%ld diff=%ld pwmL=%d pwmR=%d tick=%lu run=%u\r\n",
                ypr[0], ctrl.state.yaw_zero, ctrl.state.yaw_relative, yaw_err, corr,
                ctrl.state.speed_ref_l, ctrl.state.speed_ref_r,
                imu_dbg_gyro_z_raw, imu_dbg_gyro_z_offset, imu_dbg_gyro_z_comp,
                (long)dl, (long)dr, (long)(dr - dl),
                (int)pwmL, (int)pwmR,
                (unsigned long)now_tick, (unsigned int)run_enable);
            #else
            VOFA_SendJustFloat4(ctrl.state.yaw_relative, (float)(dr - dl), corr, yaw_err);
            #endif

            math_pl++;
        }
    }
}
