#ifndef __MOTOR_H
#define __MOTOR_H

#include "stm32f10x.h"
#include <stdint.h>

void Motor_Init(uint16_t pwm_arr);
void Motor_Enable(uint8_t en);

void l_go(void);
void l_back(void);
void r_go(void);
void r_back(void);

void left(short output);
void right(short output);

#endif
