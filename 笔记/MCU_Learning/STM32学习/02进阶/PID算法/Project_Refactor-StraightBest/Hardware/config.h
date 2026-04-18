#ifndef __CONFIG_H
#define __CONFIG_H

#include "stm32f10x.h"

/* ========== Control Mode ========== */
typedef enum {
    MODE_STRAIGHT = 0,
    MODE_TRACK    = 1
} ControlMode_t;

typedef enum {
    SYS_STOP      = 0,
    SYS_STRAIGHT  = 1,
    SYS_TRACKING  = 2
} SystemState_t;

typedef enum {
    KEY_EVENT_NONE        = 0,
    KEY_EVENT_SHORT_PRESS = 1,
    KEY_EVENT_LONG_PRESS  = 2
} KeyEvent_t;

/* ========== Motor Pins (TB6612 + TIM1) ========== */
#define MOTOR_STBY_PORT     GPIOB
#define MOTOR_STBY_PIN      GPIO_Pin_0

#define MOTOR_AIN1_PORT     GPIOA
#define MOTOR_AIN1_PIN      GPIO_Pin_4
#define MOTOR_AIN2_PORT     GPIOA
#define MOTOR_AIN2_PIN      GPIO_Pin_5

#define MOTOR_BIN1_PORT     GPIOB
#define MOTOR_BIN1_PIN      GPIO_Pin_1
#define MOTOR_BIN2_PORT     GPIOB
#define MOTOR_BIN2_PIN      GPIO_Pin_10

#define MOTOR_PWM_TIM       TIM1
#define MOTOR_PWM_PERIOD    100
#define MOTOR_PWM_PRESCALER 720
#define MOTOR_PWM_MAX       60
#define MOTOR_DIFF_MAX      20
#define MOTOR_DEADZONE      8

/* ========== Encoder (TIM2 left, TIM3 right) ========== */
#define ENC_LEFT_TIM        TIM2
#define ENC_RIGHT_TIM       TIM3
#define ENC_IC_FILTER        12

#define ENC_PPR              11
#define ENC_RATIO            30
#define ENC_QUAD_MULT        4
#define ENC_LEFT_SIGN       (-1)
#define ENC_RIGHT_SIGN       1
#define ENC_MAX_DELTA        2600
#define ENC_SPEED_LPF_ALPHA  0.3f

/* ========== BNO085 IMU (Software I2C) ========== */
#define BNO_SCL_PORT        GPIOB
#define BNO_SCL_PIN         GPIO_Pin_12
#define BNO_SDA_PORT        GPIOB
#define BNO_SDA_PIN         GPIO_Pin_13
#define BNO_RST_PORT        GPIOB
#define BNO_RST_PIN         GPIO_Pin_14
#define BNO_INT_PORT        GPIOA
#define BNO_INT_PIN         GPIO_Pin_15

#define BNO_ADDR_DEFAULT    0x4B
#define BNO_ADDR_ALT        0x4A
#define BNO_ADDR_DOC_DEF    0x28
#define BNO_ADDR_DOC_ALT    0x29

#define BNO_REPORT_INTERVAL_US  20000UL
#define BNO_BOOT_DELAY_MS       80u
#define BNO_PRESENT_TIMEOUT_MS  1000u
#define BNO_PACKET_MAX          384u

#define BNO_YAW_JUMP_REJECT_DEG  45.0f
#define BNO_YAW_RATE_LIMIT_DPS   180.0f
#define BNO_YAW_RATE_LPF_ALPHA   0.14f

/* ========== Line Tracking Sensors (8-channel, active low) ========== */
#define LINE_SENSOR_COUNT    8
#define LINE_ACTIVE_LOW      1

#define LINE_S1_PORT  GPIOA
#define LINE_S1_PIN   GPIO_Pin_10
#define LINE_S2_PORT  GPIOA
#define LINE_S2_PIN   GPIO_Pin_11
#define LINE_S3_PORT  GPIOA
#define LINE_S3_PIN   GPIO_Pin_12
#define LINE_S4_PORT  GPIOB
#define LINE_S4_PIN   GPIO_Pin_3
#define LINE_S5_PORT  GPIOB
#define LINE_S5_PIN   GPIO_Pin_4
#define LINE_S6_PORT  GPIOB
#define LINE_S6_PIN   GPIO_Pin_9
#define LINE_S7_PORT  GPIOB
#define LINE_S7_PIN   GPIO_Pin_11
#define LINE_S8_PORT  GPIOC
#define LINE_S8_PIN   GPIO_Pin_13

/* ========== UART Communication (USART2) ========== */
#define COMM_USART          USART2
#define COMM_USART_IRQn     USART2_IRQn
#define COMM_USART_RCC      RCC_APB1Periph_USART2
#define COMM_TX_PORT        GPIOA
#define COMM_TX_PIN         GPIO_Pin_2
#define COMM_RX_PORT        GPIOA
#define COMM_RX_PIN         GPIO_Pin_3
#define COMM_BAUDRATE       115200
#define COMM_TX_BUF_SIZE    2048
#define COMM_RX_BUF_SIZE    64

/* ========== System Timer (TIM4 1ms tick) ========== */
#define SYS_TIM             TIM4
#define SYS_TIM_IRQn        TIM4_IRQn
#define SYS_TIM_RCC         RCC_APB1Periph_TIM4
#define SYS_TIM_PRESCALER   72
#define SYS_TIM_PERIOD      1000

/* ========== OLED (Software I2C) ========== */
#define OLED_SCL_PORT       GPIOB
#define OLED_SCL_PIN        GPIO_Pin_7
#define OLED_SDA_PORT       GPIOB
#define OLED_SDA_PIN        GPIO_Pin_8

/* ========== Key (PB5 only, active low with internal pull-up) ========== */
#define KEY1_PORT           GPIOB
#define KEY1_PIN            GPIO_Pin_5
#define KEY_LONG_PRESS_MS   1000
#define KEY_DEBOUNCE_MS     20

/* ========== Control Timing ========== */
#define CONTROL_PERIOD_MS   10
#define HEADING_LOOP_MS     10
#define TELEMETRY_PERIOD_MS 20
#define IMU_READ_PERIOD_MS  20

/* ========== PID Defaults - Straight Mode ========== */
#define PID_STRAIGHT_SPEED_KP    0.35f
#define PID_STRAIGHT_SPEED_KI    0.02f
#define PID_STRAIGHT_SPEED_KD    0.0f
#define PID_STRAIGHT_HEADING_KP  0.45f
#define PID_STRAIGHT_HEADING_KI  0.12f
#define PID_STRAIGHT_HEADING_KD  0.30f

/* ========== PID Defaults - Track Mode ========== */
#define PID_TRACK_SPEED_KP       0.6f
#define PID_TRACK_SPEED_KI       0.012f
#define PID_TRACK_SPEED_KD       0.0f
#define PID_TRACK_LINE_KP        2.0f
#define PID_TRACK_LINE_KI        0.01f
#define PID_TRACK_LINE_KD        0.5f

/* ========== Speed Ramp ========== */
#define SPEED_TARGET_DEFAULT     10.0f
#define SPEED_ENTRY              0.0f
#define SPEED_RAMP_RATE          20.0f
#define SPEED_OUTPUT_LIMIT       60.0f
#define SPEED_FEEDFORWARD_GAIN   1.15f
#define PID_DERIV_LPF_ALPHA      0.15f

#endif
