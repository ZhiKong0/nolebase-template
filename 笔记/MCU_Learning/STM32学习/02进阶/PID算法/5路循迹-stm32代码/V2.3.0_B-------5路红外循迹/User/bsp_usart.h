#ifndef __USART_H
#define	__USART_H


#include "stm32f10x.h"
#include <stdio.h>


#define  USART1_
/********************************************************************* 
* 串口宏定义，不同的串口挂载的总线和IO不一样，移植时需要修改这几个宏
* 1-修改总线时钟的宏，uart1挂载到APB2总线，其他uart挂载到APB1总线
* 2-修改GPIO的宏
*********************************************************************/

#ifdef USART1_	
// 串口1-USART1
#define  DEBUG_USARTx                   USART1
#define  DEBUG_USART_CLK                RCC_APB2Periph_USART1
#define  DEBUG_USART_APBxClkCmd         RCC_APB2PeriphClockCmd
#define  DEBUG_USART_BAUDRATE           115200

// USART GPIO 引脚宏定义
#define  DEBUG_USART_GPIO_CLK           (RCC_APB2Periph_GPIOA)
#define  DEBUG_USART_GPIO_APBxClkCmd    RCC_APB2PeriphClockCmd
    
#define  DEBUG_USART_TX_GPIO_PORT       GPIOA   
#define  DEBUG_USART_TX_GPIO_PIN        GPIO_Pin_9
#define  DEBUG_USART_RX_GPIO_PORT       GPIOA
#define  DEBUG_USART_RX_GPIO_PIN        GPIO_Pin_10


#endif   //USART1_





/***************************函数声明***************************/
void USART_Config(void);


#endif /* __USART_H */


