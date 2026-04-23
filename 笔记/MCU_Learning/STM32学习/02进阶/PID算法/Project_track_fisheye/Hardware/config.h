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
#define BNO_PRESENT_TIMEOUT_MS 250u
#define BNO_PACKET_MAX         384u

#define BNO_YAW_JUMP_REJECT_DEG 45.0f
#define BNO_YAW_RATE_LIMIT_DPS  180.0f
#define BNO_YAW_RATE_LPF_ALPHA  0.14f

/* ========== 8 路循迹输入 (74HC4051 扫描 A3~A10, bit0~bit7 = Y0~Y7) ========== */
#define LINE_SENSOR_COUNT 8

#define LINE_ACTIVE_LOW 0

#define LINE_MUX_S0_PORT GPIOA
#define LINE_MUX_S0_PIN  GPIO_Pin_2
#define LINE_MUX_S1_PORT GPIOA
#define LINE_MUX_S1_PIN  GPIO_Pin_11
#define LINE_MUX_S2_PORT GPIOA
#define LINE_MUX_S2_PIN  GPIO_Pin_10
#define LINE_MUX_Z_PORT  GPIOA
#define LINE_MUX_Z_PIN   GPIO_Pin_3
#define LINE_MUX_SETTLE_CYCLES 180u

/* ========== DAPlink / 命令串口 (USART1 重映射到 PB6/PB7) ========== */
#define COMM_USART           USART1
#define COMM_USART_IRQn      USART1_IRQn
#define COMM_USART_RCC       RCC_APB2Periph_USART1
#define COMM_USART_ON_APB2   1
#define COMM_USART_GPIO_RCC  (RCC_APB2Periph_GPIOB | RCC_APB2Periph_AFIO)
#define COMM_USART_REMAP     GPIO_Remap_USART1
#define COMM_TX_PORT         GPIOB
#define COMM_TX_PIN          GPIO_Pin_6
#define COMM_RX_PORT         GPIOB
#define COMM_RX_PIN          GPIO_Pin_7
#define COMM_BAUDRATE   115200
#define COMM_TX_BUF_SIZE 2048
#define COMM_RX_BUF_SIZE 64

/* ========== 系统定时器 (TIM4, 1ms 节拍) ========== */
#define SYS_TIM       TIM4
#define SYS_TIM_IRQn  TIM4_IRQn
#define SYS_TIM_RCC   RCC_APB1Periph_TIM4
#define SYS_TIM_PRESCALER 72
#define SYS_TIM_PERIOD    1000

/* ========== OLED 显示屏 (软件 I2C, PB8/PB9) ========== */
#define OLED_SCL_PORT GPIOB
#define OLED_SCL_PIN  GPIO_Pin_8
#define OLED_SDA_PORT GPIOB
#define OLED_SDA_PIN  GPIO_Pin_9

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
#define IMU_ENABLE                0
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
/* 调参建议（单主链 PD）
  1) 先降速度：先调 PID_TRACK_SPEED_TARGET，再谈循迹 PD。
     速度过高时，就算方向正确，也会因机械滞后和采样延迟出现左右摆。
  2) 先定 P，再补 D：先让 PID_TRACK_LINE_KP 达到“能快速回线”，
     再用 PID_TRACK_LINE_KD 压制过冲和左右来回摆动。
  3) 先调主链，再调辅参：优先顺序是 KP -> KD -> DEV_RATIO -> DEADBAND。 */
/* 速度环：影响基础推进力。
   如果速度目标太高，或者速度环太冲，小车会在循迹环还没来得及稳定前不断左右修正。
   对“轻微蛇形、越跑越晃”最有效的第一刀通常是先降速度目标。 */
#define PID_TRACK_SPEED_TARGET      44.0f
#define PID_TRACK_SPEED_KP          1.50f
#define PID_TRACK_SPEED_KI          0.10f
#define PID_TRACK_SPEED_KD          0.0f

/* 主循迹 PD：主链只保留单一位置误差与统一 KP/KD。
   PID_TRACK_LINE_KP：主纠偏力度。
     - 调大：回线更猛，但也更容易左右摆。
     - 调小：更稳，但弯道和大偏差回中会变慢。
   PID_TRACK_LINE_KD：主阻尼。
     - 调大：抑制回中过冲与左右抽动。
     - 过大：会让车变钝，出现看见偏差但修正迟疑。 */
#define PID_TRACK_LINE_KP           13.8f
#define PID_TRACK_LINE_KD           6.8f

/* 传感器位置映射：把 8 路灯映射到一条连续位置轴。
   TRACK_LINE_POS_STEP / TRACK_SENSOR_POS_TRIM_RANGE 主要影响“位置误差”的细腻程度。
   如果位置映射过陡，会让相邻灯切换时控制量变化偏大，更容易抖。 */
#define TRACK_LINE_POS_STEP         45
#define TRACK_SENSOR_POS_TRIM_RANGE 40.0f

#define TRACK_LINE_POS_S1           (-225)
#define TRACK_LINE_POS_S2           (-135)
#define TRACK_LINE_POS_S3           (-90)
#define TRACK_LINE_POS_S4           (-45)
#define TRACK_LINE_POS_S5           45
#define TRACK_LINE_POS_S6           90
#define TRACK_LINE_POS_S7           135
#define TRACK_LINE_POS_S8           225

/* 分区阈值：决定当前位置误差落在哪个区间。
   这些阈值只用于状态判断、找线与遥测分级，不再用于分段增益调度。 */
#define TRACK_LINE_POS_CENTER_MAX   18
#define TRACK_LINE_POS_SMALL_MAX    95
#define TRACK_LINE_POS_MEDIUM_MAX   150
#define TRACK_LINE_POS_LARGE_MAX    235

/* 单主链控制参数：
   TRACK_FOLLOW_DEADBAND：中心死区，小偏差直接视为 0。
   TRACK_FOLLOW_ERROR_SCALE：位置误差缩放，决定 linePos 到控制量的换算尺度。
   TRACK_FOLLOW_POS_LPF_ALPHA：位置低通。
   TRACK_FOLLOW_D_LPF_ALPHA：导数低通。
   TRACK_FOLLOW_DEV_RATIO：差速输出占基础 PWM 的最大比例。
   TRACK_FOLLOW_DEV_STEP_LIMIT：每周期差速变化限幅。
   TRACK_FOLLOW_BASE_MIN_PWM：大偏差时基础速度下压到的最小 PWM。 */
#define TRACK_FOLLOW_DEADBAND        6.0f
#define TRACK_FOLLOW_ERROR_SCALE     64.0f
#define TRACK_FOLLOW_POS_LPF_ALPHA   0.60f
#define TRACK_FOLLOW_D_LPF_ALPHA     0.38f
#define TRACK_FOLLOW_DEV_RATIO       0.64f
#define TRACK_FOLLOW_DEV_STEP_LIMIT  44
#define TRACK_FOLLOW_BASE_MIN_PWM    250
#define TRACK_STATIC_STEER_BIAS         0

/* 输出限制：限制左右轮差速与 PWM 上下限。
   单主链中左右轮都保持正向，差速通过 TRACK_FOLLOW_DEV_RATIO 限制。 */
#define TRACK_PWM_MAX               480
#define TRACK_PWM_MIN               0
#define TRACK_EDGE_BEARING_MIN      4

#define TRACK_LOST_CONFIRM_TICKS    3
#define TRACK_LOST_FAST_CONFIRM_TICKS 2
#define TRACK_SEARCH_BLIND_TICKS    0
#define TRACK_SEARCH_ARC_TICKS      3
#define TRACK_SEARCH_TIMEOUT_TICKS  18
#define TRACK_SEARCH_ARC_PWM_FAST   240
#define TRACK_SEARCH_ARC_PWM_SLOW   160
#define TRACK_SEARCH_TURN_PWM_FAST  300
#define TRACK_SEARCH_TURN_PWM_SLOW  190
#define TRACK_SEARCH_SIDE_EXIT_TICKS 2
#define TRACK_RECOVER_TICKS         10
#define TRACK_RESUME_SPEED_MIN      28.0f
#define TRACK_RESUME_SPEED_BOOST    8.0f
#define TRACK_RESUME_SPEED_MAX      44.0f

#define TRACK_DEFAULT_CROSSINGS     0
#define TRACK_CROSS_MIN_ACTIVE      5
#define TRACK_CROSS_RELEASE_TICKS   100

/* ========== SPIN 占位模式 ========== */
#define SPIN_PLACEHOLDER_PWM        180
#define TURNBACK_SPIN_PWM_FAST      180
#define TURNBACK_SPIN_PWM_SLOW      120
#define TURNBACK_TARGET_DEG         180.0f
#define TURNBACK_ARM_ERR_DEG        40.0f
#define TURNBACK_STOP_ERR_DEG       18.0f
#define TURNBACK_OVERSHOOT_DEG      70.0f
#define TURNBACK_CAPTURE_TICKS      2u
#define TURNBACK_TIMEOUT_MS         4500u

/* ========== 速度斜坡 ========== */
#define SPEED_TARGET_DEFAULT        10.0f
#define SPEED_ENTRY                 12.0f
#define SPEED_RAMP_RATE             36.0f
#define SPEED_CORE_SLEW_STEP        18
#define SPEED_OUTPUT_LIMIT          600.0f
#define SPEED_FEEDFORWARD_GAIN      8.7f
#define PID_DERIV_LPF_ALPHA         0.15f

#endif
