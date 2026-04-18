#ifndef __PID_H
#define __PID_H

#include "stm32f10x.h"

typedef struct
{
	float Kp;
	float Ki;
	float Kd;
	float Integral;
	float PrevError;
	float OutMin;
	float OutMax;
} PID_t;

void PID_Init(PID_t *Pid, float Kp, float Ki, float Kd, float OutMin, float OutMax);
float PID_Update(PID_t *Pid, float Setpoint, float Measurement);
void PID_Reset(PID_t *Pid);

#endif
