#ifndef __BOARD_CONFIG_H
#define __BOARD_CONFIG_H

#include "stm32f10x.h"

#define APP_CONTROL_PERIOD_MS          10u
#define APP_DISPLAY_PERIOD_MS          100u
#define APP_TELEMETRY_PERIOD_MS        200u

#define BOARD_CONTROL_TICK_TIMER       TIM4
#define BOARD_CONTROL_TICK_RCC         RCC_APB1Periph_TIM4
#define BOARD_CONTROL_TICK_IRQn        TIM4_IRQn

#define BOARD_KEY_PORT                 GPIOB
#define BOARD_KEY_PIN                  GPIO_Pin_5
#define BOARD_KEY_LONG_PRESS_MS        800u
#define BOARD_KEY_DEBOUNCE_MS          20u

#define BOARD_MOTOR_STBY_PORT          GPIOB
#define BOARD_MOTOR_STBY_PIN           GPIO_Pin_0
#define BOARD_MOTOR_AIN_PORT           GPIOA
#define BOARD_MOTOR_AIN1_PIN           GPIO_Pin_4
#define BOARD_MOTOR_AIN2_PIN           GPIO_Pin_5
#define BOARD_MOTOR_BIN_PORT           GPIOB
#define BOARD_MOTOR_BIN1_PIN           GPIO_Pin_1
#define BOARD_MOTOR_BIN2_PIN           GPIO_Pin_10
#define BOARD_MOTOR_PWM_TIMER          TIM1
#define BOARD_MOTOR_PWM_PERIOD         100u
#define BOARD_MOTOR_PWM_PRESCALER      719u

#define BOARD_TRACK_ACTIVE_LOW         1u
#define BOARD_TRACK_S1_PORT            GPIOA
#define BOARD_TRACK_S1_PIN             GPIO_Pin_10
#define BOARD_TRACK_S2_PORT            GPIOA
#define BOARD_TRACK_S2_PIN             GPIO_Pin_11
#define BOARD_TRACK_S3_PORT            GPIOA
#define BOARD_TRACK_S3_PIN             GPIO_Pin_12
#define BOARD_TRACK_S4_PORT            GPIOB
#define BOARD_TRACK_S4_PIN             GPIO_Pin_3
#define BOARD_TRACK_S5_PORT            GPIOB
#define BOARD_TRACK_S5_PIN             GPIO_Pin_4
#define BOARD_TRACK_S6_PORT            GPIOB
#define BOARD_TRACK_S6_PIN             GPIO_Pin_9
#define BOARD_TRACK_S7_PORT            GPIOB
#define BOARD_TRACK_S7_PIN             GPIO_Pin_11
#define BOARD_TRACK_S8_PORT            GPIOC
#define BOARD_TRACK_S8_PIN             GPIO_Pin_13

#define BOARD_IMU_SCL_PORT             GPIOB
#define BOARD_IMU_SCL_PIN              GPIO_Pin_12
#define BOARD_IMU_SDA_PORT             GPIOB
#define BOARD_IMU_SDA_PIN              GPIO_Pin_13
#define BOARD_IMU_RESET_PORT           GPIOB
#define BOARD_IMU_RESET_PIN            GPIO_Pin_14
#define BOARD_IMU_INT_PORT             GPIOA
#define BOARD_IMU_INT_PIN              GPIO_Pin_15

#endif
