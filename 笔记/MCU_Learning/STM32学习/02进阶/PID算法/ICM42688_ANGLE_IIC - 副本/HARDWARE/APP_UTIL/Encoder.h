#ifndef __ENCODER_UTIL_H
#define __ENCODER_UTIL_H

#include "stm32f10x.h"
#include <stdint.h>

void encoder_tim2_init(void);
void encoder_tim3_init(void);
int16_t encoder_delta(uint16_t now, uint16_t* last);

#endif
