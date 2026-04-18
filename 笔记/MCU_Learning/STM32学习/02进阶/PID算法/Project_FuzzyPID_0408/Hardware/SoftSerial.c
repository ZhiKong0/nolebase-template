/**
 * @file SoftSerial.c
 * @brief 软串口驱动（简化版）
 */

#include "SoftSerial.h"
#include "stm32f10x.h"
#include <string.h>
#include <stdio.h>

// 简化实现：直接使用USART发送
extern void USART_SendData(USART_TypeDef* USARTx, uint16_t Data);
extern FlagStatus USART_GetFlagStatus(USART_TypeDef* USARTx, uint16_t USART_FLAG);

void SoftSerial_Init(void) {
    // 简化实现：不做任何初始化
    // 假设USART已经在其他地方初始化
}

void SoftSerial_SendByte(uint8_t b) {
    USART_SendData(USART1, b);
    while (USART_GetFlagStatus(USART1, USART_FLAG_TXE) == RESET);
}

void SoftSerial_SendString(const char *s) {
    while (*s) {
        SoftSerial_SendByte(*s++);
    }
}

void SoftSerial_SendFloat3(float ch0, float ch1, float ch2) {
    char buf[64];
    snprintf(buf, sizeof(buf), "%.3f,%.3f,%.3f\r\n", ch0, ch1, ch2);
    SoftSerial_SendString(buf);
}
