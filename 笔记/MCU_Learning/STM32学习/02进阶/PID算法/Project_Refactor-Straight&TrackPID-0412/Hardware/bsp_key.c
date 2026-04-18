#include "bsp_key.h"

/* ========== Internal State Machine ========== */

typedef enum {
    KS_IDLE = 0,
    KS_DEBOUNCE,
    KS_PRESSED,
    KS_WAIT_RELEASE
} KeyState_t;

static KeyState_t s_state = KS_IDLE;
static uint32_t   s_pressStartMs = 0;
static uint32_t   s_debounceStartMs = 0;
static KeyEvent_t s_pendingEvent = KEY_EVENT_NONE;

static uint8_t key_raw_pressed(void)
{
    return (GPIO_ReadInputDataBit(KEY1_PORT, KEY1_PIN) == Bit_RESET) ? 1u : 0u;
}

/* ========== Public API ========== */

void BspKey_Init(void)
{
    GPIO_InitTypeDef g;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);

    g.GPIO_Pin  = KEY1_PIN;
    g.GPIO_Mode = GPIO_Mode_IPU;
    g.GPIO_Speed = GPIO_Speed_2MHz;
    GPIO_Init(KEY1_PORT, &g);

    s_state = KS_IDLE;
    s_pendingEvent = KEY_EVENT_NONE;
}

void BspKey_Tick(uint32_t nowMs)
{
    switch (s_state) {
    case KS_IDLE:
        if (key_raw_pressed()) {
            s_debounceStartMs = nowMs;
            s_state = KS_DEBOUNCE;
        }
        break;

    case KS_DEBOUNCE:
        if (!key_raw_pressed()) {
            s_state = KS_IDLE;
        } else if ((nowMs - s_debounceStartMs) >= KEY_DEBOUNCE_MS) {
            s_pressStartMs = nowMs;
            s_state = KS_PRESSED;
        }
        break;

    case KS_PRESSED:
        if (!key_raw_pressed()) {
            s_pendingEvent = KEY_EVENT_SHORT_PRESS;
            s_state = KS_IDLE;
        } else if ((nowMs - s_pressStartMs) >= KEY_LONG_PRESS_MS) {
            s_pendingEvent = KEY_EVENT_LONG_PRESS;
            s_state = KS_WAIT_RELEASE;
        }
        break;

    case KS_WAIT_RELEASE:
        if (!key_raw_pressed()) {
            s_state = KS_IDLE;
        }
        break;
    }
}

KeyEvent_t BspKey_Read(void)
{
    KeyEvent_t evt = s_pendingEvent;
    s_pendingEvent = KEY_EVENT_NONE;
    return evt;
}
