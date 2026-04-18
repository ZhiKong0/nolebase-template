#ifndef __PID_H
#define __PID_H

#include "stm32f10x.h"

typedef struct {
    float Kp;
    float Ki;
    float Kd;

    float output;
    float outputLimit;

    float e1;
    float e2;
} PID_t;

void PID_Init(PID_t *pid, float kp, float ki, float kd, float outLimit);
void PID_Reset(PID_t *pid);
float PID_CalcIncremental(PID_t *pid, float err);

#endif
