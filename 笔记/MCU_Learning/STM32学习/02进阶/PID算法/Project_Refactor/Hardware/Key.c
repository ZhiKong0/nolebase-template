#include "Key.h"
#include "stm32f10x.h"
#include "Delay.h"

#define KEY1_PORT GPIOB
#define KEY1_PIN  GPIO_Pin_5

#define KEY_LONG_PRESS_MS 800

static uint8_t s_key1Lock = 0;

void Key_Init(void) {
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);

    GPIO_InitTypeDef g;
    g.GPIO_Mode = GPIO_Mode_IPU;
    g.GPIO_Speed = GPIO_Speed_50MHz;
    g.GPIO_Pin = KEY1_PIN;
    GPIO_Init(GPIOB, &g);
}

static uint8_t key_is_pressed(GPIO_TypeDef *port, uint16_t pin) {
    return (GPIO_ReadInputDataBit(port, pin) == Bit_RESET) ? 1 : 0;
}

uint8_t Key_GetNum(void) {
    uint8_t k1 = key_is_pressed(KEY1_PORT, KEY1_PIN);

    if (k1 && !s_key1Lock) {
        Delay_ms(15);
        if (key_is_pressed(KEY1_PORT, KEY1_PIN)) {
            uint32_t pressMs = 0;
            while (key_is_pressed(KEY1_PORT, KEY1_PIN) && pressMs < (KEY_LONG_PRESS_MS + 200)) {
                Delay_ms(10);
                pressMs += 10;
            }
            while (key_is_pressed(KEY1_PORT, KEY1_PIN)) {
            }
            Delay_ms(15);
            s_key1Lock = 1;
            if (pressMs >= KEY_LONG_PRESS_MS) {
                return 2;
            }
            return 1;
        }
    }
    if (!k1) {
        s_key1Lock = 0;
    }

    return 0;
}
