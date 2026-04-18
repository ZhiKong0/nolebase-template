#include "stm32f10x.h"                  // Device header
#include "PWM.h"

float va,vb,Aima_v, Aimb_v;
extern int16_t Speed1,Speed2;

void Motor_Init(void)
{
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);
	
	GPIO_InitTypeDef GPIO_InitStructure;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_4 | GPIO_Pin_5 | GPIO_Pin_2 | GPIO_Pin_3;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_Init(GPIOA, &GPIO_InitStructure);
	
	PWM_Init();
}

void l_go(void)
{
		GPIO_SetBits(GPIOA, GPIO_Pin_3);
		GPIO_ResetBits(GPIOA, GPIO_Pin_2);
}
void r_go(void)
{
		GPIO_SetBits(GPIOA, GPIO_Pin_4);
		GPIO_ResetBits(GPIOA, GPIO_Pin_5);
}

void l_back(void)
{
	  GPIO_SetBits(GPIOA, GPIO_Pin_2);
		GPIO_ResetBits(GPIOA, GPIO_Pin_3);
}

void r_back(void)
{
		GPIO_SetBits(GPIOA, GPIO_Pin_5);
		GPIO_ResetBits(GPIOA, GPIO_Pin_4);
}

void left( short output) 
{
    if(output>900)output=900;
	  else  if(output<-900)output=-900;
    if(output>0){l_go();PWM_SetCompare1(output);}
    else if(output<0){l_back();PWM_SetCompare1(-output);}
    
}

void right(short output1 ) 
{  
    if(output1>900)output1=900;
	  else  if(output1<-900)output1=-900;
    if(output1>0){r_go(); PWM_SetCompare2(output1);}
    else if(output1<0){r_back(); PWM_SetCompare2(-output1);}  
}

int PID_A(short Aima_v)
{
  static	float Kp =1.5, Ki = 0.5, Kd = 0;
  int32_t aim , error_now = 0, error_last = 0, v_now = 0, error_i = 0, out = 0;
  aim = Aima_v;
	v_now = Speed1;
	error_now = aim - v_now;
	error_i = error_i + error_now;
	out = Kp * error_now + Ki * error_i + Kd * (error_now - error_last);
	error_last=error_now;
	return out;
}

int PID_B(short Aimb_v)
{
	 float Kp =1.5, Ki = 0.1, Kd = 0;
	 int16_t aim , error_now = 0, error_last = 0, v_now = 0, error_i = 0, out = 0;
   aim = Aimb_v;
	 v_now = Speed2;
	 error_now = aim - v_now;
	 error_i = error_i + error_now;	
	 if(Aima_v==0)error_i=0;
	 out = Kp * error_now + Ki * error_i + Kd * (error_now - error_last);
	 error_last=error_now;
   return out;
}

int angle(float Angle,float Gyroy,float Mechanical_Angle)
{
	float Kp = 10; //       
  float Kd = 0;//      
	float Bias; //角度误差值
	int balance_up; //直立环控制PWM
	Bias=Angle-Mechanical_Angle; //角度误差值==测量的俯仰角-理想角度（机械平衡角度）
	balance_up= Kp*Bias+ Kd*Gyroy; //计算平衡控制的电机PWM  PD控制   Up_balance_KP是P系数,Up_balance_KD是D系数
	return balance_up;
} 

