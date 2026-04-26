#ifndef __CONFIG_H
#define __CONFIG_H

#include "stm32f10x.h"

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

/* TB6612 + TIM1 PWM */
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
#define MOTOR_BRAKE_PWM     600
#define MOTOR_DIFF_MAX      320
#define MOTOR_DEADZONE      80
#define MOTOR_LEFT_DIR_SIGN (-1)
#define MOTOR_RIGHT_DIR_SIGN (-1)

/* Current car wiring: left/right encoder plugs are swapped on the board,
 * so firmware maps left control feedback to TIM3 and right to TIM2. */
#define ENC_LEFT_TIM         TIM3
#define ENC_RIGHT_TIM        TIM2
#define ENC_IC_FILTER        12
#define ENC_PPR              11
#define ENC_RATIO            30
#define ENC_QUAD_MULT        4
#define ENC_LEFT_SIGN        (-1)
#define ENC_RIGHT_SIGN       1
#define ENC_MAX_DELTA        2600
#define ENC_SPEED_LPF_ALPHA  0.28f

/* BNO085 software I2C */
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

/* 8-way track sensor:
 * bit0~bit7 correspond to 74HC4051 Y0~Y7, i.e. the 8-way module A1~A8.
 */
#define LINE_SENSOR_COUNT 8
#define LINE_ACTIVE_LOW   1

#define LINE_MUX_S0_PORT GPIOA
#define LINE_MUX_S0_PIN  GPIO_Pin_2
#define LINE_MUX_S1_PORT GPIOA
#define LINE_MUX_S1_PIN  GPIO_Pin_11
#define LINE_MUX_S2_PORT GPIOA
#define LINE_MUX_S2_PIN  GPIO_Pin_10
#define LINE_MUX_Z_PORT  GPIOA
#define LINE_MUX_Z_PIN   GPIO_Pin_3
#define LINE_MUX_SETTLE_CYCLES 180u

/* USART1 remap -> PB6/PB7 */
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
#define COMM_BAUDRATE        115200
#define COMM_TX_BUF_SIZE     2048
#define COMM_RX_BUF_SIZE     96

/* Buzzer: recommended active buzzer on free GPIO PA12 */
#define BUZZER_PORT          GPIOA
#define BUZZER_PIN           GPIO_Pin_12
#define BUZZER_GPIO_RCC      RCC_APB2Periph_GPIOA
#define BUZZER_ACTIVE_HIGH   1
#define STOP_ALERT_BEEP_MS   120u
#define STOP_ALERT_OLED_MS   2000u

/* TIM4 1ms system tick */
#define SYS_TIM            TIM4
#define SYS_TIM_IRQn       TIM4_IRQn
#define SYS_TIM_RCC        RCC_APB1Periph_TIM4
#define SYS_TIM_PRESCALER  72
#define SYS_TIM_PERIOD     1000

/* OLED soft I2C */
#define OLED_SCL_PORT GPIOB
#define OLED_SCL_PIN  GPIO_Pin_8
#define OLED_SDA_PORT GPIOB
#define OLED_SDA_PIN  GPIO_Pin_9

/* Key */
#define KEY1_PORT         GPIOB
#define KEY1_PIN          GPIO_Pin_5
#define KEY_LONG_PRESS_MS 1000
#define KEY_DEBOUNCE_MS   20

/* Scheduler */
#define CONTROL_PERIOD_MS         10u
#define HEADING_LOOP_MS           30u
#define TELEMETRY_PERIOD_MS       20u
#define TELEMETRY_IDLE_PERIOD_MS  200u
#define IMU_ENABLE                0
#define IMU_READ_PERIOD_MS        20u
#define EXP_HOST_SYNC_TIMEOUT_MS  4000u

/* Straight mode */
#define PID_STRAIGHT_SPEED_TARGET  12.0f
#define PID_STRAIGHT_SPEED_KP      1.50f
#define PID_STRAIGHT_SPEED_KI      0.10f
#define PID_STRAIGHT_SPEED_KD      0.00f

#define PID_STRAIGHT_HEADING_KP    6.00f
#define PID_STRAIGHT_HEADING_KI    0.050f
#define PID_STRAIGHT_HEADING_KD    3.20f
#define HEADING_TRIM               0.15f
#define HEADING_INTEGRAL_ZONE      1.5f
#define HEADING_INTEGRAL_ATTEN     0.30f

/* Track mode:
 * steering state machine follows the 5-sensor reference:
 * keep a remembered side hint, and when fully lost, turn back into the last seen side.
 * line PD uses the simple competition-style position PD core.
 */
#define PID_TRACK_SPEED_TARGET      30.0f
#define PID_TRACK_SPEED_KP          1.50f
#define PID_TRACK_SPEED_KI          0.10f
#define PID_TRACK_SPEED_KD          0.00f

#define PID_TRACK_LINE_KP           10.00f
#define PID_TRACK_LINE_KD           2.90f

#define TRACK_LINE_POS_CENTER_MAX   18
#define TRACK_LINE_POS_SMALL_MAX    95
#define TRACK_LINE_POS_MEDIUM_MAX   150
#define TRACK_LINE_POS_LARGE_MAX    235
#define TRACK_CONTROL_POS_UNIT      30.0f

#define TRACK_FOLLOW_DEADBAND       0.25f
#define TRACK_FOLLOW_POS_LPF_ALPHA  0.95f
#define TRACK_FOLLOW_D_LPF_ALPHA    0.86f
#define TRACK_FOLLOW_DEV_RATIO      1.72f
#define TRACK_DIFF_MAX              620
#define TRACK_DIFF_SLEW_STEP        26
#define TRACK_STEER_GAIN_SMALL      1.28f
#define TRACK_STEER_GAIN_MEDIUM     1.68f
#define TRACK_STEER_GAIN_LARGE      2.20f
#define TRACK_TURN_ENTRY_GAIN_BOOST 1.20f
#define TRACK_TURN_ENTRY_DIFF_BOOST 1.25f
#define TRACK_BASE_SCALE_SMALL      0.94f
#define TRACK_BASE_SCALE_MEDIUM     0.88f
#define TRACK_BASE_SCALE_LARGE      0.80f
#define TRACK_CROSS_BASE_SCALE      0.86f
#define TRACK_RECOVER_BASE_SCALE    1.00f
#define TRACK_RECOVER_MIN_BASE_SCALE 1.00f
#define TRACK_RECOVER_GAIN_BOOST    1.45f
#define TRACK_RECOVER_DIFF_BOOST    1.60f
#define TRACK_RECOVER_SLEW_BOOST    2.00f
#define TRACK_PIVOT_TRIGGER_ERROR   3.5f
#define TRACK_PIVOT_RECOVER_BASE    0.82f
#define TRACK_PIVOT_RECOVER_DIFF    1.85f
#define TRACK_STATIC_STEER_BIAS     0
/* Map semantic steering diff into this chassis' physical yaw direction.
 * Positive semantic diff means "steer right toward a right-side line". */
#define TRACK_FOLLOW_DIFF_SIGN      (-1)
/* Normalize sensor bits to the 5-sensor convention:
 * high bits = left side, low bits = right side. */
#define TRACK_SENSOR_REVERSED       1

#define TRACK_PWM_MAX               520
#define TRACK_PWM_MIN               0

#define TRACK_LOST_CONFIRM_TICKS    1u
#define TRACK_SEARCH_ARC_TICKS      0u
#define TRACK_SEARCH_TIMEOUT_TICKS  60u
#define TRACK_SEARCH_ARC_PWM_FAST   340u
#define TRACK_SEARCH_ARC_PWM_SLOW   230u
#define TRACK_SEARCH_TURN_PWM_FAST  480u
#define TRACK_SEARCH_TURN_PWM_SLOW  500u
#define TRACK_SEARCH_MIN_PIVOT_TICKS 12u
#define TRACK_SEARCH_REACQUIRE_TICKS 2u
#define TRACK_SEARCH_EXIT_HOLD_TICKS 2u
#define TRACK_CORNER_LATCH_TICKS    6u
#define TRACK_RECOVER_TICKS         14u

#define TRACK_DEFAULT_CROSSINGS     0u
#define TRACK_CROSS_MIN_ACTIVE      5u
#define TRACK_CROSS_RELEASE_TICKS   3u
#define TRACK_FULL_BLACK_STOP_DELAY_MS 20000u

/* Spin / manual turn placeholder */
#define SPIN_PLACEHOLDER_PWM        180
#define TURNBACK_PWM_FAST           210
#define TURNBACK_PWM_SLOW           140
#define TURNBACK_TARGET_COUNTS      150
#define TURNBACK_TIMEOUT_MS         1800u

/* Speed ramp and feedforward */
#define SPEED_TARGET_DEFAULT        12.0f
#define SPEED_ENTRY                 12.0f
#define SPEED_RAMP_RATE             36.0f
#define SPEED_CORE_SLEW_STEP        18
#define SPEED_OUTPUT_LIMIT          600.0f
#define SPEED_FEEDFORWARD_GAIN      8.0f

#endif
