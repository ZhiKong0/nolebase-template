#ifndef __SERIAL_H
#define __SERIAL_H

#include "stm32f10x.h"
#include <stdio.h>

void Serial_Init(u32 baudrate);
void Serial_SendByte(u8 byte);
void Serial_SendString(char *str);
void Serial_SendNumber(int num);

#endif
