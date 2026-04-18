#include "DrvKey.h"
#include "BoardConfig.h"

static uint8_t s_lastRawPressed = 0u;
static uint8_t s_stablePressed = 0u;
static uint8_t s_longReported = 0u;
static uint32_t s_lastBounceMs = 0u;
static uint32_t s_pressStartMs = 0u;

static uint8_t drv_key_is_pressed(void)
{
    return (GPIO_ReadInputDataBit(BOARD_KEY_PORT, BOARD_KEY_PIN) == Bit_RESET) ? 1u : 0u;
}

void DrvKey_Init(void)
{
    GPIO_InitTypeDef gpio;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);

    gpio.GPIO_Pin = BOARD_KEY_PIN;
    gpio.GPIO_Mode = GPIO_Mode_IPU;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(BOARD_KEY_PORT, &gpio);

    s_lastRawPressed = drv_key_is_pressed();
    s_stablePressed = s_lastRawPressed;
    s_longReported = 0u;
    s_lastBounceMs = 0u;
    s_pressStartMs = 0u;
}

DrvKeyEvent_t DrvKey_Poll(uint32_t nowMs)
{
    uint8_t rawPressed = drv_key_is_pressed();

    if (rawPressed != s_lastRawPressed) {
        s_lastRawPressed = rawPressed;
        s_lastBounceMs = nowMs;
    }

    if ((uint32_t)(nowMs - s_lastBounceMs) < BOARD_KEY_DEBOUNCE_MS) {
        return DRV_KEY_EVENT_NONE;
    }

    if (rawPressed != s_stablePressed) {
        s_stablePressed = rawPressed;
        if (s_stablePressed != 0u) {
            s_pressStartMs = nowMs;
            s_longReported = 0u;
        } else if (s_longReported == 0u) {
            return DRV_KEY_EVENT_SHORT;
        }
    }

    if ((s_stablePressed != 0u) &&
        (s_longReported == 0u) &&
        ((uint32_t)(nowMs - s_pressStartMs) >= BOARD_KEY_LONG_PRESS_MS)) {
        s_longReported = 1u;
        return DRV_KEY_EVENT_LONG;
    }

    return DRV_KEY_EVENT_NONE;
}
