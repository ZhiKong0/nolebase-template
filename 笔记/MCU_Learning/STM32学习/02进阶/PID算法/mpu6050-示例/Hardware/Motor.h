#ifndef __MOTOR_H
#define __MOTOR_H

void Motor_Init(void);

void l_go(void);
void r_go(void);
void l_back(void);
void r_back(void);

void right( short output) ;
void left(short output1 ) ;

int PID_A(short Aima_v);
int PID_B(short Aimb_v);
int angle(float Angle,float Gyroy,float Mechanical_Angle);
	
#endif
