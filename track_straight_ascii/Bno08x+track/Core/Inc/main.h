/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.h
  * @brief          : Header for main.c file.
  *                   This file contains the common defines of the application.
  ******************************************************************************
  * @attention
  *
  * <h2><center>&copy; Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.</center></h2>
  *
  * This software component is licensed by ST under BSD 3-Clause license,
  * the "License"; You may not use this file except in compliance with the
  * License. You may obtain a copy of the License at:
  *                        opensource.org/licenses/BSD-3-Clause
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "stm32f1xx_hal.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Exported types ------------------------------------------------------------*/
/* USER CODE BEGIN ET */

/* USER CODE END ET */

/* Exported constants --------------------------------------------------------*/
/* USER CODE BEGIN EC */

/* USER CODE END EC */

/* Exported macro ------------------------------------------------------------*/
/* USER CODE BEGIN EM */

/* USER CODE END EM */

/* Exported functions prototypes ---------------------------------------------*/
void Error_Handler(void);

/* USER CODE BEGIN EFP */

/* USER CODE END EFP */

/* Private defines -----------------------------------------------------------*/
/* USER CODE BEGIN Private defines */
#define TB6612_STBY_Pin GPIO_PIN_0
#define TB6612_STBY_GPIO_Port GPIOB
#define TB6612_BIN1_Pin GPIO_PIN_1
#define TB6612_BIN1_GPIO_Port GPIOB
#define TB6612_BIN2_Pin GPIO_PIN_10
#define TB6612_BIN2_GPIO_Port GPIOB

#define TB6612_AIN1_Pin GPIO_PIN_4
#define TB6612_AIN1_GPIO_Port GPIOA
#define TB6612_AIN2_Pin GPIO_PIN_5
#define TB6612_AIN2_GPIO_Port GPIOA
#define TB6612_PWMA_Pin GPIO_PIN_8
#define TB6612_PWMA_GPIO_Port GPIOA
#define TB6612_PWMB_Pin GPIO_PIN_9
#define TB6612_PWMB_GPIO_Port GPIOA

#define TRACK_S1_Pin GPIO_PIN_10
#define TRACK_S1_GPIO_Port GPIOA
#define TRACK_S2_Pin GPIO_PIN_11
#define TRACK_S2_GPIO_Port GPIOA
#define TRACK_S3_Pin GPIO_PIN_12
#define TRACK_S3_GPIO_Port GPIOA
#define TRACK_S4_Pin GPIO_PIN_3
#define TRACK_S4_GPIO_Port GPIOB
#define TRACK_S5_Pin GPIO_PIN_4
#define TRACK_S5_GPIO_Port GPIOB
#define TRACK_S6_Pin GPIO_PIN_9
#define TRACK_S6_GPIO_Port GPIOB
#define TRACK_S7_Pin GPIO_PIN_11
#define TRACK_S7_GPIO_Port GPIOB
#define TRACK_S8_Pin GPIO_PIN_13
#define TRACK_S8_GPIO_Port GPIOC

#define KEY_MODE_Pin GPIO_PIN_5
#define KEY_MODE_GPIO_Port GPIOB
#define KEY_SPEED_Pin GPIO_PIN_6
#define KEY_SPEED_GPIO_Port GPIOB

#define BNO_SCL_Pin GPIO_PIN_12
#define BNO_SCL_GPIO_Port GPIOB
#define BNO_SDA_Pin GPIO_PIN_13
#define BNO_SDA_GPIO_Port GPIOB

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */

/************************ (C) COPYRIGHT STMicroelectronics *****END OF FILE****/
