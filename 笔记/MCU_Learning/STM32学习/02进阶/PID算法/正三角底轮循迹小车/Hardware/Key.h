#ifndef __KEY_H
#define __KEY_H

#include "stm32f10x.h"



extern uint8_t flag;

void Key_Init(void);
void Key_Scan(void);

#endif