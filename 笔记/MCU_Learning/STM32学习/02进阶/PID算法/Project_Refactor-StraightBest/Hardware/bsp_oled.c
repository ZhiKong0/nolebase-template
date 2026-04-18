#include "bsp_oled.h"
#include "OLED.h"
#include <stdio.h>

void BspOled_Init(void)
{
    OLED_Init();
    OLED_Clear();
    OLED_ShowString(1, 1, "System Ready");
}

void BspOled_ShowStatus(SystemState_t state, ControlMode_t mode,
                        float yaw, float speedL, float speedR)
{
    char buf[17];

    /* Line 1: State */
    switch (state) {
    case SYS_STOP:     OLED_ShowString(1, 1, "STOP    "); break;
    case SYS_STRAIGHT: OLED_ShowString(1, 1, "STRAIGHT"); break;
    case SYS_TRACKING: OLED_ShowString(1, 1, "TRACKING"); break;
    default:           OLED_ShowString(1, 1, "???     "); break;
    }

    /* Line 1 col 10: Mode indicator */
    OLED_ShowString(1, 10, (mode == MODE_TRACK) ? "T" : "S");

    /* Line 2: Yaw */
    sprintf(buf, "Y:%+7.1f", (double)yaw);
    OLED_ShowString(2, 1, buf);

    /* Line 3: Speed L/R */
    sprintf(buf, "L:%+5.0f R:%+5.0f", (double)speedL, (double)speedR);
    OLED_ShowString(3, 1, buf);
}

void BspOled_ShowIMUInit(uint8_t stage, uint8_t addr)
{
    char buf[17];
    sprintf(buf, "IMU stg=%d", stage);
    OLED_ShowString(4, 1, buf);
    sprintf(buf, "addr=0x%02X", addr);
    OLED_ShowString(4, 10, buf);
}

void BspOled_Clear(void)
{
    OLED_Clear();
}
