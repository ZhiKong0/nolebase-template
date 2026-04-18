#include "stm32f10x.h"
#include "Delay.h"
#include "OLED.h"
#include "Motor.h"
#include "Timer.h"
#include "MPU6050.h"
#include "Key.h"

// Global control system instance
#include "Control.h"
ControlSystem_t g_controlSys;
extern volatile uint32_t g_mpuReadOkCount;
extern volatile uint32_t g_mpuReadFailCount;

int main(void)
{
    // Initialize OLED
    OLED_Init();
    OLED_ShowString(1, 1, "Fuzzy PID Car");
    OLED_ShowString(2, 1, "Initializing...");

    // Initialize button
    Key_Init();

    // Initialize control system (speed test mode, skip MPU6050)
    // skipMPU=1: Skip MPU6050 initialization, use speed-only test mode
    Control_Init(&g_controlSys, 1);

    // Set target speed (must be set, otherwise car won't move forward)
    g_controlSys.targetSpeed = 1;  // Ultra low speed for testing (reduced from 3)

    // Initialize timer (1ms interrupt)
    Timer_Init();

    // Display ready status (diagnostic interface)
    OLED_ShowString(1, 1, "DIAG T0000 OK000");
    OLED_ShowString(2, 1, "F000 Y0000 E000");
    OLED_ShowString(3, 1, "C000 L000 R000 ");
    OLED_ShowString(4, 1, "RUN=0 MPU=?    ");

    // Main loop
    while (1)
    {
        // Main loop provides 1ms tick (avoid relying on TIM4 tick causing Control_Tick not to run)
        Control_Tick(&g_controlSys);

        uint8_t keyNum = Key_GetNum();
        if (keyNum == 1) {
            if (!g_controlSys.isRunning)
            {
                OLED_ShowString(1, 1, "Starting...     ");
                // In speed test mode, don't call Control_LockHeading (MPU6050 not initialized)
                if (g_controlSys.testMode != TEST_MODE_SPEED_ONLY) {
                    Control_LockHeading(&g_controlSys);
                }
                Control_Start(&g_controlSys);
                OLED_ShowString(1, 1, "Running...      ");
            }
            else
            {
                Control_Stop(&g_controlSys);
                OLED_ShowString(4, 1, "RUN=0 MPU=?    ");
            }
        } else if (keyNum == 2) {
            if (g_controlSys.isRunning) {
                Control_Stop(&g_controlSys);
                OLED_ShowString(4, 1, "RUN=0 MPU=?    ");
            } else {
                OLED_ShowString(1, 1, "Calibrating...  ");
                Control_Stop(&g_controlSys);
                if (g_controlSys.testMode != TEST_MODE_SPEED_ONLY) {
                    Control_LockHeading(&g_controlSys);
                }
                OLED_ShowString(1, 1, "DIAG T0000 OK000");
                OLED_ShowString(2, 1, "F000 Y0000 E000");
                OLED_ShowString(3, 1, "C000 L000 R000 ");
                OLED_ShowString(4, 1, "RUN=0 MPU=?    ");
            }
        }

        // Update display: heading angle and current Kp (observe fuzzy PID adaptive effect)
        static uint32_t lastDispTick = 0;
        if ((g_controlSys.tickCount - lastDispTick) >= 50) {
            lastDispTick = g_controlSys.tickCount;
            OLED_ShowNum(1, 7, (uint32_t)(g_controlSys.tickCount % 10000), 4);
            OLED_ShowNum(1, 14, (uint32_t)(g_mpuReadOkCount % 1000), 3);
            OLED_ShowNum(2, 2, (uint32_t)(g_mpuReadFailCount % 1000), 3);
            OLED_ShowSignedNum(2, 7, (int32_t)g_controlSys.mpu.yaw, 4);
            OLED_ShowSignedNum(2, 13, (int32_t)(g_controlSys.yawErr * 10.0f), 3);
            OLED_ShowSignedNum(3, 2, (int32_t)(g_controlSys.headingCorr * 10.0f), 3);
            OLED_ShowSignedNum(3, 8, (int32_t)g_controlSys.leftPWM, 3);
            OLED_ShowSignedNum(3, 13, (int32_t)g_controlSys.rightPWM, 3);
            OLED_ShowNum(4, 5, (uint32_t)g_controlSys.isRunning, 1);
        }

        Delay_ms(1);
    }
}