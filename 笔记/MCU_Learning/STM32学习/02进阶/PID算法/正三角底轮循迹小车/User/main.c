#include "stm32f10x.h"
#include "Delay.h"
#include "Motor.h"
#include "Encoder.h"
#include "OLED.h"
#include "Key.h"
#include "pid.h"
#include "Timer.h"
#include "Tracker.h"

uint8_t car_mode = 3;  // 1=循迹运行  3=停止

int main(void)
{
    SystemInit();
    Delay_Init();
    Motor_Init();
    Encoder_Init();
    OLED_Init();
    Key_Init();
    Tracker_Init();    // 循迹初始化
    PID_Init();
    TIM4_Init();       // 10ms 节拍，用于稳定执行循迹逻辑
    
    
    
    car_mode = 3;      // 初始停止

    OLED_Clear();
    OLED_ShowString(0, 0, "STM32 Car", 16);
    OLED_ShowString(0, 2, "Ready", 16);

    while(1)
    {
        Key_Scan();  // 按键扫描
        
        // 按键切换启停
        if(flag == 1)
        {
            flag = 0;
            if(car_mode == 3) 
                car_mode = 1;      // 启动循迹
            else {
                car_mode = 3;      // 停止
                Motor_Stop();
            }
        }

        // ====================== 纯循环执行循迹 ======================
        if(car_mode == 1)
        {
            if(Time10ms_Flag)
            {
                Time10ms_Flag = 0;
                xunji_8();
            }
        }
        else
        {
            Time10ms_Flag = 0;
            Motor_Stop(); // 停车
        }
    }
}
