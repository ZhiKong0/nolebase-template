#include "Key.h"

void key_pb5_init(void)
{
    GPIO_InitTypeDef gpio;
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);
    gpio.GPIO_Pin = GPIO_Pin_5;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    gpio.GPIO_Mode = GPIO_Mode_IPU;
    GPIO_Init(GPIOB, &gpio);
}

uint8_t key_pb5_read_raw(void)
{
    return GPIO_ReadInputDataBit(GPIOB, GPIO_Pin_5) ? 1 : 0;
}
