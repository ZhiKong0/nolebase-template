#include "Control.h"
#include "sys.h"
#include "stdio.h"
#include "Key.h"
#include "Motor.h"
#include "Timer.h"
#include "usart.h"
#include "Encoder_Timer.h"

#define TRACK_SENSOR_COUNT 8U
#define TRACK_SPEED_LEVEL_COUNT 3U
#define TRACK_CONTROL_PERIOD_MS 40U
#define TRACK_MAX_TARGET 18.0f
#define TRACK_MAX_OUTPUT 24.0f
#define TRACK_MAX_INTEGRAL 80.0f
#define TRACK_PWM_SCALE 5.0f
#define TRACK_STARTUP_RAMP_MS 1200U

static const GPIO_TypeDef * const kTrackPorts[TRACK_SENSOR_COUNT] = {
    GPIOA, GPIOA, GPIOA, GPIOB, GPIOB, GPIOB, GPIOB, GPIOC
};

static const uint16_t kTrackPins[TRACK_SENSOR_COUNT] = {
    GPIO_Pin_10, GPIO_Pin_11, GPIO_Pin_12, GPIO_Pin_3,
    GPIO_Pin_4, GPIO_Pin_9, GPIO_Pin_11, GPIO_Pin_13
};

static const float kTrackWeights[TRACK_SENSOR_COUNT] = {
    -35.0f, -25.0f, -15.0f, -5.0f, 5.0f, 15.0f, 25.0f, 35.0f
};

static const float kTrackBaseSpeeds[TRACK_SPEED_LEVEL_COUNT] = {
    6.0f, 8.0f, 10.0f
};

static volatile ControlTelemetry_t g_control;

static float g_kpLeft = 0.32f;
static float g_kiLeft = 0.18f;
static float g_kdLeft = 0.04f;
static float g_kpRight = 0.32f;
static float g_kiRight = 0.18f;
static float g_kdRight = 0.04f;

static float g_errLeft = 0.0f;
static float g_prevErrLeft = 0.0f;
static float g_intErrLeft = 0.0f;
static float g_errRight = 0.0f;
static float g_prevErrRight = 0.0f;
static float g_intErrRight = 0.0f;

static float g_lastTrackError = 0.0f;
static int8_t g_lastTrackDirection = 0;
static uint32_t g_lastPrintTick = 0U;
static uint32_t g_runStartTick = 0U;

static void Control_InitTrackSensorGPIO(void)
{
    GPIO_InitTypeDef gpio_init;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_AFIO | RCC_APB2Periph_GPIOA |
                           RCC_APB2Periph_GPIOB | RCC_APB2Periph_GPIOC,
                           ENABLE);

    GPIO_PinRemapConfig(GPIO_Remap_SWJ_JTAGDisable, ENABLE);

    gpio_init.GPIO_Mode = GPIO_Mode_IPU;
    gpio_init.GPIO_Speed = GPIO_Speed_50MHz;

    gpio_init.GPIO_Pin = GPIO_Pin_10 | GPIO_Pin_11 | GPIO_Pin_12;
    GPIO_Init(GPIOA, &gpio_init);

    gpio_init.GPIO_Pin = GPIO_Pin_3 | GPIO_Pin_4 | GPIO_Pin_9 | GPIO_Pin_11;
    GPIO_Init(GPIOB, &gpio_init);

    gpio_init.GPIO_Pin = GPIO_Pin_13;
    GPIO_Init(GPIOC, &gpio_init);
}

static float Control_ClampFloat(float value, float min_value, float max_value)
{
    if (value > max_value)
    {
        return max_value;
    }

    if (value < min_value)
    {
        return min_value;
    }

    return value;
}

static int32_t Control_Scale10(float value)
{
    if (value >= 0.0f)
    {
        return (int32_t)(value * 10.0f + 0.5f);
    }

    return (int32_t)(value * 10.0f - 0.5f);
}

static void Control_ResetSpeedLoop(void)
{
    g_errLeft = 0.0f;
    g_prevErrLeft = 0.0f;
    g_intErrLeft = 0.0f;
    g_errRight = 0.0f;
    g_prevErrRight = 0.0f;
    g_intErrRight = 0.0f;

    g_control.actualLeft = 0.0f;
    g_control.actualRight = 0.0f;
    g_control.outLeft = 0.0f;
    g_control.outRight = 0.0f;
}

static void Control_ResetTrackState(void)
{
    g_control.sensorBits = 0U;
    g_control.trackError = 0.0f;
    g_control.targetLeft = 0.0f;
    g_control.targetRight = 0.0f;
    g_lastTrackError = 0.0f;
    g_lastTrackDirection = 0;
}

static uint8_t Control_ReadTrackBits(void)
{
    uint8_t bits = 0U;
    uint8_t index;

    for (index = 0U; index < TRACK_SENSOR_COUNT; index++)
    {
        if (GPIO_ReadInputDataBit((GPIO_TypeDef *)kTrackPorts[index], kTrackPins[index]) == Bit_RESET)
        {
            bits |= (uint8_t)(1U << index);
        }
    }

    return bits;
}

static void Control_UpdateTrackTargets(void)
{
    uint8_t index;
    uint8_t activeCount = 0U;
    float weightedSum = 0.0f;
    float baseSpeed = kTrackBaseSpeeds[g_control.speedLevel];
    float rampFactor = 1.0f;
    float error;
    float derivative;
    float correction;

    if ((g_control.tickMs - g_runStartTick) < TRACK_STARTUP_RAMP_MS)
    {
        rampFactor = (float)(g_control.tickMs - g_runStartTick) / (float)TRACK_STARTUP_RAMP_MS;
    }

    baseSpeed *= rampFactor;

    g_control.sensorBits = Control_ReadTrackBits();

    for (index = 0U; index < TRACK_SENSOR_COUNT; index++)
    {
        if ((g_control.sensorBits & (uint8_t)(1U << index)) != 0U)
        {
            weightedSum += kTrackWeights[index];
            activeCount++;
        }
    }

    if (activeCount > 0U)
    {
        error = weightedSum / (float)activeCount;
        derivative = error - g_lastTrackError;
        correction = error * 0.35f + derivative * 0.18f;

        g_control.trackError = error;
        g_lastTrackError = error;

        if (error > 3.0f)
        {
            g_lastTrackDirection = 1;
        }
        else if (error < -3.0f)
        {
            g_lastTrackDirection = -1;
        }

        g_control.targetLeft = Control_ClampFloat(baseSpeed + correction, -TRACK_MAX_TARGET, TRACK_MAX_TARGET);
        g_control.targetRight = Control_ClampFloat(baseSpeed - correction, -TRACK_MAX_TARGET, TRACK_MAX_TARGET);
        return;
    }

    g_control.trackError = g_lastTrackError;
    if (g_lastTrackDirection < 0)
    {
        g_control.targetLeft = 0.0f;
        g_control.targetRight = Control_ClampFloat(baseSpeed * 0.75f, -TRACK_MAX_TARGET, TRACK_MAX_TARGET);
    }
    else if (g_lastTrackDirection > 0)
    {
        g_control.targetLeft = Control_ClampFloat(baseSpeed * 0.75f, -TRACK_MAX_TARGET, TRACK_MAX_TARGET);
        g_control.targetRight = 0.0f;
    }
    else
    {
        g_control.targetLeft = 0.0f;
        g_control.targetRight = 0.0f;
    }
}

void Control_Init(void)
{
    NVIC_Configuration();
    uart_init(115200U);
    Key_Init();
    Control_InitTrackSensorGPIO();
    Motor_Init();
    Encoder_Timer_Init();
    Timer_Init();

    g_control.running = 0U;
    g_control.speedLevel = 1U;
    g_control.tickMs = 0U;
    g_lastPrintTick = 0U;
    Control_ResetSpeedLoop();
    Control_ResetTrackState();
    Encoder_Reset();
    Motor_Stop();

    printf("track init ok\r\n");
}

void Control_Tick(void)
{
    uint8_t keyNum = Key_GetNum();
    uint32_t tickNow = g_control.tickMs;

    if (keyNum == 1U)
    {
        g_control.running = (uint8_t)!g_control.running;
        Control_ResetSpeedLoop();
        Control_ResetTrackState();
        Encoder_Reset();
        g_runStartTick = g_control.tickMs;
        if (g_control.running == 0U)
        {
            Motor_Stop();
        }
    }
    else if (keyNum == 2U)
    {
        g_control.speedLevel++;
        if (g_control.speedLevel >= TRACK_SPEED_LEVEL_COUNT)
        {
            g_control.speedLevel = 0U;
        }
    }

    if (g_control.running != 0U)
    {
        Control_UpdateTrackTargets();
    }
    else
    {
        g_control.targetLeft = 0.0f;
        g_control.targetRight = 0.0f;
    }

    if ((uint32_t)(tickNow - g_lastPrintTick) >= 100U)
    {
        g_lastPrintTick = tickNow;
        printf("run=%u gear=%u sensor=0x%02X err_x10=%ld tgt_x10=%ld/%ld act_x10=%ld/%ld out_x10=%ld/%ld\r\n",
               g_control.running,
               (uint32_t)(g_control.speedLevel + 1U),
               g_control.sensorBits,
               (long)Control_Scale10(g_control.trackError),
               (long)Control_Scale10(g_control.targetLeft),
               (long)Control_Scale10(g_control.targetRight),
               (long)Control_Scale10(g_control.actualLeft),
               (long)Control_Scale10(g_control.actualRight),
               (long)Control_Scale10(g_control.outLeft),
               (long)Control_Scale10(g_control.outRight));
    }
}

void Control_TimerIRQHandler(void)
{
    static uint16_t controlDivider = 0U;

    g_control.tickMs++;
    controlDivider++;

    if (controlDivider < TRACK_CONTROL_PERIOD_MS)
    {
        return;
    }

    controlDivider = 0U;

    if (g_control.running == 0U)
    {
        Control_ResetSpeedLoop();
        Motor_Stop();
        return;
    }

    g_control.actualLeft = (float)Encoder_GetLeft();
    g_control.actualRight = (float)Encoder_GetRight();

    g_prevErrLeft = g_errLeft;
    g_prevErrRight = g_errRight;
    g_errLeft = g_control.targetLeft - g_control.actualLeft;
    g_errRight = g_control.targetRight - g_control.actualRight;

    g_intErrLeft = Control_ClampFloat(g_intErrLeft + g_errLeft, -TRACK_MAX_INTEGRAL, TRACK_MAX_INTEGRAL);
    g_intErrRight = Control_ClampFloat(g_intErrRight + g_errRight, -TRACK_MAX_INTEGRAL, TRACK_MAX_INTEGRAL);

    g_control.outLeft = g_kpLeft * g_errLeft + g_kiLeft * g_intErrLeft + g_kdLeft * (g_errLeft - g_prevErrLeft);
    g_control.outRight = g_kpRight * g_errRight + g_kiRight * g_intErrRight + g_kdRight * (g_errRight - g_prevErrRight);

    g_control.outLeft = Control_ClampFloat(g_control.outLeft, -TRACK_MAX_OUTPUT, TRACK_MAX_OUTPUT);
    g_control.outRight = Control_ClampFloat(g_control.outRight, -TRACK_MAX_OUTPUT, TRACK_MAX_OUTPUT);

    Motor_SetLeft((int16_t)(g_control.outLeft * TRACK_PWM_SCALE));
    Motor_SetRight((int16_t)(g_control.outRight * TRACK_PWM_SCALE));
}

const ControlTelemetry_t *Control_GetTelemetry(void)
{
    return (const ControlTelemetry_t *)&g_control;
}
