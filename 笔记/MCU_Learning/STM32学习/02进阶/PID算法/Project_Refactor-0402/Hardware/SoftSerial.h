#ifndef __SOFTSERIAL_H
#define __SOFTSERIAL_H

#include "stm32f10x.h"

void SoftSerial_Init(void);
void SoftSerial_SendByte(uint8_t b);
void SoftSerial_SendString(const char *s);
void SoftSerial_SendFloat3(float ch0, float ch1, float ch2);

#endif
