//#include "stm32f10x.h"                  // Device header
//#include "Delay.h"
//#include "Encoder.h"
//#include "Motor.h"
//#include "PWM.h"
//#include "MPU6050.h"
//#include "inv_mpu.h"

////int16_t aacx,aacy,aacz;		
////int16_t Speed1,Speed2;
////int16_t gyrox,gyroy,gyroz;	
////float Pitch,Roll,Yaw; 		
////int Encoder_left,Encoder_right;
////float target_speed,turn_speed;
////int Moto1,Moto2;//两个电机
////int Out_Vertical,Out_Velocity,Out_Turn;//三个环对应的输出值
////int Encoder_left,Encoder_right;//两个编码电机

//////参数
////float target_speed=0,turn_speed=5;

////期望角度
////float expect_angle=0;	//机械中值//0.7

////ֱ直立环的Kp与Kd
//float Vertical_Kp=-168, Vertical_Kd=1.432;//-280,-168 //2.38,1.47

////速度环的KP与Kd
//float Velocity_Kp=-0.45,Velocity_Ki=-0.00225;//-0.45,-0.00225

//float turn_kd=0.8,turn_kp=20;  

////#define MAX_Speed 10 
////#define MAX_Turn_Speed 80

////int Vertical(float Expect_Angle,float Real_Angle,float gyroy);
////int Velocity(int target,int encoder_left,int encoder_right);
////int Turn(int gyro_Z,int turn_angle);                             
//                             
////void Control_Init(void)
////{   
////		mpu_dmp_init();
////	                int PWM_out;
////					Encoder_left = -Speed1;//电机是相对安装，刚好相差180度为了编码器输出极性一致，就需要对其中一个取反
////					Encoder_right= Speed2;
////					
////					//1利用MPU的数据库进行读取对应值
////					mpu_dmp_get_data(&Pitch,&Roll,&Yaw);   //角度
////					
////                    MPU6050_GetData(&gyrox,&gyroy,&gyroz,&aacx,&aacy,&aacz);//陀螺仪都读取--角速度	加速度读取

////			
////					//2 提取三个环的输出值
////					Out_Velocity  =	 Velocity(target_speed,Encoder_left,Encoder_right);//速度环
////					Out_Vertical  =  Vertical(Out_Velocity+expect_angle,Pitch,gyroy);  //直立环
////					Out_Turn	  =	 Turn(gyroz,turn_speed);                           //转向环
////					
////					PWM_out		  =	 Out_Vertical;//直立环-KP1*速度环-Vertical_Kp*Out_Velocity
////				
////		            //3.把控制输出量加载到电机上，完成最终的控制
////		            Moto1	= PWM_out;//左电机-Out_Turn
////		            Moto2	= PWM_out;//右电机+Out_Turn
////					Limit(&Moto1,&Moto2);//PWM限幅
////		            Load(Moto1,Moto2);   //加载到电机上
////					Stop(&expect_angle,&Pitch);
////}



////（直立环）角度环PD控制：KP*EK（偏差）+KD*EK_D（偏差的微分）
//int Vertical(float Expect_Angle,float Real_Angle,float gyroy)    //入口：分别代表期望角度，真实角度，角速度
//{
//		int PWM_out;
//	
//		PWM_out=Vertical_Kp*( Real_Angle-Expect_Angle)  + Vertical_Kd*( gyroy - 0 );

//		return PWM_out;//出口：直立环输出
//}



////速度环PI控制：KP*EK+KI+EK_S（偏差的积分）
//int Velocity(int target,int encoder_left,int encoder_right)	
//{
//		static int PWM_out,Encoder_value,Encoder_Sum_value;
//		static int Encoder_value_low_out,Encoder_value_low_out_last;
//		float a=0.7;
//	    //1 计算速度偏差
//	    Encoder_value	=	(encoder_left+encoder_right)	-	target;//舍去误差
//		//2 低通滤波,防止速度控制对直立造成干扰
//		Encoder_value_low_out = (1-a) * Encoder_value+	a*Encoder_value_low_out_last;
//	    //使得波形更加平滑，滤除高频干扰，防速度突变
//	    //防速度过大的影响直立环的正常工作
//		Encoder_value_low_out_last	=		Encoder_value_low_out;
//	
//	//3 对速度偏差进行积分求和
//	Encoder_Sum_value+=Encoder_value_low_out;
//	//4 对速度偏差积分，积分出位移，积分限幅
//	Encoder_Sum_value=Encoder_Sum_value>10000?10000:(Encoder_Sum_value<(-10000)?(-10000):Encoder_Sum_value);
//    //5 速度环控制输出计算
//	PWM_out	=	Velocity_Kp*Encoder_value_low_out	+	Velocity_Ki*Encoder_Sum_value;
//	
//		return PWM_out;
//}



////转向环：Z轴的角速度
//int Turn(int gyro_Z,int turn_angle)
//{
//	int PWM_out;
//	
//	PWM_out=turn_kd*gyro_Z  +	turn_kp*turn_angle;
//	
//	return PWM_out;
//}

