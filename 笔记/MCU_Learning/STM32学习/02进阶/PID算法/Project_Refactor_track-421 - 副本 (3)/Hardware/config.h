#ifndef __CONFIG_H
#define __CONFIG_H

#include "stm32f10x.h"

/* ========== 控制模式 ========== */
typedef enum
{
    MODE_STRAIGHT = 0,
    MODE_TRACK    = 1,
    MODE_SPIN     = 2
} ControlMode_t;

typedef enum
{
    SYS_STOP     = 0,
    SYS_STRAIGHT = 1,
    SYS_TRACKING = 2,
    SYS_SPINNING = 3
} SystemState_t;

typedef enum
{
    KEY_EVENT_NONE        = 0,
    KEY_EVENT_SHORT_PRESS = 1,
    KEY_EVENT_LONG_PRESS  = 2
} KeyEvent_t;

/* ========== 电机引脚 (TB6612 驱动 + TIM1 PWM) ========== */
#define MOTOR_STBY_PORT GPIOB
#define MOTOR_STBY_PIN  GPIO_Pin_0

#define MOTOR_AIN1_PORT GPIOA
#define MOTOR_AIN1_PIN  GPIO_Pin_4
#define MOTOR_AIN2_PORT GPIOA
#define MOTOR_AIN2_PIN  GPIO_Pin_5

#define MOTOR_BIN1_PORT GPIOB
#define MOTOR_BIN1_PIN  GPIO_Pin_1
#define MOTOR_BIN2_PORT GPIOB
#define MOTOR_BIN2_PIN  GPIO_Pin_10

#define MOTOR_PWM_TIM       TIM1
#define MOTOR_PWM_PERIOD    1000
#define MOTOR_PWM_PRESCALER 72
#define MOTOR_PWM_MAX       600
#define MOTOR_DIFF_MAX      200
#define MOTOR_DEADZONE      80

/* ========== 编码器 (TIM2 左轮, TIM3 右轮) ========== */
#define ENC_LEFT_TIM         TIM2
#define ENC_RIGHT_TIM        TIM3
#define ENC_IC_FILTER        12
#define ENC_PPR              11
#define ENC_RATIO            30
#define ENC_QUAD_MULT        4
#define ENC_LEFT_SIGN        (-1)
#define ENC_RIGHT_SIGN       1
#define ENC_MAX_DELTA        2600
#define ENC_SPEED_LPF_ALPHA  0.3f

/* ========== BNO085 九轴 IMU (软件 I2C) ========== */
#define BNO_SCL_PORT GPIOB
#define BNO_SCL_PIN  GPIO_Pin_12
#define BNO_SDA_PORT GPIOB
#define BNO_SDA_PIN  GPIO_Pin_13
#define BNO_RST_PORT GPIOB
#define BNO_RST_PIN  GPIO_Pin_14
#define BNO_INT_PORT GPIOA
#define BNO_INT_PIN  GPIO_Pin_15

#define BNO_ADDR_DEFAULT 0x4B
#define BNO_ADDR_ALT     0x4A
#define BNO_ADDR_DOC_DEF 0x28
#define BNO_ADDR_DOC_ALT 0x29

#define BNO_REPORT_INTERVAL_US 20000UL
#define BNO_BOOT_DELAY_MS      80u
#define BNO_PRESENT_TIMEOUT_MS 1000u
#define BNO_PACKET_MAX         384u

#define BNO_YAW_JUMP_REJECT_DEG 45.0f
#define BNO_YAW_RATE_LIMIT_DPS  180.0f
#define BNO_YAW_RATE_LPF_ALPHA  0.14f

/* ========== 8 路循迹传感器 (低电平有效) ========== */
#define LINE_SENSOR_COUNT 8
#define LINE_ACTIVE_LOW   1

#define LINE_S1_PORT GPIOA
#define LINE_S1_PIN  GPIO_Pin_10
#define LINE_S2_PORT GPIOA
#define LINE_S2_PIN  GPIO_Pin_11
#define LINE_S3_PORT GPIOA
#define LINE_S3_PIN  GPIO_Pin_12
#define LINE_S4_PORT GPIOB
#define LINE_S4_PIN  GPIO_Pin_3
#define LINE_S5_PORT GPIOB
#define LINE_S5_PIN  GPIO_Pin_4
#define LINE_S6_PORT GPIOB
#define LINE_S6_PIN  GPIO_Pin_9
#define LINE_S7_PORT GPIOB
#define LINE_S7_PIN  GPIO_Pin_11
#define LINE_S8_PORT GPIOC
#define LINE_S8_PIN  GPIO_Pin_13

/* ========== 串口通信 (USART2) ========== */
#define COMM_USART      USART2
#define COMM_USART_IRQn USART2_IRQn
#define COMM_USART_RCC  RCC_APB1Periph_USART2
#define COMM_TX_PORT    GPIOA
#define COMM_TX_PIN     GPIO_Pin_2
#define COMM_RX_PORT    GPIOA
#define COMM_RX_PIN     GPIO_Pin_3
#define COMM_BAUDRATE   115200
#define COMM_TX_BUF_SIZE 2048
#define COMM_RX_BUF_SIZE 64

/* ========== 系统定时器 (TIM4, 1ms 节拍) ========== */
#define SYS_TIM       TIM4
#define SYS_TIM_IRQn  TIM4_IRQn
#define SYS_TIM_RCC   RCC_APB1Periph_TIM4
#define SYS_TIM_PRESCALER 72
#define SYS_TIM_PERIOD    1000

/* ========== OLED 显示屏 (软件 I2C) ========== */
#define OLED_SCL_PORT GPIOB
#define OLED_SCL_PIN  GPIO_Pin_7
#define OLED_SDA_PORT GPIOB
#define OLED_SDA_PIN  GPIO_Pin_8

/* ========== 按键 (PB5, 低电平有效, 内部上拉) ========== */
#define KEY1_PORT         GPIOB
#define KEY1_PIN          GPIO_Pin_5
#define KEY_LONG_PRESS_MS 1000
#define KEY_DEBOUNCE_MS   20

/* ========== 控制周期 ========== */
#define CONTROL_PERIOD_MS         10
#define HEADING_LOOP_MS           30
#define TELEMETRY_PERIOD_MS       20
#define TELEMETRY_IDLE_PERIOD_MS  200
#define IMU_READ_PERIOD_MS        20
#define EXP_HOST_SYNC_TIMEOUT_MS  4000

/* ========== 直线模式 PID 默认参数 ========== */
#define PID_STRAIGHT_SPEED_TARGET   10.0f
#define PID_STRAIGHT_SPEED_KP       1.50f
#define PID_STRAIGHT_SPEED_KI       0.10f
#define PID_STRAIGHT_SPEED_KD       0.0f

#define PID_STRAIGHT_HEADING_KP     6.00f
#define PID_STRAIGHT_HEADING_KI     0.060f
#define PID_STRAIGHT_HEADING_KD     3.20f
#define HEADING_TRIM                0.15f
#define HEADING_INTEGRAL_ZONE       1.5f
#define HEADING_INTEGRAL_ATTEN      0.3f

/* ========== TRACK 模式默认参数 ========== */
#define PID_TRACK_SPEED_TARGET      25.0f
#define PID_TRACK_SPEED_KP          1.50f
#define PID_TRACK_SPEED_KI          0.10f
#define PID_TRACK_SPEED_KD          0.0f

/* 5 路模板风格: bearing_dev 进入 PD, 输出左右轮差速 */
#define PID_TRACK_LINE_KP           12.5f
#define PID_TRACK_LINE_KD           9.8f

/* 动态循迹 PD:
   不是在线学习，而是按偏差大小做增益调度。
   中线附近减小增益抑制蛇形，偏差变大时再逐步抬高转向强度。 */
#define TRACK_DYNAMIC_PID_ENABLE    1
#define TRACK_GAIN_CENTER_MAX_DEV   1
#define TRACK_GAIN_MID_MAX_DEV      2

#define TRACK_CENTER_KP_SCALE       0.70f
#define TRACK_CENTER_KD_SCALE       0.75f
#define TRACK_CENTER_DEV_RATIO      0.30f

#define TRACK_MID_KP_SCALE          1.00f
#define TRACK_MID_KD_SCALE          0.85f
#define TRACK_MID_DEV_RATIO         0.52f

#define TRACK_EDGE_KP_SCALE         1.00f
#define TRACK_EDGE_KD_SCALE         0.75f
#define TRACK_EDGE_DEV_RATIO        0.58f

#define TRACK_LINE_POS_UNIT         50
#define TRACK_LINE_POS_CENTER_MAX   60
#define TRACK_LINE_POS_SMALL_MAX    120
#define TRACK_LINE_POS_MEDIUM_MAX   190
#define TRACK_LINE_POS_LARGE_MAX    280
#define TRACK_CENTER_SINGLE_HOLD_TICKS 1
#define TRACK_CENTER_SINGLE_POS     18
#define TRACK_CENTER_INNER_MIN_POS  110
#define TRACK_CENTER_INNER_BOOST    1.25f
#define TRACK_CENTER_DIRECT_SMALL_RATIO 0.19f
#define TRACK_CENTER_DIRECT_SMALL_MIN   22
#define TRACK_CENTER_DIRECT_MID_RATIO   0.465f
#define TRACK_CENTER_DIRECT_MID_MIN     62
#define TRACK_EDGE_DIRECT_RATIO         0.60f
#define TRACK_EDGE_DIRECT_MIN           83
#define TRACK_RECENTER_DECAY_STEP       22

/* TRACK 内环改为“连续位置误差 + 滤波导数”。
   bearing_dev 仍保留给状态判定和遥测，但差速输出不再直接吃离散跳变。 */
#define TRACK_CTRL_CENTER_DEADBAND    45.0f
#define TRACK_CTRL_CENTER_SLOPE_RATIO 0.35f
#define TRACK_CTRL_ERROR_SCALE        90.0f
#define TRACK_CTRL_POS_FILTER_ALPHA   0.56f
#define TRACK_CTRL_D_FILTER_ALPHA     0.46f
#define TRACK_CTRL_DEV_STEP_LIMIT     26
#define TRACK_CTRL_OFFCENTER_BOOST    1.16f

#define TRACK_DEV_MAX_RATIO         0.58f
#define TRACK_PWM_MAX               480
#define TRACK_PWM_MIN               0
#define TRACK_EDGE_BASE_PWM_MAX     200
#define TRACK_EDGE_BEARING_MIN      4

#define TRACK_LOST_CONFIRM_TICKS    3
#define TRACK_LOST_FAST_CONFIRM_TICKS 2
#define TRACK_SEARCH_BLIND_TICKS    1
#define TRACK_SEARCH_ARC_TICKS      3
#define TRACK_SEARCH_TIMEOUT_TICKS  18
#define TRACK_SEARCH_ARC_PWM_FAST   220
#define TRACK_SEARCH_ARC_PWM_SLOW   140
#define TRACK_SEARCH_TURN_PWM_FAST  240
#define TRACK_SEARCH_TURN_PWM_SLOW  150
#define TRACK_RECOVER_TICKS         8
#define TRACK_RESUME_SPEED_MAX      24.0f

#define TRACK_DEFAULT_CROSSINGS     4
#define TRACK_CROSS_MIN_ACTIVE      5
#define TRACK_CROSS_RELEASE_TICKS   100

/* ========== SPIN 占位模式 ========== */
#define SPIN_PLACEHOLDER_PWM        0

/* ========== 速度斜坡 ========== */
#define SPEED_TARGET_DEFAULT        10.0f
#define SPEED_ENTRY                 9.2f
#define SPEED_RAMP_RATE             20.0f
#define SPEED_CORE_SLEW_STEP        18
#define SPEED_OUTPUT_LIMIT          600.0f
#define SPEED_FEEDFORWARD_GAIN      8.7f
#define PID_DERIV_LPF_ALPHA         0.15f

#endif
