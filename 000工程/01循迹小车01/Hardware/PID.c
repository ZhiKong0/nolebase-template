#include "PID.h"

static float PID_Clamp(float x, float min, float max)
{
	if (x < min)
		return min;
	if (x > max)
		return max;
	return x;
}

void PID_Init(PID_t *Pid, float Kp, float Ki, float Kd, float OutMin, float OutMax)
{
	Pid->Kp = Kp;
	Pid->Ki = Ki;
	Pid->Kd = Kd;
	Pid->Integral = 0.0f;
	Pid->PrevError = 0.0f;
	Pid->OutMin = OutMin;
	Pid->OutMax = OutMax;
}

float PID_Update(PID_t *Pid, float Setpoint, float Measurement)
{
	float error = Setpoint - Measurement;
	Pid->Integral += error;
	{
		float iLimit = (Pid->OutMax > -Pid->OutMin) ? Pid->OutMax : -Pid->OutMin;
		if (iLimit < 1.0f)
			iLimit = 1.0f;
		Pid->Integral = PID_Clamp(Pid->Integral, -iLimit, iLimit);
	}

	float derivative = error - Pid->PrevError;
	Pid->PrevError = error;

	float out = (Pid->Kp * error) + (Pid->Ki * Pid->Integral) + (Pid->Kd * derivative);
	return PID_Clamp(out, Pid->OutMin, Pid->OutMax);
}

void PID_Reset(PID_t *Pid)
{
	Pid->Integral = 0.0f;
	Pid->PrevError = 0.0f;
}
