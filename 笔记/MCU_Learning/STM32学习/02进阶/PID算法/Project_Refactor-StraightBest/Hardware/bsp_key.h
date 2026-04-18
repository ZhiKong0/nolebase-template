#ifndef __BSP_KEY_H
#define __BSP_KEY_H

#include "config.h"

void     BspKey_Init(void);
void     BspKey_Tick(uint32_t nowMs);
KeyEvent_t BspKey_Read(void);

#endif
