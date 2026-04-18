#ifndef __OLED_H
#define __OLED_H
#include "stm32f10x.h"                  // Device header


void OLED_Init(void);
void OLED_Clear(void);

/* 传统网格显示（4行x16列） */
void OLED_ShowChar(uint8_t Line, uint8_t Column, char Char);
void OLED_ShowString(uint8_t Line, uint8_t Column, char *String);
void OLED_ShowNum(uint8_t Line, uint8_t Column, uint32_t Number, uint8_t Length);
void OLED_ShowSignedNum(uint8_t Line, uint8_t Column, int32_t Number, uint8_t Length);
void OLED_ShowHexNum(uint8_t Line, uint8_t Column, uint32_t Number, uint8_t Length);
void OLED_ShowBinNum(uint8_t Line, uint8_t Column, uint32_t Number, uint8_t Length);

/* 像素级定位显示（x: 0-127, y: 0-63） */
void OLED_ShowCharAt(uint8_t x, uint8_t y, char Char);
void OLED_ShowStringAt(uint8_t x, uint8_t y, char *String);

/* 底层控制 */
void OLED_SetCursor(uint8_t Y, uint8_t X);
void OLED_WriteCommand(uint8_t Command);
void OLED_WriteData(uint8_t Data);

#endif
