#include "stm32f10x.h"                  // Device header
#include "MPU6050.h"
#include "inv_mpu.h"

int16_t aacx,aacy,aacz;		
int16_t gyrox,gyroy,gyroz;	
float Pitch,Roll,Yaw; 		

void GONG_Init(void)
{
	mpu_dmp_get_data(&Pitch,&Roll,&Yaw);   //角度
					
    MPU6050_GetData(&gyrox,&gyroy,&gyroz,&aacx,&aacy,&aacz);
}
