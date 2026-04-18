/**
  *****************************************************************************
  * @file    : bsp_track.c
  * @author  : 至善电子科技
  * @version : V2.3.0-B
  * @date    : 2025-04-22
  * @brief   : 5路红外循迹程序
  *****************************************************************************
  * @attention
	*  主板    : SmartCar V2.3.0_B
  *****************************************************************************
	* COPYRIGHT：本程序只供学习使用，未经作者许可，不得用于其它任何用途。
	*****************************************************************************
**/
#include "bsp_track.h"
#include "bsp_systick.h"
#include "bsp_pid_control.h"
#include "bsp_led.h"
#include "bsp_motor_drv.h "
#include "bsp_usart.h"
#include "bsp_beep.h"

#include "bsp_ui.h"
#include "bsp_GeneralTim3.h"


Track_TypeDef     track = {  track_starting,         //state    
                                          0,         //bearing_dev
                                       0x08,         //sensor_data
                                          0,         //last_data
                                          0,         //filter_times
                                          4,         //crossing
                                          0,         //cross_count
                                          1,         //cross_state 
                                       stop          //auto_flag                                          
                                        };     

/**
  * @brief  红外循迹端口配置
  * @param  无
  * @retval 无
  */
void Track_GPIO_Config(void)
{
	  /***定义循迹初始化结构体***/
	 GPIO_InitTypeDef  Track_Obs_InitStruct; 
				
	 /***使能循迹端口的时钟***/
	 RCC_APB2PeriphClockCmd(Track_GPIO_CLK, ENABLE);
		 	
	 Track_Obs_InitStruct.GPIO_Pin = (M_Track_Pin | L0_Track_Pin | L1_Track_Pin | R0_Track_Pin);
	 Track_Obs_InitStruct.GPIO_Mode = GPIO_Mode_IN_FLOATING;
	
	 /***初始化循迹端口GPIOB的结构体***/
	 GPIO_Init(GPIOB, &Track_Obs_InitStruct);
	
	  /***初始化循迹端口GPIOA的结构体***/
   Track_Obs_InitStruct.GPIO_Pin = ( R1_Track_Pin);
	 Track_Obs_InitStruct.GPIO_Mode = GPIO_Mode_IN_FLOATING;
   GPIO_Init(GPIOA, &Track_Obs_InitStruct);
}


/**
  * @brief  红外循迹初始化
  * @param  无
  * @retval 无
  */
void Track_Init(void)
{
	
	Track_GPIO_Config();
	
}


/**
  * @brief  读取红外循迹传感器数据。
            红外传感器识别到黑线返回数字信号1，即高电平；未识别到黑线返回0，即低电平。 
  * @param  无
  * @retval 无
  */
void read_data(void)
{
  
	if( GPIO_ReadInputDataBit(M_Track_PORT, M_Track_Pin) )
     track.sensor_data |=  0x08 ;    //检测到黑线时，该位置1。               
	else
		 track.sensor_data &= ~0x08 ;    //未检测到黑线时，该位清0。
	
	if( GPIO_ReadInputDataBit(L0_Track_PORT, L0_Track_Pin) )
     track.sensor_data |=  0x10 ;
	else
		 track.sensor_data &= ~0x10 ;
	
  if( GPIO_ReadInputDataBit(L1_Track_PORT, L1_Track_Pin) )
     track.sensor_data |=  0x20 ;
	else
		 track.sensor_data &= ~0x20 ;	
	
	if( GPIO_ReadInputDataBit(R0_Track_PORT, R0_Track_Pin) )
     track.sensor_data |=  0x04 ;
	else
		 track.sensor_data &= ~0x04 ;
	
	if( GPIO_ReadInputDataBit(R1_Track_PORT, R1_Track_Pin) )
     track.sensor_data |=  0x02 ;
	else
		 track.sensor_data &= ~0x02 ;
		
}


/**
  * @brief  红外循迹信号处理
  * @param  无
  * @retval 无 
  */

void Signal_Handler(void)
{	 
	
 static uint8_t  overrun_level = 0; //允许小车脱离黑线的时间 
    		
	
	 track.filter_times++;		
	if( track.filter_times >= 50  )
	 {  track.filter_times = 50;	}
	 
	 read_data( );//读取红外循迹传感器的数值
		
	if(track.sensor_data != 0x00)//如果检测到黑线，overrun_level清零。
	 { overrun_level = 0; }
	 
	if( track.sensor_data&0x22 )//直角、锐角标记, 0x22-过滤中间的三个传感器状态
	track.last_data = track.sensor_data;
	
	/***检测到黑线时为1，没有检测到黑线时为0。***/
	switch(track.sensor_data)
	{
		
		case 0x00 :					          		                
		            if((track.last_data & 0x20) )//0x20;刚刚左侧直角、锐角 
									corner_handler( dir_L, 2000, 2400 );
								
		            else if((track.last_data & 0x02) )//0x02;刚刚右侧直角、锐角 
                  corner_handler( dir_R, 2400, 2000 );
								
								else	
								{ 
									overrun_level++;
/***如果小车短时脱离轨道，允许小车继续跑，超过设定时间后，判定小车失去追踪信号，小车停止。***/										
                  if( overrun_level == 50 )//这个参数是允许小车脱离轨道运行的时间参数 *20ms。
                  {   										
									  overrun_level = 0;
                    track.auto_flag = stop ;//停止循迹  										
									}
								}	
									break;
									
		/***识别到黑线在中间 X000 1000，小车前进。 ***/
		case 0x08 :				
		              track.bearing_dev = 0;
									break;
							 
		/*** 小车位置有点偏右- 1 X001 1000***/
		case 0x18 :   			          
									track.bearing_dev = -1;         		
									break;	
		
		/*** 小车位置有点偏左+ 1 X000 1100***/
		case 0x0c :                    
									track.bearing_dev = 1;             		
									break;
		
		/*** 小车位置偏右-- 2   X001 0000***/
		case 0x10 :		                                                                     
									track.bearing_dev = -2;         		
									break;
		
		/*** 小车位置偏左++ 2  X000 0100***/
		case 0x04 :                                 		              		                               
									track.bearing_dev = 2;            	
									break;

    /*** 小车位置偏右--- 3***/
		case 0x30 :											              
									track.bearing_dev = -4;//														  								 
									break;		
		
		/*** 小车位置偏左+++ 3***/		
		case 0x06 :											             
									track.bearing_dev = 4;//						                   									               
									break;		
								
		/*** 小车位置偏右---- 4***/
		case 0x20 :								                  
									track.bearing_dev = -7;																										     		
									break;
		
		/*** 小车位置偏左++++ 4***/
		case 0x02 :									                
									track.bearing_dev = 7;//																                   		
									break;
		

		/***识别交叉路口***/						      
		case 0x3e :				
									switch(track.cross_state)
									{	
										case 1:
											 /*********计算交叉路口*********/
		                        track.cross_count++;  
										        track.cross_state = 2;
										        track.filter_times = 0;
										       	break;
										
										
										case 2:
											 /*********计算交叉路口*********/
		                  if(  track.filter_times >= 50 )	//50 
											 {
													  track.filter_times = 0; 
													  track.cross_count++;													  											   
																					      												
											 /*********停止循迹*********/
											 if( ( track.cross_count >= track.crossing ) )  												   												   
														 track.auto_flag = stop ; //停止循迹												 
												 								                  														 
                       }												 
                           break;
									 default : break;			
								  }
									
									break;	
							
		default : break;
		
	}
		
	
}


/**
  * @brief  急拐弯处理函数,应用于直角、锐角------阻塞函数。
  * @param  uint8_t dir,    方向
  *         uint16_t L_PWM, 左PWM
  *         uint16_t R_PWM  右PWM
  * @retval 无
  */
void corner_handler( uint8_t dir_, uint16_t L_PWM, uint16_t R_PWM )
{
	
		motor_Stop( );
		SysTick_Delay_ms(5); 
	 
	 if( dir_ == dir_R )//右转	
		motor_Turn_Right_B( L_PWM , R_PWM ); 
	 else if( dir_ == dir_L )//左转
		motor_Turn_Left_B( L_PWM , R_PWM );
	 
		while(1)
		{
			read_data( );
			if(track.sensor_data&0x3e )//传感器检测到黑线
			 {				 
				 motor_Stop( );				 
				 SysTick_Delay_ms(5);											 								 												
				 break;
			 }	 											
		}   		

			
}


/**
  * @brief  小车自动循迹
  * @param  无
  * @retval 无
  */
void car_auto_track(void)
{
	switch(track.state)
	 {
		 //小车启动中
		 case track_starting :   
		                         track.auto_flag = start;   //自动循迹---启动	                           														  
		                         track.state = track_running ; //切换到运行状态
		                         break;
	   //小车循迹中
		 case track_running :      		                         
														 Signal_Handler( );	
														 Track_Handler( );       
																												 
														 //如果：1.小车识别到终点线；2.小车脱离轨道；则小车停止。
														 if(track.auto_flag == stop )//停车标志
															{	                                																
																motor_Stop();        //小车停止。小车停的快一点。
																Beep_Led_Long();   //蜂鸣器长鸣一声,LED灯亮一下	
																motor_drv.control_mes = drive_mes_stop;	    //在motor_drive_Handler()中，小车由自动循迹状态切换到停车状态。 										
															} 
														 break;	

		default: break;
			
	}
	 
			
}


/**
  * @brief  寻迹变量状态恢复
  * @param  无
  * @retval 无
  */
void  Auto_track_variable_restore(void)
{
	
	 track.auto_flag = stop ;				//自动循迹---停止											
	 track.state = track_starting ;	//切换到启动状态
	 track.cross_count = 0;         //检测到的交叉路口数清0     
	 track.cross_state = 1;         //交叉路口识别状态恢复初始值      
	 track.filter_times = 0;
	 track.last_data = 0x08;
		
} 



/************************END OF FILE****************************/


