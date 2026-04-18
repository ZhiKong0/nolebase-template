#ifndef __PID_SPEED_H
#define __PID_SPEED_H

#include "stm32f10x.h"
#include <stdint.h>

typedef struct
{
    float kp;
    float ki;
    float kd;
    float dt;
    float integral;
    float integral_min;
    float integral_max;
    float prev_err;
    float out_min;
    float out_max;
} pid_speed_t;

typedef struct
{
    float kp;       /* proportional gain */
    float ki;       /* integral gain */
    float kd;       /* derivative gain */
    float kaw;      /* anti-windup back calculation gain */
    float dt;       /* loop period in seconds */
    float tau_d;    /* derivative filter time constant */

    float out_min;  /* output lower limit */
    float out_max;  /* output upper limit */
    float i_min;    /* integral state lower limit */
    float i_max;    /* integral state upper limit */

    float integral; /* integral contribution state */
    float d_state;  /* filtered measurement derivative */
    float prev_meas;/* last measurement */
    float prev_out; /* last saturated output */
} pid_adv_t;

void PID_SpeedInit(pid_speed_t* p, float kp, float ki, float kd, float dt,
                   float integral_min, float integral_max,
                   float out_min, float out_max);
void PID_SpeedReset(pid_speed_t* p);
float PID_SpeedUpdate(pid_speed_t* p, float target, float measure);

void PID_AdvInit(pid_adv_t* pid,
                 float kp, float ki, float kd, float kaw,
                 float dt, float tau_d,
                 float i_min, float i_max,
                 float out_min, float out_max);
void PID_AdvReset(pid_adv_t* pid, float meas, float out_init);
float PID_AdvUpdate(pid_adv_t* pid, float ref, float meas, float ff);

extern int16_t Speed1;
extern int16_t Speed2;

int PID_A(short Aima_v);
int PID_B(short Aimb_v);
void PID_ResetAB(void);

#endif
