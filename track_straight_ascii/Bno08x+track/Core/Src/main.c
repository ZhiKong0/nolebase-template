/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
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
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "i2c.h"
#include "tim.h"
#include "usart.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "fun.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */
volatile uint8_t g_track_enabled = 0U;
volatile float Actual_leftx = 0.0f;
volatile float Actual_rightx = 0.0f;
volatile float Out_leftx = 0.0f;
volatile float Out_rightx = 0.0f;

float Kp_left = 0.32f;
float Ki_left = 0.18f;
float Kd_left = 0.04f;
float Kp_right = 0.32f;
float Ki_right = 0.18f;
float Kd_right = 0.04f;

float Error0_leftx = 0.0f;
float Error1_leftx = 0.0f;
float ErrorInt_leftx = 0.0f;
float Error0_rightx = 0.0f;
float Error1_rightx = 0.0f;
float ErrorInt_rightx = 0.0f;
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */
static void reset_speed_loop(void);
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
static void reset_speed_loop(void)
{
  Error0_leftx = 0.0f;
  Error1_leftx = 0.0f;
  ErrorInt_leftx = 0.0f;
  Error0_rightx = 0.0f;
  Error1_rightx = 0.0f;
  ErrorInt_rightx = 0.0f;
  Out_leftx = 0.0f;
  Out_rightx = 0.0f;
  Actual_leftx = 0.0f;
  Actual_rightx = 0.0f;
}
/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{
  uint8_t key_event;
  uint32_t last_print_tick = 0U;

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* Configure the system clock */
  SystemClock_Config();

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_TIM1_Init();
  MX_TIM2_Init();
  MX_TIM3_Init();
  MX_USART2_UART_Init();

  /* USER CODE BEGIN 2 */
  HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_1);
  HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_2);
  HAL_TIM_Encoder_Start(&htim2, TIM_CHANNEL_ALL);
  HAL_TIM_Encoder_Start(&htim3, TIM_CHANNEL_ALL);
  HAL_TIM_Base_Start_IT(&htim1);
  Motor_StopAll();
  track_reset();
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    key_event = key_get();
    if (key_event == 1U)
    {
      g_track_enabled = (uint8_t)!g_track_enabled;
      reset_speed_loop();
      track_reset();
      if (g_track_enabled == 0U)
      {
        Motor_StopAll();
      }
    }
    else if (key_event == 2U)
    {
      track_cycle_speed();
    }

    if (g_track_enabled != 0U)
    {
      track();
      led_mode1();
    }
    else
    {
      led_mode0();
    }

    if ((HAL_GetTick() - last_print_tick) >= 100U)
    {
      last_print_tick = HAL_GetTick();
      Serial_Printf("run=%u,gear=%u,sensor=0x%02X,error=%.1f,target=%.1f/%.1f,actual=%.1f/%.1f,out=%.1f/%.1f\r\n",
                    g_track_enabled,
                    (uint32_t)(track_get_speed_level() + 1U),
                    track_get_raw_bits(),
                    track_get_error(),
                    Target_left,
                    Target_right,
                    Actual_leftx,
                    Actual_rightx,
                    Out_leftx,
                    Out_rightx);
    }
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLMUL = RCC_PLL_MUL9;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }
  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
  {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
  static uint16_t Count = 0U;

  if (htim->Instance == TIM1)
  {
    Count++;
    if (Count >= 40U)
    {
      Count = 0U;

      if (g_track_enabled != 0U)
      {
        Actual_leftx = (float)Encoderleft_Get();
        Actual_rightx = (float)Encoderright_Get();

        Error1_leftx = Error0_leftx;
        Error1_rightx = Error0_rightx;
        Error0_leftx = Target_left - Actual_leftx;
        Error0_rightx = Target_right - Actual_rightx;

        ErrorInt_leftx += Error0_leftx;
        if (ErrorInt_leftx > 250.0f)
        {
          ErrorInt_leftx = 250.0f;
        }
        if (ErrorInt_leftx < -250.0f)
        {
          ErrorInt_leftx = -250.0f;
        }

        ErrorInt_rightx += Error0_rightx;
        if (ErrorInt_rightx > 250.0f)
        {
          ErrorInt_rightx = 250.0f;
        }
        if (ErrorInt_rightx < -250.0f)
        {
          ErrorInt_rightx = -250.0f;
        }

        Out_leftx = Kp_left * Error0_leftx + Ki_left * ErrorInt_leftx + Kd_left * (Error0_leftx - Error1_leftx);
        Out_rightx = Kp_right * Error0_rightx + Ki_right * ErrorInt_rightx + Kd_right * (Error0_rightx - Error1_rightx);

        if (Out_leftx > 100.0f)
        {
          Out_leftx = 100.0f;
        }
        if (Out_leftx < -100.0f)
        {
          Out_leftx = -100.0f;
        }
        if (Out_rightx > 100.0f)
        {
          Out_rightx = 100.0f;
        }
        if (Out_rightx < -100.0f)
        {
          Out_rightx = -100.0f;
        }

        Motorleft_Setpwm((int16_t)(Out_leftx * 10.0f));
        Motorright_Setpwm((int16_t)(Out_rightx * 10.0f));
      }
      else
      {
        reset_speed_loop();
        Motor_StopAll();
      }
    }
  }
}
/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}

#ifdef  USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */

/************************ (C) COPYRIGHT STMicroelectronics *****END OF FILE****/
