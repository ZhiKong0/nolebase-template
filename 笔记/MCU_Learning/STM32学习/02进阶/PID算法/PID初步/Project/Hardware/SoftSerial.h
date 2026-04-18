#ifndef __SOFTSERIAL_H
#define __SOFTSERIAL_H
#include "stm32f10x.h"

// 软件串口引脚定义（PB14=TX, PB15=RX）
#define SOFTSERIAL_TX_PIN   GPIO_Pin_14
#define SOFTSERIAL_RX_PIN   GPIO_Pin_15
#define SOFTSERIAL_GPIO     GPIOB

// 波特率（9600或115200）
#define SOFTSERIAL_BAUDRATE  115200

// 初始化软件串口
void SoftSerial_Init(void);

// 发送一个字节
void SoftSerial_SendByte(uint8_t data);

// 发送数据
void SoftSerial_SendArray(uint8_t *data, uint16_t len);

// 发送字符串
void SoftSerial_SendString(char *str);

// 发送 JustFloat 协议（3通道）
void SoftSerial_SendFloat3(float ch0, float ch1, float ch2);

// 检查是否有接收数据
uint8_t SoftSerial_Available(void);

// 读取一个字节（非阻塞）
uint8_t SoftSerial_ReadByte(void);

#endif
