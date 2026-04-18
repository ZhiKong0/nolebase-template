#ifndef __KEY_UTIL_H
#define __KEY_UTIL_H

#include "stm32f10x.h"
#include <stdint.h>

void key_pb5_init(void);
uint8_t key_pb5_read_raw(void);

#endif
