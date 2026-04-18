#ifndef __VOFA_H
#define __VOFA_H

#include "stm32f10x.h"

#define VOFA_USE_USART1     0

#if VOFA_USE_USART1
#define VOFA_USART          USART1
#define VOFA_USART_IRQn     USART1_IRQn
#define VOFA_USART_RCC      RCC_APB2Periph_USART1
#define VOFA_USART_APBx     2
#define VOFA_TX_PIN         GPIO_Pin_9
#define VOFA_RX_PIN         GPIO_Pin_10
#define VOFA_GPIO_PORT      GPIOA
#else
#define VOFA_USART          USART2
#define VOFA_USART_IRQn     USART2_IRQn
#define VOFA_USART_RCC      RCC_APB1Periph_USART2
#define VOFA_USART_APBx     1
#define VOFA_TX_PIN         GPIO_Pin_2
#define VOFA_RX_PIN         GPIO_Pin_3
#define VOFA_GPIO_PORT      GPIOA
#endif

#define VOFA_BAUDRATE       115200

typedef enum {
    VOFA_STATE_WAIT_HEAD = 0,
    VOFA_STATE_RECEIVING,
    VOFA_STATE_COMPLETE
} VOFA_State_t;

typedef enum {
    VOFA_CHANNEL_USB = 0,
    VOFA_CHANNEL_BLUETOOTH,
    VOFA_CHANNEL_BOTH
} VOFA_Channel_t;

void VOFA_Init(void);
void VOFA_SetChannel(VOFA_Channel_t channel);
void VOFA_SendString(const char *s);
void VOFA_SendJustFloat3(float ch0, float ch1, float ch2);
void VOFA_SendJustFloat5(float ch0, float ch1, float ch2, float ch3, float ch4);
uint8_t VOFA_TakeCommand(char *out, uint8_t outSize);
uint32_t VOFA_GetRxByteCount(void);
uint32_t VOFA_GetTxDropByteCount(void);

#endif
