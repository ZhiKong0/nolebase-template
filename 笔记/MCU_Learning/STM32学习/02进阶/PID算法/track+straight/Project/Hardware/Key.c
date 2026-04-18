#include "Key.h"
#include "Delay.h"

static uint8_t Key_Scan(GPIO_TypeDef *port, uint16_t pin)
{
    if (GPIO_ReadInputDataBit(port, pin) != Bit_RESET)
    {
        return 0U;
    }

    Delay_ms(15);
    if (GPIO_ReadInputDataBit(port, pin) != Bit_RESET)
    {
        return 0U;
    }

    while (GPIO_ReadInputDataBit(port, pin) == Bit_RESET)
    {
        Delay_ms(5);
    }

    Delay_ms(15);
    return 1U;
}

void Key_Init(void)
{
    GPIO_InitTypeDef gpio_init;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);

    gpio_init.GPIO_Pin = GPIO_Pin_5 | GPIO_Pin_6;
    gpio_init.GPIO_Mode = GPIO_Mode_IPU;
    gpio_init.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOB, &gpio_init);
}

uint8_t Key_GetNum(void)
{
    if (Key_Scan(GPIOB, GPIO_Pin_5) != 0U)
    {
        return 1U;
    }

    if (Key_Scan(GPIOB, GPIO_Pin_6) != 0U)
    {
        return 2U;
    }

    return 0U;
}
