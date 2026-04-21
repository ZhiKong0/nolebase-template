#ifndef __CONFIG_H
#define __CONFIG_H

#include "stm32f10x.h"

/* ========== 控制模式 ========== */
typedef enum
{
    MODE_STRAIGHT = 0, /* 直线模式 */
    MODE_TRACK = 1,    /* 循迹模式 */
    MODE_SPIN = 2      /* 旋进模式 */
} ControlMode_t;

typedef enum
{
    SYS_STOP = 0,      /* 停止 */
    SYS_STRAIGHT = 1,  /* 直线运行中 */
    SYS_TRACKING = 2,  /* 循迹运行中 */
    SYS_SPINNING = 3   /* 旋进运行中 */
} SystemState_t;

typedef enum
{
    KEY_EVENT_NONE = 0,        /* 无事件 */
    KEY_EVENT_SHORT_PRESS = 1, /* 短按 */
    KEY_EVENT_LONG_PRESS = 2   /* 长按 */
} KeyEvent_t;

/* ========== 电机引脚 (TB6612 驱动 + TIM1 PWM) ========== */
#define MOTOR_STBY_PORT GPIOB
#define MOTOR_STBY_PIN GPIO_Pin_0

#define MOTOR_AIN1_PORT GPIOA
#define MOTOR_AIN1_PIN GPIO_Pin_4
#define MOTOR_AIN2_PORT GPIOA
#define MOTOR_AIN2_PIN GPIO_Pin_5

#define MOTOR_BIN1_PORT GPIOB
#define MOTOR_BIN1_PIN GPIO_Pin_1
#define MOTOR_BIN2_PORT GPIOB
#define MOTOR_BIN2_PIN GPIO_Pin_10

#define MOTOR_PWM_TIM TIM1
#define MOTOR_PWM_PERIOD 1000  /* PWM 周期 */
#define MOTOR_PWM_PRESCALER 72 /* 预分频 */
#define MOTOR_PWM_MAX 600      /* 单轮 PWM 上限 */
#define MOTOR_DIFF_MAX 200     /* 航向差速上限 */
#define MOTOR_DEADZONE 80      /* 死区补偿：低于此值电机不动，自动抬升到此值 */

/* ========== 编码器 (TIM2 左轮, TIM3 右轮) ========== */
#define ENC_LEFT_TIM TIM2
#define ENC_RIGHT_TIM TIM3
#define ENC_IC_FILTER 12 /* 输入捕获滤波 */

#define ENC_PPR 11               /* 每转脉冲数 */
#define ENC_RATIO 30             /* 减速比 */
#define ENC_QUAD_MULT 4          /* 正交4倍频 */
#define ENC_LEFT_SIGN (-1)       /* 左轮计数方向修正 */
#define ENC_RIGHT_SIGN 1         /* 右轮计数方向修正 */
#define ENC_MAX_DELTA 2600       /* 单周期最大合法增量，超过视为毛刺丢弃 */
#define ENC_SPEED_LPF_ALPHA 0.3f /* 速度低通滤波系数 (0~1, 越大越灵敏) */

/* ========== BNO085 九轴IMU (软件I2C) ========== */
#define BNO_SCL_PORT GPIOB
#define BNO_SCL_PIN GPIO_Pin_12
#define BNO_SDA_PORT GPIOB
#define BNO_SDA_PIN GPIO_Pin_13
#define BNO_RST_PORT GPIOB
#define BNO_RST_PIN GPIO_Pin_14
#define BNO_INT_PORT GPIOA
#define BNO_INT_PIN GPIO_Pin_15

#define BNO_ADDR_DEFAULT 0x4B
#define BNO_ADDR_ALT 0x4A
#define BNO_ADDR_DOC_DEF 0x28
#define BNO_ADDR_DOC_ALT 0x29

#define BNO_REPORT_INTERVAL_US 20000UL /* 报告间隔 20ms */
#define BNO_BOOT_DELAY_MS 80u          /* 上电等待 */
#define BNO_PRESENT_TIMEOUT_MS 1000u   /* 检测超时 */
#define BNO_PACKET_MAX 384u            /* 最大包长 */

#define BNO_YAW_JUMP_REJECT_DEG 45.0f /* 航向跳变抑制阈值(度) */
#define BNO_YAW_RATE_LIMIT_DPS 180.0f /* 角速度限幅(度/秒) */
#define BNO_YAW_RATE_LPF_ALPHA 0.14f  /* 角速度低通滤波系数 */

/* ========== 8路循迹传感器 (低电平有效) ========== */
#define LINE_SENSOR_COUNT 8 /* 传感器数量 */
#define LINE_ACTIVE_LOW 1   /* 1=检测到黑线时输出低电平 */

#define LINE_S1_PORT GPIOA
#define LINE_S1_PIN GPIO_Pin_10
#define LINE_S2_PORT GPIOA
#define LINE_S2_PIN GPIO_Pin_11
#define LINE_S3_PORT GPIOA
#define LINE_S3_PIN GPIO_Pin_12
#define LINE_S4_PORT GPIOB
#define LINE_S4_PIN GPIO_Pin_3
#define LINE_S5_PORT GPIOB
#define LINE_S5_PIN GPIO_Pin_4
#define LINE_S6_PORT GPIOB
#define LINE_S6_PIN GPIO_Pin_9
#define LINE_S7_PORT GPIOB
#define LINE_S7_PIN GPIO_Pin_11
#define LINE_S8_PORT GPIOC
#define LINE_S8_PIN GPIO_Pin_13

/* ========== 串口通信 (USART2) ========== */
#define COMM_USART USART2
#define COMM_USART_IRQn USART2_IRQn
#define COMM_USART_RCC RCC_APB1Periph_USART2
#define COMM_TX_PORT GPIOA
#define COMM_TX_PIN GPIO_Pin_2
#define COMM_RX_PORT GPIOA
#define COMM_RX_PIN GPIO_Pin_3
#define COMM_BAUDRATE 115200  /* 波特率 */
#define COMM_TX_BUF_SIZE 2048 /* 发送缓冲区 */
#define COMM_RX_BUF_SIZE 64   /* 接收缓冲区 */

/* ========== 系统定时器 (TIM4, 1ms 节拍) ========== */
#define SYS_TIM TIM4
#define SYS_TIM_IRQn TIM4_IRQn
#define SYS_TIM_RCC RCC_APB1Periph_TIM4
#define SYS_TIM_PRESCALER 72
#define SYS_TIM_PERIOD 1000

/* ========== OLED 显示屏 (软件I2C) ========== */
#define OLED_SCL_PORT GPIOB
#define OLED_SCL_PIN GPIO_Pin_7
#define OLED_SDA_PORT GPIOB
#define OLED_SDA_PIN GPIO_Pin_8

/* ========== 按键 (PB5, 低电平有效, 内部上拉) ========== */
#define KEY1_PORT GPIOB
#define KEY1_PIN GPIO_Pin_5
#define KEY_LONG_PRESS_MS 1000 /* 长按判定时间(ms) */
#define KEY_DEBOUNCE_MS 20     /* 消抖时间(ms) */

/* ========== 控制周期 ========== */
#define CONTROL_PERIOD_MS 10          /* 主控制循环周期(ms), 速度环+巡线环都在此周期运行 */
#define HEADING_LOOP_MS 30            /* 航向环执行间隔(ms) */
#define TELEMETRY_PERIOD_MS 20        /* 串口遥测上报间隔(ms) */
#define IMU_READ_PERIOD_MS 20         /* IMU 读取间隔(ms) */
#define EXP_HOST_SYNC_TIMEOUT_MS 4000 /* py 端实验编号接管的租期(ms), 超时后板子不再自动递增编号 */

/*
 * ============================================================================
 *   PID 调参指南
 * ============================================================================
 *
 * 本车有两种运行模式，每种模式包含两个 PID 环：
 *
 * 【直线模式】 (MODE_STRAIGHT)
 *   速度环: 编码器 → PID → pwmCore (基础油门)
 *     - KP: 速度偏差的比例响应。偏大→电机抖动；偏小→加速慢
 *     - KI: 消除稳态误差。偏大→积分饱和震荡；偏小→长期有速度偏差
 *     - KD: 一般设0，编码器噪声大时D项会引入抖动
 *   航向环: IMU偏航角 → PID → 差速 (headingDiffPWM)
 *     - KP: 偏航角偏差的比例修正。偏大→蛇形走位；偏小→跑偏修正慢
 *     - KI: 消除长期偏航。偏大→过冲；偏小→持续微偏
 *     - KD: 使用陀螺仪角速度作为阻尼。偏大→响应迟钝；偏小→过冲
 *
 * 【循迹模式】 (MODE_TRACK)
 *   速度环: 编码器 → PID → pwmCore (基础油门)
 *     - 结构沿用当前工程的速度外环
 *   巡线环: 8路传感器状态 → bearingDev → PD差速
 *     - 控制骨架参考 5 路模板
 *     - 8 路只用于更细的偏差分层，不再保留旧实验状态机
 *
 * 【旋进模式】 (MODE_SPIN)
 *   仅保留最小可运行占位：
 *     - 短按启动后固定原地旋转
 *     - 长按仅在停止时切换模式
 *
 * 调参步骤建议：
 *   1. 先调速度环(SKP/SKI)，让小车能平稳地以目标速度直行
 *   2. 直线模式调航向环(AKP/AKD)，让小车走直线不跑偏
 *   3. 循迹模式先调 LINE_KP，再补 LINE_KD
 *   4. 若回头弯仍容易丢线，再调转角确认和转角 PWM
 *
 * 串口实时调参命令 (运行中可改)：
 *   #SKP=1.5   速度环 KP       #AKP=6.0   航向环 KP
 *   #SKI=0.1   速度环 KI       #AKI=0.06  航向环 KI
 *   #SKD=0.0   速度环 KD       #AKD=3.2   航向环 KD
 *   #LKP=24    巡线环 KP       #LKD=60    巡线环 KD
 *   #SPD=10    当前模式目标速度   #HTR=0.15  航向偏置(度)
 *   #STAT!     查询当前所有PID参数
 * ============================================================================
 */

/* ========== 直线模式 PID 默认参数 ========== */
/* 速度环: 编码器反馈 → pwmCore */
#define PID_STRAIGHT_SPEED_TARGET 10.0f /* 直线模式目标速度 (串口 #SPD= 可运行时修改) */
#define PID_STRAIGHT_SPEED_KP 1.50f     /* 比例: 速度误差→油门补偿 */
#define PID_STRAIGHT_SPEED_KI 0.10f     /* 积分: 消除稳态速度偏差 */
#define PID_STRAIGHT_SPEED_KD 0.0f      /* 微分: 通常设0(编码器噪声) */
/* 航向环: IMU偏航角 → 差速 */
#define PID_STRAIGHT_HEADING_KP 6.00f  /* 比例: 偏航角→差速修正 */
#define PID_STRAIGHT_HEADING_KI 0.060f /* 积分: 消除长期偏航 */
#define PID_STRAIGHT_HEADING_KD 3.20f  /* 微分: 陀螺仪角速度阻尼 */
#define HEADING_TRIM 0.15f             /* 航向偏置(度): 正值=向右补偿左偏 */
#define HEADING_INTEGRAL_ZONE 1.5f     /* 积分区(度): 误差在此范围内用全KI, 超出衰减 */

/* ========== 循迹模式 PID 默认参数 ========== */
/* 速度环: 编码器反馈 → pwmCore (结构同直线, 参数可独立调) */
#define PID_TRACK_SPEED_TARGET 50.0f /* 循迹模式目标速度 (串口 #SPD= 可运行时修改) */
#define PID_TRACK_SPEED_KP 1.50f     /* 比例 */
#define PID_TRACK_SPEED_KI 0.10f     /* 积分 */
#define PID_TRACK_SPEED_KD 0.0f      /* 微分 */
/* 巡线环: bearingDev → PD差速 (仅PD, 无积分)
 * 参考 5 路模板的差速思路，按 421 的 PWM 尺度重标定。 */
#define PID_TRACK_LINE_KP 34.0f
#define PID_TRACK_LINE_KD 28.0f

/* ========== 循迹行为参数 ========== */
#define TRACK_PWM_MAX 400
#define TRACK_PWM_MIN 0
#define TRACK_BASE_PWM_MIN 140
#define TRACK_BASE_PWM_MAX 330
#define TRACK_CORNER_BASE_PWM_MAX 235
#define TRACK_DEV_PWM_MAX 170
#define TRACK_EDGE_BASE_PWM_MAX 210
#define TRACK_EDGE_DEV_PWM_MAX 240
#define TRACK_SHARP_TURN_DEV 4

#define TRACK_POS_CENTER_DEADBAND 40
#define TRACK_POS_NEAR_THRESHOLD 90
#define TRACK_POS_MID_THRESHOLD 180
#define TRACK_POS_EDGE_THRESHOLD 270
#define TRACK_POSITION_TRIM 70 /* 正值=让循迹更愿意向右修正, 用于抵消整车左偏 */

#define TRACK_DEFAULT_CROSSINGS 4
#define TRACK_CROSS_MIN_COUNT 6
#define TRACK_CROSS_CONFIRM_TICKS 2
#define TRACK_WIDE_PATTERN_COUNT 5
#define TRACK_CROSS_FILTER 100
#define TRACK_DTERM_STEP_CLAMP 1
#define TRACK_DTERM_WIDE_CLAMP 1
#define TRACK_CENTER_BEARING_SLEW 1
#define TRACK_NORMAL_BEARING_SLEW 4
#define TRACK_STRAIGHT_ARM_BEARING 4
#define TRACK_STRAIGHT_ENTER_TICKS 4
#define TRACK_STRAIGHT_HOLD_TICKS 8
#define TRACK_STRAIGHT_POS_THRESHOLD 70
#define TRACK_STRAIGHT_DEV_PWM_MAX 80
#define TRACK_STRAIGHT_CENTER_SCALE_PCT 35
#define TRACK_STRAIGHT_YAW_RATE_ARM_DEG 90.0f
#define TRACK_STRAIGHT_YAW_RATE_ENTER_DEG 35.0f
#define TRACK_STRAIGHT_POS_DELTA_THRESHOLD 95
#define TRACK_STRAIGHT_BIT_DELTA_ARM 2
#define TRACK_STRAIGHT_BIT_DELTA_STABLE 1
#define TRACK_CURVE_ENTER_TICKS 2
#define TRACK_CURVE_EXIT_TICKS 4
#define TRACK_CURVE_BASE_PWM_MAX 265
#define TRACK_CURVE_DEV_PWM_MAX 240
#define TRACK_CURVE_YAW_RATE_ENTER_DEG 65.0f
#define TRACK_CURVE_YAW_RATE_EXIT_DEG 24.0f
#define TRACK_CURVE_POS_ENTER_THRESHOLD 115
#define TRACK_CURVE_POS_EXIT_THRESHOLD 55
#define TRACK_CURVE_BIT_DELTA_ENTER 2
#define TRACK_CURVE_BIT_DELTA_EXIT 1
#define TRACK_CORNER_STRONG_SIDE_HITS 3
#define TRACK_CORNER_OPPOSITE_MAX_HITS 2

#define TRACK_CORNER_CONFIRM_TICKS 4
#define TRACK_CORNER_FAST_CONFIRM_TICKS 1
#define TRACK_LOSS_ENTER_TICKS 4
#define TRACK_LOSS_FORCE_CORNER_TICKS 28
#define TRACK_LOSS_FORCE_REQUIRE_REF 1
#define TRACK_LOSS_SEARCH_BEARING 5
#define TRACK_LOSS_HOLD_BASE_PWM_MAX 210
#define TRACK_LOSS_HOLD_DEV_PWM_MAX 100
#define TRACK_LOSS_SEARCH_BASE_PWM_MAX 190
#define TRACK_LOSS_SEARCH_DEV_PWM_MAX 120
#define TRACK_OVERRUN_LIMIT_TICKS 100
#define TRACK_TURN_PWM 220
#define TRACK_TURN_PWM_MIN 150
#define TRACK_CORNER_EXIT_POS_THRESHOLD 80
#define TRACK_CORNER_TIMEOUT_MS 1000
#define TRACK_CORNER_RESUME_SPEED_MAX 20.0f
#define TRACK_CORNER_RECOVER_TICKS 10
#define TRACK_CENTER_LOCK_TICKS 6
#define TRACK_CENTER_LOCK_DEV_PWM_MAX 48
#define TRACK_RECOVER_BASE_PWM_MAX 235
#define TRACK_RECOVER_DEV_PWM_MAX 84
#define TRACK_CORNER_FLIP_YAW_DEG 30.0f

/* ========== 旋进模式占位参数 ========== */
#define SPIN_TURN_PWM 180

/* ========== 速度斜坡 ========== */
#define SPEED_TARGET_DEFAULT 10.0f  /* 初始目标速度 (仅用于上电初始化, 切换模式时会覆盖) */
#define SPEED_ENTRY 9.2f            /* 起步速度 = 死区/前馈增益, 确保不卡死区 */
#define SPEED_RAMP_RATE 20.0f       /* 加速斜率(速度单位/秒) */
#define SPEED_CORE_SLEW_STEP 18     /* 速度环 pwmCore 每拍最大变化量, 压住 exp123 这类直线段油门抽动 */
#define SPEED_OUTPUT_LIMIT 600.0f   /* 速度环PWM输出上限 */
#define SPEED_FEEDFORWARD_GAIN 8.7f /* 前馈增益: target*ff≈87, 刚好超过死区 */
#define PID_DERIV_LPF_ALPHA 0.15f   /* 微分项低通滤波系数 */
#define HEADING_INTEGRAL_ATTEN 0.3f /* 积分衰减: 误差超过INTEGRAL_ZONE时KI乘以此系数 */

#endif
