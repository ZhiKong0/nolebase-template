#ifndef __DRV_DISPLAY_H
#define __DRV_DISPLAY_H

#include "stm32f10x.h"

void DrvDisplay_Init(void);
void DrvDisplay_ShowLine(uint8_t line, const char *text);

#endif
