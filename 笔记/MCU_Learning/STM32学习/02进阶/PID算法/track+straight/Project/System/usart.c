#include "usart.h"

#if SYSTEM_SUPPORT_UCOS
#include "includes.h"
#endif

#pragma import(__use_no_semihosting)

struct __FILE
{
    int handle;
};

FILE __stdout;

void _sys_exit(int x)
{
    (void)x;
}

int fputc(int ch, FILE *f)
{
    (void)f;

    USART_SendData(USART2, (uint8_t)ch);
    while (USART_GetFlagStatus(USART2, USART_FLAG_TXE) == RESET)
    {
    }

    return ch;
}

#if EN_USART2_RX
u8 USART_RX_BUF[USART_REC_LEN];
u16 USART_RX_STA = 0;
#else
u8 USART_RX_BUF[1];
u16 USART_RX_STA = 0;
#endif

void uart_init(u32 bound)
{
    GPIO_InitTypeDef gpio_init;
    USART_InitTypeDef usart_init;
    NVIC_InitTypeDef nvic_init;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_AFIO, ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_USART2, ENABLE);

    gpio_init.GPIO_Pin = GPIO_Pin_2;
    gpio_init.GPIO_Mode = GPIO_Mode_AF_PP;
    gpio_init.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &gpio_init);

    gpio_init.GPIO_Pin = GPIO_Pin_3;
    gpio_init.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    GPIO_Init(GPIOA, &gpio_init);

    usart_init.USART_BaudRate = bound;
    usart_init.USART_WordLength = USART_WordLength_8b;
    usart_init.USART_StopBits = USART_StopBits_1;
    usart_init.USART_Parity = USART_Parity_No;
    usart_init.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    usart_init.USART_Mode = USART_Mode_Rx | USART_Mode_Tx;
    USART_Init(USART2, &usart_init);

#if EN_USART2_RX
    nvic_init.NVIC_IRQChannel = USART2_IRQn;
    nvic_init.NVIC_IRQChannelPreemptionPriority = 3;
    nvic_init.NVIC_IRQChannelSubPriority = 3;
    nvic_init.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&nvic_init);

    USART_ITConfig(USART2, USART_IT_RXNE, ENABLE);
#endif

    USART_Cmd(USART2, ENABLE);
}

void uart_send_string(const char *str)
{
    while ((str != 0) && (*str != '\0'))
    {
        fputc(*str, &__stdout);
        str++;
    }
}

#if EN_USART2_RX
void USART2_IRQHandler(void)
{
    u8 res;

#ifdef OS_TICKS_PER_SEC
    OSIntEnter();
#endif

    if (USART_GetITStatus(USART2, USART_IT_RXNE) != RESET)
    {
        res = (u8)USART_ReceiveData(USART2);

        if ((USART_RX_STA & 0x8000U) == 0U)
        {
            if ((USART_RX_STA & 0x4000U) != 0U)
            {
                if (res != 0x0AU)
                {
                    USART_RX_STA = 0U;
                }
                else
                {
                    USART_RX_STA |= 0x8000U;
                }
            }
            else if (res == 0x0DU)
            {
                USART_RX_STA |= 0x4000U;
            }
            else
            {
                USART_RX_BUF[USART_RX_STA & 0x3FFFU] = res;
                USART_RX_STA++;
                if ((USART_RX_STA & 0x3FFFU) >= (USART_REC_LEN - 1U))
                {
                    USART_RX_STA = 0U;
                }
            }
        }
    }

#ifdef OS_TICKS_PER_SEC
    OSIntExit();
#endif
}
#endif
