#ifndef __OLED_H
#define __OLED_H

#include "stm32f10x.h"

void OLED_Init(void);
void OLED_Clear(void);
void OLED_ShowString(uint8_t line, uint8_t col, char *str);
void OLED_ShowNum(uint8_t line, uint8_t col, uint32_t num, uint8_t len);
void OLED_ShowSignedNum(uint8_t line, uint8_t col, int32_t num, uint8_t len);

#endif
