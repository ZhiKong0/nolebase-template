#include "stm32f10x.h"
#include "Delay.h"
#include "OLED.h"
#include "Motor.h"
#include "Timer.h"
#include "MPU6050.h"
#include "Key.h"

// 全局控制系统实例
#include "Control.h"
ControlSystem_t g_controlSys;
extern volatile uint32_t g_mpuReadOkCount;
extern volatile uint32_t g_mpuReadFailCount;

int main(void)
{
    // 初始化 OLED
    OLED_Init();
    OLED_ShowString(1, 1, "Fuzzy PID Car");
    OLED_ShowString(2, 1, "Initializing...");
    
    // 初始化按键
    Key_Init();
    
    // 初始化控制系统（正常模式，使用模糊PID航向控制）
    // skipMPU=0: 初始化MPU6050和模糊PID
    Control_Init(&g_controlSys, 0);
    
    // 设置目标速度（必须设置，否则小车不前进）
    g_controlSys.targetSpeed = 5;  // 设置基础速度
    
    // 初始化定时器（1ms中断）
    Timer_Init();

    // 显示就绪状态（诊断界面）
    OLED_ShowString(1, 1, "DIAG T0000 O000");
    OLED_ShowString(2, 1, "F000 Y0000 E000");
    OLED_ShowString(3, 1, "C000 L000 R000 ");
    OLED_ShowString(4, 1, "RUN=0 MPU=?    ");
    
    // 主循环
    while (1)
    {
        // 主循环提供1ms节拍（避免依赖TIM4打点导致Control_Tick不跑）
        Control_Tick(&g_controlSys);

        uint8_t keyNum = Key_GetNum();
        if (keyNum == 1) {
            if (!g_controlSys.isRunning)
            {
                OLED_ShowString(1, 1, "Starting...     ");
                Control_LockHeading(&g_controlSys);  // 锁定当前航向
                Control_Start(&g_controlSys);
                OLED_ShowString(1, 1, "Running...      ");
            }
            else
            {
                Control_Stop(&g_controlSys);
                OLED_ShowString(4, 1, "RUN=0 MPU=?    ");
            }
        } else if (keyNum == 2) {
            OLED_ShowString(1, 1, "Calibrating...  ");
            MPU6050_Calibrate(&g_controlSys.mpu, 100);
            OLED_ShowString(1, 1, "DIAG T0000 O000");
            OLED_ShowString(2, 1, "F000 Y0000 E000");
            OLED_ShowString(3, 1, "C000 L000 R000 ");
            OLED_ShowString(4, 1, "RUN=0 MPU=?    ");
        }
        
        // 更新显示：航向角和当前Kp（观察模糊PID自适应效果）
        static uint32_t lastDispTick = 0;
        if ((g_controlSys.tickCount - lastDispTick) >= 50) {
            lastDispTick = g_controlSys.tickCount;
            OLED_ShowSignedNum(1, 7, (int32_t)(g_controlSys.tickCount % 10000), 4);
            OLED_ShowSignedNum(1, 13, (int32_t)(g_mpuReadOkCount % 1000), 3);
            OLED_ShowSignedNum(2, 2, (int32_t)(g_mpuReadFailCount % 1000), 3);
            OLED_ShowSignedNum(2, 7, (int32_t)g_controlSys.mpu.yaw, 4);
            OLED_ShowSignedNum(2, 13, (int32_t)(g_controlSys.yawErr * 10.0f), 3);
            OLED_ShowSignedNum(3, 2, (int32_t)(g_controlSys.headingCorr * 10.0f), 3);
            OLED_ShowSignedNum(3, 8, (int32_t)g_controlSys.leftPWM, 3);
            OLED_ShowSignedNum(3, 13, (int32_t)g_controlSys.rightPWM, 3);
            OLED_ShowSignedNum(4, 5, (int32_t)g_controlSys.isRunning, 1);
        }

        Delay_ms(1);
    }
}