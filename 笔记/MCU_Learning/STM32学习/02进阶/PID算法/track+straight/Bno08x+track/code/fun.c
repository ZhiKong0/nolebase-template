#include "headfile.h"

#define TRACK_SENSOR_COUNT 8U
#define TRACK_SPEED_LEVEL_COUNT 3U
#define TRACK_MAX_TARGET 70.0f

static const GPIO_TypeDef * const kTrackPorts[TRACK_SENSOR_COUNT] = {
  TRACK_S1_GPIO_Port,
  TRACK_S2_GPIO_Port,
  TRACK_S3_GPIO_Port,
  TRACK_S4_GPIO_Port,
  TRACK_S5_GPIO_Port,
  TRACK_S6_GPIO_Port,
  TRACK_S7_GPIO_Port,
  TRACK_S8_GPIO_Port,
};

static const uint16_t kTrackPins[TRACK_SENSOR_COUNT] = {
  TRACK_S1_Pin,
  TRACK_S2_Pin,
  TRACK_S3_Pin,
  TRACK_S4_Pin,
  TRACK_S5_Pin,
  TRACK_S6_Pin,
  TRACK_S7_Pin,
  TRACK_S8_Pin,
};

static const float kTrackWeights[TRACK_SENSOR_COUNT] = {-35.0f, -25.0f, -15.0f, -5.0f, 5.0f, 15.0f, 25.0f, 35.0f};
static const float kTrackBaseSpeeds[TRACK_SPEED_LEVEL_COUNT] = {34.0f, 42.0f, 50.0f};

volatile float Target_left = 0.0f;
volatile float Target_right = 0.0f;

static uint8_t g_track_raw_bits = 0U;
static float g_track_error = 0.0f;
static uint8_t g_track_speed_level = 1U;
static float g_last_track_error = 0.0f;
static int8_t g_last_track_direction = 0;

static uint16_t clamp_pwm_compare(int32_t compare)
{
  uint32_t limit = htim1.Init.Period;

  if (compare <= 0)
  {
    return 0U;
  }
  if ((uint32_t)compare > limit)
  {
    return (uint16_t)limit;
  }

  return (uint16_t)compare;
}

static float clamp_track_target(float target)
{
  if (target > TRACK_MAX_TARGET)
  {
    return TRACK_MAX_TARGET;
  }
  if (target < -TRACK_MAX_TARGET)
  {
    return -TRACK_MAX_TARGET;
  }

  return target;
}

static uint8_t track_read_raw_bits_internal(void)
{
  uint8_t raw_bits = 0U;
  uint8_t index;

  for (index = 0; index < TRACK_SENSOR_COUNT; index++)
  {
    if (HAL_GPIO_ReadPin((GPIO_TypeDef *)kTrackPorts[index], kTrackPins[index]) == GPIO_PIN_RESET)
    {
      raw_bits |= (uint8_t)(1U << index);
    }
  }

  return raw_bits;
}

static uint8_t key_pressed(GPIO_TypeDef *port, uint16_t pin)
{
  if (HAL_GPIO_ReadPin(port, pin) != GPIO_PIN_RESET)
  {
    return 0U;
  }

  HAL_Delay(15);
  if (HAL_GPIO_ReadPin(port, pin) != GPIO_PIN_RESET)
  {
    return 0U;
  }

  while (HAL_GPIO_ReadPin(port, pin) == GPIO_PIN_RESET)
  {
    HAL_Delay(5);
  }

  HAL_Delay(15);
  return 1U;
}

int16_t Encoderleft_Get(void)
{
  int16_t temp;

  temp = (int16_t)__HAL_TIM_GET_COUNTER(&htim2);
  __HAL_TIM_SET_COUNTER(&htim2, 0);
  return temp;
}

int16_t Encoderright_Get(void)
{
  int16_t temp;

  temp = (int16_t)__HAL_TIM_GET_COUNTER(&htim3);
  __HAL_TIM_SET_COUNTER(&htim3, 0);
  return temp;
}

void Motorleft_Setpwm(int16_t Speed)
{
  uint16_t compare = clamp_pwm_compare((Speed < 0) ? -Speed : Speed);

  HAL_GPIO_WritePin(TB6612_STBY_GPIO_Port, TB6612_STBY_Pin, GPIO_PIN_SET);

  if (Speed > 0)
  {
    HAL_GPIO_WritePin(TB6612_AIN1_GPIO_Port, TB6612_AIN1_Pin, GPIO_PIN_SET);
    HAL_GPIO_WritePin(TB6612_AIN2_GPIO_Port, TB6612_AIN2_Pin, GPIO_PIN_RESET);
  }
  else if (Speed < 0)
  {
    HAL_GPIO_WritePin(TB6612_AIN1_GPIO_Port, TB6612_AIN1_Pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(TB6612_AIN2_GPIO_Port, TB6612_AIN2_Pin, GPIO_PIN_SET);
  }
  else
  {
    HAL_GPIO_WritePin(TB6612_AIN1_GPIO_Port, TB6612_AIN1_Pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(TB6612_AIN2_GPIO_Port, TB6612_AIN2_Pin, GPIO_PIN_RESET);
  }

  __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_1, compare);
}

void Motorright_Setpwm(int16_t Speed)
{
  uint16_t compare = clamp_pwm_compare((Speed < 0) ? -Speed : Speed);

  HAL_GPIO_WritePin(TB6612_STBY_GPIO_Port, TB6612_STBY_Pin, GPIO_PIN_SET);

  if (Speed > 0)
  {
    HAL_GPIO_WritePin(TB6612_BIN1_GPIO_Port, TB6612_BIN1_Pin, GPIO_PIN_SET);
    HAL_GPIO_WritePin(TB6612_BIN2_GPIO_Port, TB6612_BIN2_Pin, GPIO_PIN_RESET);
  }
  else if (Speed < 0)
  {
    HAL_GPIO_WritePin(TB6612_BIN1_GPIO_Port, TB6612_BIN1_Pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(TB6612_BIN2_GPIO_Port, TB6612_BIN2_Pin, GPIO_PIN_SET);
  }
  else
  {
    HAL_GPIO_WritePin(TB6612_BIN1_GPIO_Port, TB6612_BIN1_Pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(TB6612_BIN2_GPIO_Port, TB6612_BIN2_Pin, GPIO_PIN_RESET);
  }

  __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_2, compare);
}

void Motor_StopAll(void)
{
  __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_1, 0U);
  __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_2, 0U);

  HAL_GPIO_WritePin(TB6612_AIN1_GPIO_Port, TB6612_AIN1_Pin, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(TB6612_AIN2_GPIO_Port, TB6612_AIN2_Pin, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(TB6612_BIN1_GPIO_Port, TB6612_BIN1_Pin, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(TB6612_BIN2_GPIO_Port, TB6612_BIN2_Pin, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(TB6612_STBY_GPIO_Port, TB6612_STBY_Pin, GPIO_PIN_RESET);
}

void track_reset(void)
{
  Target_left = 0.0f;
  Target_right = 0.0f;
  g_track_raw_bits = 0U;
  g_track_error = 0.0f;
  g_last_track_error = 0.0f;
  g_last_track_direction = 0;
}

void track_cycle_speed(void)
{
  g_track_speed_level++;
  if (g_track_speed_level >= TRACK_SPEED_LEVEL_COUNT)
  {
    g_track_speed_level = 0U;
  }
}

uint8_t track_get_speed_level(void)
{
  return g_track_speed_level;
}

uint8_t track_get_raw_bits(void)
{
  return g_track_raw_bits;
}

float track_get_error(void)
{
  return g_track_error;
}

void track(void)
{
  uint8_t index;
  uint8_t active_count = 0U;
  float weighted_sum = 0.0f;
  float base_speed = kTrackBaseSpeeds[g_track_speed_level];
  float error;
  float derivative;
  float correction;

  g_track_raw_bits = track_read_raw_bits_internal();

  for (index = 0; index < TRACK_SENSOR_COUNT; index++)
  {
    if ((g_track_raw_bits & (uint8_t)(1U << index)) != 0U)
    {
      weighted_sum += kTrackWeights[index];
      active_count++;
    }
  }

  if (active_count > 0U)
  {
    error = weighted_sum / (float)active_count;
    derivative = error - g_last_track_error;
    correction = error * 0.55f + derivative * 0.30f;

    g_track_error = error;
    g_last_track_error = error;

    if (error > 3.0f)
    {
      g_last_track_direction = 1;
    }
    else if (error < -3.0f)
    {
      g_last_track_direction = -1;
    }

    Target_left = clamp_track_target(base_speed + correction);
    Target_right = clamp_track_target(base_speed - correction);
    return;
  }

  g_track_error = g_last_track_error;
  if (g_last_track_direction < 0)
  {
    Target_left = 0.0f;
    Target_right = clamp_track_target(base_speed * 0.95f);
  }
  else if (g_last_track_direction > 0)
  {
    Target_left = clamp_track_target(base_speed * 0.95f);
    Target_right = 0.0f;
  }
  else
  {
    Target_left = clamp_track_target(base_speed * 0.55f);
    Target_right = clamp_track_target(base_speed * 0.55f);
  }
}

uint8_t key_get(void)
{
  if (key_pressed(KEY_MODE_GPIO_Port, KEY_MODE_Pin) != 0U)
  {
    return 1U;
  }

  if (key_pressed(KEY_SPEED_GPIO_Port, KEY_SPEED_Pin) != 0U)
  {
    return 2U;
  }

  return 0U;
}

static void Serial_SentByte(uint8_t Byte)
{
  HAL_UART_Transmit(&huart2, &Byte, 1, 100);
}

static void Serial_SentString(const char *String)
{
  uint16_t i;

  for (i = 0; String[i] != '\0'; i++)
  {
    Serial_SentByte((uint8_t)String[i]);
  }
}

void Serial_Printf(const char *format,...)
{
  char String[128];
  va_list arg;

  va_start(arg, format);
  vsnprintf(String, sizeof(String), format, arg);
  va_end(arg);

  Serial_SentString(String);
}

void led_mode0(void)
{
}

void led_mode1(void)
{
}

void led_mode2(void)
{
}
