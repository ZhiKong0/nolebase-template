#ifndef __PWM_H
#define __PWM_H

#include "stm32f10x.h"
#include <stdint.h>

void PWM_Init(uint16_t arr);
void PWM_SetCompare1(uint16_t compare);
void PWM_SetCompare2(uint16_t compare);

#endif
